#!/usr/bin/env python3
"""
Comprehensive Forward Proxy & LRU Cache Test Suite
Tests all 9 planned test categories against assignment specifications.
"""

import socket
import subprocess
import time
import sys
import os
import signal

SERVER_BIN = "./server"
PROXY_BIN = "./proxy"
DOC_ROOT = "./www"

ORIGIN_PORT = 18080
PROXY_PORT = 18085

class TestFailure(Exception):
    pass

def log(msg):
    print(f"[PROXY TEST] {msg}", flush=True)

def raw_http_exchange(port, request_bytes, timeout=4.0):
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

def run_all_proxy_tests():
    log("==================================================")
    log("Running Full Forward Proxy & LRU Cache Test Battery")
    log("==================================================")

    # Test 1: Request forwarding with absolute-form URI
    log("Test 1: Absolute-form URI forwarding (GET http://localhost:18080/index.html)...")
    req = f"GET http://127.0.0.1:{ORIGIN_PORT}/index.html HTTP/1.1\r\nHost: 127.0.0.1:{ORIGIN_PORT}\r\nConnection: close\r\n\r\n".encode()
    res = raw_http_exchange(PROXY_PORT, req)
    code, hdrs, body = parse_http_response(res)
    if code != 200:
        raise TestFailure(f"Expected 200 OK from forwarded request, got {code}")
    if b"HTTP/1.1 Server is Working!" not in body:
        raise TestFailure("Body mismatch in forwarded response")
    if "via" not in hdrs:
        raise TestFailure("Expected 'Via' header in proxy response")
    log("  PASS: Absolute-form request correctly forwarded and answered with 200 OK")

    # Test 2: Response relay headers and Content-Length accuracy
    log("Test 2: Response relay verification (headers, Content-Type, Content-Length)...")
    req = f"GET http://127.0.0.1:{ORIGIN_PORT}/sample.json HTTP/1.1\r\nHost: 127.0.0.1:{ORIGIN_PORT}\r\nConnection: close\r\n\r\n".encode()
    res = raw_http_exchange(PROXY_PORT, req)
    code, hdrs, body = parse_http_response(res)
    expected_size = os.path.getsize("www/sample.json")
    if code != 200:
        raise TestFailure(f"Expected 200 OK, got {code}")
    if "application/json" not in hdrs.get("content-type", ""):
        raise TestFailure(f"Expected application/json, got {hdrs.get('content-type')}")
    if int(hdrs.get("content-length", -1)) != expected_size:
        raise TestFailure(f"Content-Length mismatch: expected {expected_size}, got {hdrs.get('content-length')}")
    if len(body) != expected_size:
        raise TestFailure("Body byte count mismatch")
    log("  PASS: Response relayed with exact headers and length")

    # Test 3: Unreachable origin handling (502 Bad Gateway)
    log("Test 3: Unreachable origin server (502 Bad Gateway)...")
    unreachable_port = 19999
    req = f"GET http://127.0.0.1:{unreachable_port}/test.txt HTTP/1.1\r\nHost: 127.0.0.1:{unreachable_port}\r\nConnection: close\r\n\r\n".encode()
    res = raw_http_exchange(PROXY_PORT, req)
    code, hdrs, body = parse_http_response(res)
    if code != 502:
        raise TestFailure(f"Expected 502 Bad Gateway for unreachable upstream, got {code}")
    log("  PASS: Connection failure correctly returned 502 Bad Gateway")

    # Test 4: Cache MISS followed by Cache HIT
    log("Test 4: Cache behavior (Cache MISS followed by Cache HIT)...")
    target_url = f"http://127.0.0.1:{ORIGIN_PORT}/test.txt"
    req = f"GET {target_url} HTTP/1.1\r\nHost: 127.0.0.1:{ORIGIN_PORT}\r\nConnection: close\r\n\r\n".encode()

    # First request: Cache MISS
    res1 = raw_http_exchange(PROXY_PORT, req)
    code1, hdrs1, body1 = parse_http_response(res1)
    if code1 != 200:
        raise TestFailure(f"Req 1 expected 200 OK, got {code1}")
    if hdrs1.get("x-cache", "").upper() != "MISS":
        raise TestFailure(f"Expected X-Cache: MISS on first request, got {hdrs1.get('x-cache')}")

    # Second request: Cache HIT
    res2 = raw_http_exchange(PROXY_PORT, req)
    code2, hdrs2, body2 = parse_http_response(res2)
    if code2 != 200:
        raise TestFailure(f"Req 2 expected 200 OK, got {code2}")
    if hdrs2.get("x-cache", "").upper() != "HIT":
        raise TestFailure(f"Expected X-Cache: HIT on second request, got {hdrs2.get('x-cache')}")
    if body1 != body2:
        raise TestFailure("Cached body does not match original body")
    if "age" not in hdrs2:
        raise TestFailure("Expected Age header in cached HIT response")
    log("  PASS: First request recorded MISS, second request served from cache as HIT")

    # Test 5: HEAD request handling through proxy
    log("Test 5: HEAD request handling through proxy...")
    req = f"HEAD http://127.0.0.1:{ORIGIN_PORT}/index.html HTTP/1.1\r\nHost: 127.0.0.1:{ORIGIN_PORT}\r\nConnection: close\r\n\r\n".encode()
    res = raw_http_exchange(PROXY_PORT, req)
    code, hdrs, body = parse_http_response(res)
    if code != 200:
        raise TestFailure(f"HEAD expected 200 OK, got {code}")
    if len(body) > 0:
        raise TestFailure(f"HEAD must return empty body, received {len(body)} bytes")
    if "content-length" not in hdrs:
        raise TestFailure("HEAD response missing Content-Length header")
    log("  PASS: HEAD request returned headers with zero body bytes")

    # Test 6: Origin-form URI with Host header forwarding
    log("Test 6: Origin-form URI with Host header (GET /style.css + Host)...")
    req = f"GET /style.css HTTP/1.1\r\nHost: 127.0.0.1:{ORIGIN_PORT}\r\nConnection: close\r\n\r\n".encode()
    res = raw_http_exchange(PROXY_PORT, req)
    code, hdrs, body = parse_http_response(res)
    if code != 200:
        raise TestFailure(f"Expected 200 OK, got {code}")
    if "text/css" not in hdrs.get("content-type", ""):
        raise TestFailure(f"Expected text/css, got {hdrs.get('content-type')}")
    log("  PASS: Origin-form request resolved and forwarded successfully")

    # Test 7: Method validation (POST -> 405 Method Not Allowed)
    log("Test 7: Unsupported method (POST -> 405 Method Not Allowed)...")
    req = f"POST http://127.0.0.1:{ORIGIN_PORT}/index.html HTTP/1.1\r\nHost: 127.0.0.1:{ORIGIN_PORT}\r\nConnection: close\r\n\r\n".encode()
    res = raw_http_exchange(PROXY_PORT, req)
    code, hdrs, body = parse_http_response(res)
    if code != 405:
        raise TestFailure(f"Expected 405 Method Not Allowed, got {code}")
    log("  PASS: POST method rejected by proxy with 405")

    # Test 8: Malformed request syntax (400 Bad Request)
    log("Test 8: Malformed request syntax (400 Bad Request)...")
    req = b"MALFORMED_PROXY_REQUEST\r\n\r\n"
    res = raw_http_exchange(PROXY_PORT, req)
    code, hdrs, body = parse_http_response(res)
    if code != 400:
        raise TestFailure(f"Expected 400 Bad Request, got {code}")
    log("  PASS: Malformed syntax rejected with 400 Bad Request")

    # Test 9: Concurrent proxy clients
    log("Test 9: Concurrent client requests through proxy...")
    from concurrent.futures import ThreadPoolExecutor

    def worker_req(worker_id):
        r = f"GET http://127.0.0.1:{ORIGIN_PORT}/index.html HTTP/1.1\r\nHost: 127.0.0.1:{ORIGIN_PORT}\r\nConnection: close\r\n\r\n".encode()
        res = raw_http_exchange(PROXY_PORT, r)
        c, _, _ = parse_http_response(res)
        return c == 200

    with ThreadPoolExecutor(max_workers=15) as executor:
        results = list(executor.map(worker_req, range(30)))
    success = sum(1 for ok in results if ok)
    if success != 30:
        raise TestFailure(f"Only {success}/30 concurrent requests succeeded")
    log(f"  PASS: 30 concurrent proxy requests completed successfully ({success}/30)")

    log("\nALL PROXY & CACHE TESTS PASSED SUCCESSFULLY!\n")

def main():
    log("Starting Forward Proxy Test Orchestrator")

    # 1. Start Origin HTTP Server
    server_cmd = [SERVER_BIN, "--port", str(ORIGIN_PORT), "--root", DOC_ROOT, "--mode", "threadpool"]
    log(f"Starting origin HTTP server: {' '.join(server_cmd)}")
    server_proc = subprocess.Popen(server_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.3)

    if server_proc.poll() is not None:
        stdout, stderr = server_proc.communicate()
        print(f"Origin server failed to start:\n{stderr.decode()}")
        sys.exit(1)

    # 2. Start Forward Proxy
    proxy_cmd = [PROXY_BIN, "--port", str(PROXY_PORT), "--cache-size", "10", "--threads", "8"]
    log(f"Starting forward caching proxy: {' '.join(proxy_cmd)}")
    proxy_proc = subprocess.Popen(proxy_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.3)

    if proxy_proc.poll() is not None:
        server_proc.kill()
        stdout, stderr = proxy_proc.communicate()
        print(f"Proxy failed to start:\n{stderr.decode()}")
        sys.exit(1)

    try:
        run_all_proxy_tests()
    finally:
        log("Tearing down test processes...")
        proxy_proc.send_signal(signal.SIGINT)
        server_proc.send_signal(signal.SIGINT)
        try:
            proxy_proc.communicate(timeout=2.0)
            server_proc.communicate(timeout=2.0)
        except subprocess.TimeoutExpired:
            proxy_proc.kill()
            server_proc.kill()
        log("Test teardown complete.")

if __name__ == "__main__":
    main()
