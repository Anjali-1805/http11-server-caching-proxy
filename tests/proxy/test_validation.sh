#!/bin/bash
# ==============================================================================
# HTTP/1.1 Forward Proxy Validation & Revalidation Test Suite (No Python)
# Contributed by Sipra
# Covers all 15 assigned test categories using bash and curl.
# ==============================================================================

set -e

ORIGIN_PORT=18090
PROXY_PORT=18091
ORIGIN_BIN="./tests/proxy/mock_origin"
PROXY_BIN="./proxy"

PASS_COUNT=0
FAIL_COUNT=0

log() {
    echo -e "[SIPRA-TEST] $1"
}

pass() {
    echo -e "  \033[32mPASS:\033[0m $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    echo -e "  \033[31mFAIL:\033[0m $1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

cleanup() {
    log "Cleaning up test processes..."
    if [ -n "$PROXY_PID" ] && kill -0 "$PROXY_PID" 2>/dev/null; then
        kill -INT "$PROXY_PID" 2>/dev/null || kill -9 "$PROXY_PID" 2>/dev/null
    fi
    if [ -n "$ORIGIN_PID" ] && kill -0 "$ORIGIN_PID" 2>/dev/null; then
        kill -INT "$ORIGIN_PID" 2>/dev/null || kill -9 "$ORIGIN_PID" 2>/dev/null
    fi
    rm -f "$ORIGIN_BIN"
}
trap cleanup EXIT INT TERM

log "===================================================================="
log " Compiling Mock Origin & Building Proxy"
log "===================================================================="
gcc -Wall -Wextra -std=c11 -O2 tests/proxy/mock_origin.c -o "$ORIGIN_BIN"
make proxy >/dev/null

log "Starting Mock Origin Server on port $ORIGIN_PORT..."
"$ORIGIN_BIN" "$ORIGIN_PORT" &
ORIGIN_PID=$!
sleep 0.3

log "Starting Caching Proxy on port $PROXY_PORT..."
"$PROXY_BIN" --port "$PROXY_PORT" --cache-size 10 --threads 4 >/dev/null 2>&1 &
PROXY_PID=$!
sleep 0.4

TARGET_BASE="http://127.0.0.1:${ORIGIN_PORT}"

log "===================================================================="
log " Executing 15 Validation & Revalidation Test Scenarios"
log "===================================================================="

# ------------------------------------------------------------------------------
# Test 1: First request resulting in cache miss
# ------------------------------------------------------------------------------
log "Test 1: First request resulting in cache miss..."
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/etag-resource" -H "Host: 127.0.0.1:${ORIGIN_PORT}")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')
XCACHE=$(echo "$RESP" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')

if [ "$CODE" = "200" ] && [ "$XCACHE" = "MISS" ]; then
    pass "Initial request yielded 200 OK and X-Cache: MISS"
else
    fail "Expected 200 OK and MISS, got $CODE, X-Cache: $XCACHE"
fi

# ------------------------------------------------------------------------------
# Test 2: Subsequent cache hit
# ------------------------------------------------------------------------------
log "Test 2: Subsequent cache hit..."
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/etag-resource" -H "Host: 127.0.0.1:${ORIGIN_PORT}")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')
XCACHE=$(echo "$RESP" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')

if [ "$CODE" = "200" ] && [ "$XCACHE" = "HIT" ]; then
    pass "Second request served from cache: 200 OK and X-Cache: HIT"
else
    fail "Expected 200 OK and HIT, got $CODE, X-Cache: $XCACHE"
fi

# ------------------------------------------------------------------------------
# Test 3: ETag received from origin
# ------------------------------------------------------------------------------
log "Test 3: ETag received from origin..."
ETAG=$(echo "$RESP" | grep -i "^ETag:" | awk '{print $2}' | tr -d '\r')
if [ -n "$ETAG" ] && [ "$ETAG" = '"etag-v1"' ]; then
    pass "ETag header correctly captured and returned: $ETAG"
else
    fail "Expected ETag '\"etag-v1\"', got '$ETAG'"
fi

# ------------------------------------------------------------------------------
# Test 4: If-None-Match validation by client (Cache HIT -> 304)
# ------------------------------------------------------------------------------
log "Test 4: Client If-None-Match conditional GET on fresh cache..."
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/etag-resource" \
       -H "Host: 127.0.0.1:${ORIGIN_PORT}" \
       -H "If-None-Match: \"etag-v1\"")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')
if [ "$CODE" = "304" ]; then
    pass "Proxy directly answered client with 304 Not Modified"
else
    fail "Expected 304 Not Modified, got $CODE"
fi

# ------------------------------------------------------------------------------
# Test 5 & 11: Stale cache revalidation with origin (max-age=1 expired)
# ------------------------------------------------------------------------------
log "Test 5 & 11: Stale cache revalidation (waiting 1.2s for max-age=1 expiry)..."
sleep 1.2
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/etag-resource" -H "Host: 127.0.0.1:${ORIGIN_PORT}")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')
XCACHE=$(echo "$RESP" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')

if [ "$CODE" = "200" ] && [ "$XCACHE" = "REVALIDATED" ]; then
    pass "Stale entry conditionally revalidated with origin: X-Cache: REVALIDATED"
else
    fail "Expected 200 OK and REVALIDATED, got $CODE, X-Cache: $XCACHE"
fi

# ------------------------------------------------------------------------------
# Test 6: Changed ETag on origin (resource updated)
# ------------------------------------------------------------------------------
log "Test 6 & 9: Changed ETag and modified resource on origin..."
# Update origin ETag to v2
curl -s "http://127.0.0.1:${ORIGIN_PORT}/etag-update" >/dev/null
sleep 1.2 # ensure cache entry is stale

RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/etag-resource" -H "Host: 127.0.0.1:${ORIGIN_PORT}")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')
XCACHE=$(echo "$RESP" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')
NEW_ETAG=$(echo "$RESP" | grep -i "^ETag:" | awk '{print $2}' | tr -d '\r')

if [ "$CODE" = "200" ] && [ "$NEW_ETAG" = '"etag-v2"' ]; then
    pass "Origin returned updated resource with new ETag: $NEW_ETAG"
else
    fail "Expected 200 OK with ETag '\"etag-v2\"', got $CODE, $NEW_ETAG"
fi

# ------------------------------------------------------------------------------
# Test 7: Last-Modified response header
# ------------------------------------------------------------------------------
log "Test 7: Last-Modified response header..."
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/lastmod-resource" -H "Host: 127.0.0.1:${ORIGIN_PORT}")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')
LM=$(echo "$RESP" | grep -i "^Last-Modified:" | cut -d':' -f2- | tr -d '\r' | sed 's/^ *//')

if [ "$CODE" = "200" ] && [ -n "$LM" ]; then
    pass "Received Last-Modified header from origin: $LM"
else
    fail "Expected Last-Modified header, got $CODE"
fi

# ------------------------------------------------------------------------------
# Test 8: If-Modified-Since validation
# ------------------------------------------------------------------------------
log "Test 8: If-Modified-Since validation..."
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/lastmod-resource" \
       -H "Host: 127.0.0.1:${ORIGIN_PORT}" \
       -H "If-Modified-Since: $LM")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')

if [ "$CODE" = "304" ]; then
    pass "Client conditional request with IMS returned 304 Not Modified"
else
    fail "Expected 304 for matching If-Modified-Since, got $CODE"
fi

# ------------------------------------------------------------------------------
# Test 10: Unmodified resource revalidation
# ------------------------------------------------------------------------------
log "Test 10: Unmodified resource revalidation..."
sleep 1.2 # expire cache
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/lastmod-resource" -H "Host: 127.0.0.1:${ORIGIN_PORT}")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')
XCACHE=$(echo "$RESP" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')

if [ "$CODE" = "200" ] && [ "$XCACHE" = "REVALIDATED" ]; then
    pass "Unmodified resource successfully verified with origin (REVALIDATED)"
else
    fail "Expected 200 REVALIDATED, got $CODE, X-Cache: $XCACHE"
fi

# ------------------------------------------------------------------------------
# Test 12: Correct response body after revalidation
# ------------------------------------------------------------------------------
log "Test 12: Correct response body preserved after revalidation..."
BODY=$(echo "$RESP" | sed '1,/^\r\{0,1\}$/d')
if echo "$BODY" | grep -q "Last-Modified representation"; then
    pass "Cached payload perfectly preserved during 304 revalidation exchange"
else
    fail "Cached body corrupted or missing after revalidation"
fi

# ------------------------------------------------------------------------------
# Test 13: Cache metadata updates (Age header reset)
# ------------------------------------------------------------------------------
log "Test 13: Cache metadata updates (Age header evaluation)..."
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/etag-resource" -H "Host: 127.0.0.1:${ORIGIN_PORT}")
AGE=$(echo "$RESP" | grep -i "^Age:" | awk '{print $2}' | tr -d '\r')

if [ -n "$AGE" ]; then
    pass "Cache metadata includes valid Age header: $AGE seconds"
else
    fail "Missing Age header in cached response"
fi

# ------------------------------------------------------------------------------
# Test 14: Missing validators (stale cache with no ETag or Last-Modified)
# ------------------------------------------------------------------------------
log "Test 14: Missing validators handling..."
# First request: MISS
curl -s "http://127.0.0.1:${PROXY_PORT}/novalidator-resource" -H "Host: 127.0.0.1:${ORIGIN_PORT}" >/dev/null
sleep 1.2 # expire max-age=1

# Second request on stale entry with NO validators -> proxy must do unconditional GET
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/novalidator-resource" -H "Host: 127.0.0.1:${ORIGIN_PORT}")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')
XCACHE=$(echo "$RESP" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')

if [ "$CODE" = "200" ] && [ "$XCACHE" = "MISS" ]; then
    pass "Stale entry without validators safely re-fetched via unconditional GET (MISS)"
else
    fail "Expected 200 OK and MISS for missing validators, got $CODE, $XCACHE"
fi

# ------------------------------------------------------------------------------
# Test 15: Origin server errors (unreachable / 502 Bad Gateway)
# ------------------------------------------------------------------------------
log "Test 15: Upstream error handling (502 Bad Gateway)..."
UNREACHABLE_PORT=19998
RESP=$(curl -s -i "http://127.0.0.1:${PROXY_PORT}/test.html" -H "Host: 127.0.0.1:${UNREACHABLE_PORT}")
CODE=$(echo "$RESP" | head -n 1 | awk '{print $2}')

if [ "$CODE" = "502" ]; then
    pass "Unreachable upstream server cleanly handled with 502 Bad Gateway"
else
    fail "Expected 502 Bad Gateway, got $CODE"
fi

log "===================================================================="
log " Test Summary: $PASS_COUNT Passed, $FAIL_COUNT Failed"
log "===================================================================="

if [ "$FAIL_COUNT" -eq 0 ]; then
    echo -e "\033[32mALL 15 PROXY VALIDATION TESTS PASSED PERFECTLY!\033[0m"
    exit 0
else
    echo -e "\033[31mSOME TESTS FAILED!\033[0m"
    exit 1
fi
