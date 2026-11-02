#!/bin/bash
# ==============================================================================
# Forward Proxy & Validation Benchmark Runner (No Python)
# Contributed by Sipra
# ==============================================================================

set -e

ORIGIN_PORT=18090
PROXY_PORT=18091
ORIGIN_BIN="./tests/proxy/mock_origin"
PROXY_BIN="./proxy"
BENCH_BIN="./benchmarks/benchmark_validation"

cleanup() {
    if [ -n "$PROXY_PID" ] && kill -0 "$PROXY_PID" 2>/dev/null; then
        kill -INT "$PROXY_PID" 2>/dev/null || kill -9 "$PROXY_PID" 2>/dev/null
    fi
    if [ -n "$ORIGIN_PID" ] && kill -0 "$ORIGIN_PID" 2>/dev/null; then
        kill -INT "$ORIGIN_PID" 2>/dev/null || kill -9 "$ORIGIN_PID" 2>/dev/null
    fi
    rm -f "$ORIGIN_BIN" "$BENCH_BIN"
}
trap cleanup EXIT INT TERM

gcc -Wall -Wextra -std=c11 -O2 tests/proxy/mock_origin.c -o "$ORIGIN_BIN"
gcc -Wall -Wextra -std=c11 -O2 -pthread benchmarks/benchmark_validation.c -o "$BENCH_BIN"
make proxy >/dev/null

"$ORIGIN_BIN" "$ORIGIN_PORT" &
ORIGIN_PID=$!
sleep 0.3

"$PROXY_BIN" --port "$PROXY_PORT" --cache-size 10 --threads 8 >/dev/null 2>&1 &
PROXY_PID=$!
sleep 0.4

"$BENCH_BIN" --proxy-port "$PROXY_PORT" --origin-port "$ORIGIN_PORT" --requests 500 --threads 10
