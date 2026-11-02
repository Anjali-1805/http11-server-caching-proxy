#!/usr/bin/env python3
"""
Forward Proxy & LRU Cache Benchmark Suite
Compares latency and throughput between Cache HIT and Cache MISS through the proxy.
"""

import socket
import subprocess
import time
import sys
import os
import signal
from concurrent.futures import ThreadPoolExecutor

SERVER_BIN = "./server"
PROXY_BIN = "./proxy"
DOC_ROOT = "./www"

ORIGIN_PORT = 18092
PROXY_PORT = 18093

NUM_REQUESTS = 500
CONCURRENCY = 20

def raw_request(port, uri):
    req = f"GET {uri} HTTP/1.1\r\nHost: 127.0.0.1:{ORIGIN_PORT}\r\nConnection: close\r\n\r\n".encode()
    start = time.perf_counter()
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3.0)
        s.connect(("127.0.0.1", port))
        s.sendall(req)
        resp = b""
        while True:
            chunk = s.recv(2048)
            if not chunk:
                break
            resp += chunk
        s.close()
        elapsed = (time.perf_counter() - start) * 1000.0
        return b"200 OK" in resp, elapsed
    except Exception:
        elapsed = (time.perf_counter() - start) * 1000.0
        return False, elapsed

def main():
    # 1. Start origin server
    server_proc = subprocess.Popen([SERVER_BIN, "--port", str(ORIGIN_PORT), "--root", DOC_ROOT, "--mode", "threadpool"],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.3)

    # 2. Start proxy
    proxy_proc = subprocess.Popen([PROXY_BIN, "--port", str(PROXY_PORT), "--cache-size", "10", "--threads", "8"],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.3)

    try:
        print("================================================================")
        print("    FORWARD PROXY & LRU CACHE BENCHMARK: CACHE MISS vs HIT      ")
        print("================================================================")

        # Warm-up 1 request to populate cache
        target_uri = f"http://127.0.0.1:{ORIGIN_PORT}/index.html"
        raw_request(PROXY_PORT, target_uri)

        # Benchmark: CACHE HITS (repeated requests for cached /index.html)
        print(f"\n[BENCHMARK] Executing {NUM_REQUESTS} Cache HIT requests (20 concurrent workers)...")
        hit_start = time.perf_counter()
        with ThreadPoolExecutor(max_workers=CONCURRENCY) as executor:
            hit_results = list(executor.map(lambda _: raw_request(PROXY_PORT, target_uri), range(NUM_REQUESTS)))
        hit_duration = time.perf_counter() - hit_start

        hit_success = sum(1 for ok, _ in hit_results if ok)
        hit_latencies = sorted([lat for _, lat in hit_results])
        hit_rps = NUM_REQUESTS / hit_duration if hit_duration > 0 else 0
        hit_avg = sum(hit_latencies) / len(hit_latencies) if hit_latencies else 0
        hit_p50 = hit_latencies[int(len(hit_latencies) * 0.50)] if hit_latencies else 0
        hit_p95 = hit_latencies[int(len(hit_latencies) * 0.95)] if hit_latencies else 0

        # Benchmark: CACHE MISSES (unique dummy queries preventing cache hits)
        print(f"[BENCHMARK] Executing {NUM_REQUESTS} Cache MISS requests (20 concurrent workers)...")
        miss_start = time.perf_counter()
        with ThreadPoolExecutor(max_workers=CONCURRENCY) as executor:
            miss_results = list(executor.map(lambda i: raw_request(PROXY_PORT, f"http://127.0.0.1:{ORIGIN_PORT}/index.html?t={i}"), range(NUM_REQUESTS)))
        miss_duration = time.perf_counter() - miss_start

        miss_success = sum(1 for ok, _ in miss_results if ok)
        miss_latencies = sorted([lat for _, lat in miss_results])
        miss_rps = NUM_REQUESTS / miss_duration if miss_duration > 0 else 0
        miss_avg = sum(miss_latencies) / len(miss_latencies) if miss_latencies else 0
        miss_p50 = miss_latencies[int(len(miss_latencies) * 0.50)] if miss_latencies else 0
        miss_p95 = miss_latencies[int(len(miss_latencies) * 0.95)] if miss_latencies else 0

        print("\n================================================================")
        print("Metric                         | Cache HIT (In-Memory)   | Cache MISS (Upstream Relay)")
        print("----------------------------------------------------------------")
        print(f"Total Requests                 | {NUM_REQUESTS:<23} | {NUM_REQUESTS}")
        print(f"Successful Requests            | {hit_success:<23} | {miss_success}")
        print(f"Test Duration (s)              | {hit_duration:<23.3f} | {miss_duration:.3f}")
        print(f"Requests Per Second (RPS)      | {hit_rps:<23.2f} | {miss_rps:.2f}")
        print(f"Average Latency (ms)           | {hit_avg:<23.2f} | {miss_avg:.2f}")
        print(f"P50 Latency (ms)               | {hit_p50:<23.2f} | {miss_p50:.2f}")
        print(f"P95 Latency (ms)               | {hit_p95:<23.2f} | {miss_p95:.2f}")
        print("================================================================")

        speedup = hit_rps / miss_rps if miss_rps > 0 else 1.0
        print(f"Cache Performance Improvement: {speedup:.2f}x Throughput Increase\n")

    finally:
        proxy_proc.send_signal(signal.SIGINT)
        server_proc.send_signal(signal.SIGINT)
        try:
            proxy_proc.communicate(timeout=2.0)
            server_proc.communicate(timeout=2.0)
        except Exception:
            proxy_proc.kill()
            server_proc.kill()

if __name__ == "__main__":
    main()
