#!/usr/bin/env python3
"""
HTTP/1.1 Server Benchmark Suite
Measures and compares performance between Thread-Pool and Event-Loop concurrency architectures.
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
BENCH_PORT_TP = 18090
BENCH_PORT_EL = 18091

NUM_CONCURRENT_CLIENTS = 40
REQUESTS_PER_CLIENT = 25
TOTAL_REQUESTS = NUM_CONCURRENT_CLIENTS * REQUESTS_PER_CLIENT

def single_request(port):
    req = b"GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    start = time.perf_counter()
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(4.0)
        s.connect(("127.0.0.1", port))
        s.sendall(req)
        resp = b""
        while True:
            chunk = s.recv(2048)
            if not chunk:
                break
            resp += chunk
        s.close()
        latency = (time.perf_counter() - start) * 1000.0  # ms
        is_success = b"200 OK" in resp
        return is_success, latency
    except Exception:
        latency = (time.perf_counter() - start) * 1000.0
        return False, latency

def client_worker(args):
    port, num_reqs = args
    results = []
    for _ in range(num_reqs):
        results.append(single_request(port))
    return results

def run_benchmark(mode_name, port, extra_args=[]):
    cmd = [SERVER_BIN, "--port", str(port), "--root", DOC_ROOT, "--mode", mode_name] + extra_args
    print(f"\n[BENCHMARK] Launching {mode_name.upper()} server on port {port}...")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.4)

    if proc.poll() is not None:
        stdout, stderr = proc.communicate()
        print(f"Server failed to start:\n{stderr.decode()}")
        return None

    try:
        # Warm-up phase
        for _ in range(10):
            single_request(port)

        print(f"[BENCHMARK] Executing {TOTAL_REQUESTS} requests ({NUM_CONCURRENT_CLIENTS} concurrent clients x {REQUESTS_PER_CLIENT} reqs)...")
        bench_start = time.perf_counter()

        tasks = [(port, REQUESTS_PER_CLIENT) for _ in range(NUM_CONCURRENT_CLIENTS)]
        all_results = []
        with ThreadPoolExecutor(max_workers=NUM_CONCURRENT_CLIENTS) as executor:
            batch_results = list(executor.map(client_worker, tasks))
            for b in batch_results:
                all_results.extend(b)

        total_duration = time.perf_counter() - bench_start

        success_count = sum(1 for ok, _ in all_results if ok)
        fail_count = len(all_results) - success_count
        latencies = sorted([lat for _, lat in all_results])

        rps = len(all_results) / total_duration if total_duration > 0 else 0
        avg_lat = sum(latencies) / len(latencies) if latencies else 0
        p50 = latencies[int(len(latencies) * 0.50)] if latencies else 0
        p95 = latencies[int(len(latencies) * 0.95)] if latencies else 0
        p99 = latencies[int(len(latencies) * 0.99)] if latencies else 0

        stats = {
            "mode": mode_name,
            "total_requests": len(all_results),
            "successful_requests": success_count,
            "failed_requests": fail_count,
            "duration_sec": total_duration,
            "rps": rps,
            "avg_latency_ms": avg_lat,
            "p50_ms": p50,
            "p95_ms": p95,
            "p99_ms": p99
        }
        return stats

    finally:
        proc.send_signal(signal.SIGINT)
        proc.communicate(timeout=3.0)

def main():
    print("================================================================")
    print("      HTTP/1.1 SERVER BENCHMARK: THREAD-POOL vs EVENT-LOOP     ")
    print("================================================================")

    tp_stats = run_benchmark("threadpool", BENCH_PORT_TP, ["--threads", "8"])
    el_stats = run_benchmark("eventloop", BENCH_PORT_EL)

    if not tp_stats or not el_stats:
        print("Benchmark failed.")
        sys.exit(1)

    print("\n" + "=" * 64)
    print(f"{'Metric':<30} | {'Thread-Pool (8 threads)':<16} | {'Event-Loop (poll)':<16}")
    print("-" * 64)
    print(f"{'Total Requests':<30} | {tp_stats['total_requests']:<16} | {el_stats['total_requests']:<16}")
    print(f"{'Successful Requests':<30} | {tp_stats['successful_requests']:<16} | {el_stats['successful_requests']:<16}")
    print(f"{'Failed Requests':<30} | {tp_stats['failed_requests']:<16} | {el_stats['failed_requests']:<16}")
    print(f"{'Test Duration (s)':<30} | {tp_stats['duration_sec']:<16.3f} | {el_stats['duration_sec']:<16.3f}")
    print(f"{'Requests Per Second (RPS)':<30} | {tp_stats['rps']:<16.2f} | {el_stats['rps']:<16.2f}")
    print(f"{'Average Latency (ms)':<30} | {tp_stats['avg_latency_ms']:<16.2f} | {el_stats['avg_latency_ms']:<16.2f}")
    print(f"{'P50 Latency (ms)':<30} | {tp_stats['p50_ms']:<16.2f} | {el_stats['p50_ms']:<16.2f}")
    print(f"{'P95 Latency (ms)':<30} | {tp_stats['p95_ms']:<16.2f} | {el_stats['p95_ms']:<16.2f}")
    print(f"{'P99 Latency (ms)':<30} | {tp_stats['p99_ms']:<16.2f} | {el_stats['p99_ms']:<16.2f}")
    print("=" * 64)

    # Save results to benchmarks/benchmark_results.txt
    out_file = "benchmarks/benchmark_results.txt"
    with open(out_file, "w") as f:
        f.write("HTTP/1.1 Server Benchmark Results\n")
        f.write("=" * 64 + "\n")
        f.write(f"{'Metric':<30} | {'Thread-Pool (8 threads)':<16} | {'Event-Loop (poll)':<16}\n")
        f.write("-" * 64 + "\n")
        f.write(f"{'Total Requests':<30} | {tp_stats['total_requests']:<16} | {el_stats['total_requests']:<16}\n")
        f.write(f"{'Successful Requests':<30} | {tp_stats['successful_requests']:<16} | {el_stats['successful_requests']:<16}\n")
        f.write(f"{'Failed Requests':<30} | {tp_stats['failed_requests']:<16} | {el_stats['failed_requests']:<16}\n")
        f.write(f"{'Test Duration (s)':<30} | {tp_stats['duration_sec']:<16.3f} | {el_stats['duration_sec']:<16.3f}\n")
        f.write(f"{'Requests Per Second (RPS)':<30} | {tp_stats['rps']:<16.2f} | {el_stats['rps']:<16.2f}\n")
        f.write(f"{'Average Latency (ms)':<30} | {tp_stats['avg_latency_ms']:<16.2f} | {el_stats['avg_latency_ms']:<16.2f}\n")
        f.write(f"{'P50 Latency (ms)':<30} | {tp_stats['p50_ms']:<16.2f} | {el_stats['p50_ms']:<16.2f}\n")
        f.write(f"{'P95 Latency (ms)':<30} | {tp_stats['p95_ms']:<16.2f} | {el_stats['p95_ms']:<16.2f}\n")
        f.write(f"{'P99 Latency (ms)':<30} | {tp_stats['p99_ms']:<16.2f} | {el_stats['p99_ms']:<16.2f}\n")
        f.write("=" * 64 + "\n")
    print(f"\nResults saved to {out_file}\n")

if __name__ == "__main__":
    main()
