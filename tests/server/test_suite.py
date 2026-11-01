#!/usr/bin/env python3
"""
Comprehensive HTTP/1.1 Server Test Suite
Tests both threadpool and eventloop concurrency modes against all assignment requirements.
"""

import socket
import subprocess
import time
import sys
import os
import signal
from concurrent.futures import ThreadPoolExecutor

SERVER_BIN = "./server"
DOC_ROOT = "./www"
TEST_PORT_TP = 18080
TEST_PORT_EL = 18081

class TestFailure(Exception):
    pass

def log(msg):
    print(f"[TEST] {msg}", flush=True)

def raw_http_exchange(port, request_bytes, timeout=3.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(("127.0.0.1", port))
    s.sendall(request_bytes)
    
    response = b""
    while True:
        try:
            chunk = s.recv(4096)
            if not chunk:
                break
            response += chunk
        except socket.timeout:
            break
    s.close()
    return response

def parse_http_response(raw_bytes):
    headers_part = raw_bytes
    body = b""
    if b"\r\n\r\n" in raw_bytes:
        headers_part, body = raw_bytes.split(b"\r\n\r\n", 1)
    
    lines = headers_part.decode("iso-8859-1").split("\r\n")
    status_line = lines[0] if lines else ""
    parts = status_line.split(" ", 2)
    status_code = int(parts[1]) if len(parts) >= 2 else 0
    
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
            
    return status_code, headers, body

def run_tests_on_server(port, mode_name):
    log(f"==================================================")
    log(f"Running Full Test Battery on Mode: {mode_name.upper()} (Port {port})")
    log(f"==================================================")

    # Test 1: TCP Client Connection
    log("Test 1: TCP client connection...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2.0)
    s.connect(("127.0.0.1", port))
    s.close()
    log("  PASS: Connection accepted and closed cleanly")

    # Test 2: Valid GET Request
    log("Test 2: Valid GET request (/index.html)...")
    req = b"GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    res = raw_http_exchange(port, req)
    code, hdrs, body = parse_http_response(res)
    if code != 200:
        raise TestFailure(f"Expected 200 OK, got {code}")
    if "text/html" not in hdrs.get("content-type", ""):
        raise TestFailure(f"Expected text/html, got {hdrs.get('content-type')}")
    if b"HTTP/1.1 Server is Working!" not in body:
        raise TestFailure("Body does not contain expected HTML text")
    log("  PASS: Valid GET returned 200 OK with correct HTML body")

    # Test 3: Valid HEAD Request
    log("Test 3: Valid HEAD request (/index.html)...")
    req = b"HEAD /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    res = raw_http_exchange(port, req)
    code, hdrs, body = parse_http_response(res)
    if code != 200:
        raise TestFailure(f"Expected 200 OK, got {code}")
    if len(body) > 0:
        raise TestFailure(f"HEAD must return NO body, received {len(body)} bytes")
    if "content-length" not in hdrs or int(hdrs["content-length"]) <= 0:
        raise TestFailure("HEAD response missing or invalid Content-Length header")
    log("  PASS: Valid HEAD returned 200 OK with accurate headers and empty body")

    # Test 4: Invalid HTTP Request / Malformed Syntax (400 Bad Request)
    log("Test 4: Invalid HTTP request syntax (400 Bad Request)...")
    req = b"INVALID_REQUEST_SYNTAX\r\n\r\n"
    res = raw_http_exchange(port, req)
    code, hdrs, body = parse_http_response(res)
    if code != 400:
        raise TestFailure(f"Expected 400 Bad Request, got {code}")
    log("  PASS: Malformed request returned 400 Bad Request")

    # Test 5: Missing Host Header in HTTP/1.1 (RFC 7230 -> 400 Bad Request)
    log("Test 5: HTTP/1.1 Request missing Host header (400 Bad Request)...")
    req = b"GET /index.html HTTP/1.1\r\nConnection: close\r\n\r\n"
    res = raw_http_exchange(port, req)
    code, hdrs, body = parse_http_response(res)
    if code != 400:
        raise TestFailure(f"Expected 400 Bad Request for missing Host header, got {code}")
    log("  PASS: Missing Host header correctly rejected with 400 Bad Request")

    # Test 6: Missing File (404 Not Found)
    log("Test 6: Missing file (/nonexistent_file_xyz.txt -> 404)...")
    req = b"GET /nonexistent_file_xyz.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    res = raw_http_exchange(port, req)
    code, hdrs, body = parse_http_response(res)
    if code != 404:
        raise TestFailure(f"Expected 404 Not Found, got {code}")
    log("  PASS: Nonexistent file returned 404 Not Found")

    # Test 7: Unsupported HTTP Method (POST -> 405 Method Not Allowed)
    log("Test 7: Unsupported method (POST -> 405 Method Not Allowed)...")
    req = b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    res = raw_http_exchange(port, req)
    code, hdrs, body = parse_http_response(res)
    if code != 405:
        raise TestFailure(f"Expected 405 Method Not Allowed, got {code}")
    if "allow" not in hdrs or "GET" not in hdrs["allow"]:
        raise TestFailure("Expected 'Allow: GET, HEAD' header in 405 response")
    log("  PASS: POST method returned 405 Method Not Allowed with Allow header")

    # Test 8: Unknown HTTP Method (CUSTOMMETHOD -> 501 Not Implemented)
    log("Test 8: Unknown method (CUSTOMMETHOD -> 501 Not Implemented)...")
    req = b"CUSTOMMETHOD /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    res = raw_http_exchange(port, req)
    code, hdrs, body = parse_http_response(res)
    if code != 501:
        raise TestFailure(f"Expected 501 Not Implemented, got {code}")
    log("  PASS: Unknown method returned 501 Not Implemented")

    # Test 9: MIME Type & Content-Length Accuracy across file types
    log("Test 9: Content-Type & Content-Length across static file types...")
    test_files = [
        ("/index.html", "text/html", "www/index.html"),
        ("/style.css", "text/css", "www/style.css"),
        ("/test.txt", "text/plain", "www/test.txt"),
        ("/sample.js", "application/javascript", "www/sample.js"),
        ("/sample.json", "application/json", "www/sample.json"),
        ("/test.pdf", "application/pdf", "www/test.pdf"),
    ]
    for uri, expected_mime, filepath in test_files:
        expected_size = os.path.getsize(filepath)
        req = f"GET {uri} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n".encode("utf-8")
        res = raw_http_exchange(port, req)
        code, hdrs, body = parse_http_response(res)
        if code != 200:
            raise TestFailure(f"Failed to fetch {uri}, got {code}")
        if expected_mime not in hdrs.get("content-type", ""):
            raise TestFailure(f"MIME mismatch for {uri}: expected {expected_mime}, got {hdrs.get('content-type')}")
        if int(hdrs.get("content-length", -1)) != expected_size:
            raise TestFailure(f"Content-Length mismatch for {uri}: expected {expected_size}, got {hdrs.get('content-length')}")
        if len(body) != expected_size:
            raise TestFailure(f"Body length mismatch for {uri}: expected {expected_size}, got {len(body)}")
        log(f"  -> {uri}: {expected_mime}, {expected_size} bytes verified")
    log("  PASS: All static MIME types and Content-Lengths match exact file size")

    # Test 10: Persistent Connection (Multiple sequential requests on ONE TCP socket)
    log("Test 10: Persistent connection (sequential requests on one socket)...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(3.0)
    s.connect(("127.0.0.1", port))

    # Request 1: GET /index.html
    s.sendall(b"GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n")
    resp_data1 = b""
    while b"\r\n\r\n" not in resp_data1:
        resp_data1 += s.recv(1024)
    code1, hdrs1, body1 = parse_http_response(resp_data1)
    cl1 = int(hdrs1["content-length"])
    while len(body1) < cl1:
        body1 += s.recv(1024)
    if code1 != 200:
        raise TestFailure(f"Req 1 failed with {code1}")

    # Request 2: GET /test.txt on SAME socket
    s.sendall(b"GET /test.txt HTTP/1.1\r\nHost: localhost\r\n\r\n")
    resp_data2 = b""
    while b"\r\n\r\n" not in resp_data2:
        resp_data2 += s.recv(1024)
    code2, hdrs2, body2 = parse_http_response(resp_data2)
    cl2 = int(hdrs2["content-length"])
    while len(body2) < cl2:
        body2 += s.recv(1024)
    if code2 != 200:
        raise TestFailure(f"Req 2 failed with {code2}")

    # Request 3: HEAD /style.css on SAME socket
    s.sendall(b"HEAD /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
    resp_data3 = b""
    while b"\r\n\r\n" not in resp_data3:
        resp_data3 += s.recv(1024)
    code3, hdrs3, body3 = parse_http_response(resp_data3)
    if code3 != 200:
        raise TestFailure(f"Req 3 failed with {code3}")

    s.close()
    log("  PASS: 3 sequential requests completed on single persistent TCP connection")

    # Test 11: Connection: close behavior
    log("Test 11: Connection: close header verification...")
    req = b"GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2.0)
    s.connect(("127.0.0.1", port))
    s.sendall(req)
    res = b""
    while True:
        chunk = s.recv(1024)
        if not chunk:
            break
        res += chunk
    s.close()
    code, hdrs, body = parse_http_response(res)
    if hdrs.get("connection", "").lower() != "close":
        raise TestFailure(f"Expected Connection: close, got {hdrs.get('connection')}")
    log("  PASS: Connection: close acknowledged and socket closed by server")

    # Test 12: Directory Traversal Protection (403 Forbidden / 404)
    log("Test 12: Directory traversal protection (/../../etc/passwd)...")
    traversal_paths = [
        "/../../etc/passwd",
        "/../Makefile",
        "/style/../../secret.txt",
        "/....//....//etc/passwd",
        "/%2e%2e/%2e%2e/etc/passwd",
    ]
    for path in traversal_paths:
        req = f"GET {path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n".encode("utf-8")
        res = raw_http_exchange(port, req)
        code, hdrs, body = parse_http_response(res)
        if code not in (403, 404):
            raise TestFailure(f"Traversal path '{path}' was not blocked! Status code: {code}")
        log(f"  -> Path '{path}' safely rejected with HTTP {code}")
    log("  PASS: Directory traversal attempts successfully blocked")

    # Test 13: Incomplete TCP Reads (Pipelined / Chunked transmission)
    log("Test 13: Incomplete TCP reads (packet fragmentation simulation)...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(3.0)
    s.connect(("127.0.0.1", port))
    part1 = b"GET /index"
    part2 = b".html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    s.sendall(part1)
    time.sleep(0.1)  # Simulate network latency between TCP segments
    s.sendall(part2)
    res = b""
    while True:
        chunk = s.recv(1024)
        if not chunk:
            break
        res += chunk
    s.close()
    code, hdrs, body = parse_http_response(res)
    if code != 200:
        raise TestFailure(f"Incomplete read failed to reassemble request, got code {code}")
    log("  PASS: Incomplete TCP read correctly buffered and reassembled")

    # Test 14: URI Too Long (414)
    log("Test 14: Very long URI handling (414 URI Too Long)...")
    long_path = "/" + ("a" * 8200)
    req = f"GET {long_path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n".encode("utf-8")
    res = raw_http_exchange(port, req)
    code, hdrs, body = parse_http_response(res)
    if code != 414:
        raise TestFailure(f"Expected 414 URI Too Long, got {code}")
    log("  PASS: URI exceeding maximum length rejected with 414")

    # Test 15: Header Fields Too Large (431)
    log("Test 15: Header fields too large (431 Request Header Fields Too Large)...")
    huge_header = "X-Large-Header: " + ("B" * 17000) + "\r\n"
    req = f"GET /index.html HTTP/1.1\r\nHost: localhost\r\n{huge_header}Connection: close\r\n\r\n".encode("utf-8")
    res = raw_http_exchange(port, req)
    code, hdrs, body = parse_http_response(res)
    if code != 431:
        raise TestFailure(f"Expected 431 Request Header Fields Too Large, got {code}")
    log("  PASS: Oversized headers rejected with 431")

    # Test 16: Multiple Simultaneous Clients Concurrency
    log("Test 16: Multiple simultaneous clients (25 concurrent workers)...")
    def single_worker(worker_id):
        req = b"GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
        res = raw_http_exchange(port, req, timeout=5.0)
        c, _, _ = parse_http_response(res)
        return c == 200

    with ThreadPoolExecutor(max_workers=25) as executor:
        results = list(executor.map(single_worker, range(50)))
    success_count = sum(1 for r in results if r)
    if success_count != 50:
        raise TestFailure(f"Only {success_count}/50 concurrent requests succeeded")
    log(f"  PASS: 50 concurrent requests completed successfully ({success_count}/50)")

    log(f"All tests PASSED for {mode_name.upper()} mode!\n")

def test_server_mode(mode_name, port):
    cmd = [SERVER_BIN, "--port", str(port), "--root", DOC_ROOT, "--mode", mode_name]
    if mode_name == "threadpool":
        cmd.extend(["--threads", "8"])

    log(f"Starting server in {mode_name} mode: {' '.join(cmd)}")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.3)  # Allow socket to bind

    # Verify server is alive
    if proc.poll() is not None:
        stdout, stderr = proc.communicate()
        raise TestFailure(f"Server exited unexpectedly:\nSTDOUT: {stdout.decode()}\nSTDERR: {stderr.decode()}")

    try:
        run_tests_on_server(port, mode_name)
    finally:
        # Test Graceful Shutdown
        log(f"Testing Graceful Shutdown on {mode_name} server (SIGINT)...")
        proc.send_signal(signal.SIGINT)
        try:
            stdout, stderr = proc.communicate(timeout=3.0)
            log(f"  PASS: Server shut down gracefully with return code {proc.returncode}")
        except subprocess.TimeoutExpired:
            proc.kill()
            raise TestFailure(f"Server did not terminate gracefully within timeout")

def main():
    log("Starting HTTP Server Automated Test Suite")
    
    # Test Threadpool mode
    test_server_mode("threadpool", TEST_PORT_TP)

    # Test Eventloop mode
    test_server_mode("eventloop", TEST_PORT_EL)

    print("==================================================")
    print("ALL 18 TEST REQUIREMENTS PASSED ON BOTH MODES!")
    print("==================================================")

if __name__ == "__main__":
    main()
