# Forward Proxy and Cache Tests

Maintainers: Sipra & Akshat

## Purpose

Automated test suite verifying the HTTP forward proxy and caching components against all assignment requirements.

## Test Categories & Status

1. **Request forwarding:** [PASS] Valid absolute-form HTTP requests (`http://host:port/path`) forwarded to origin server.
2. **Response relay:** [PASS] Status line, headers, and body returned accurately to clients.
3. **Connection failures:** [PASS] Unreachable origin servers correctly return `502 Bad Gateway`.
4. **Cache behavior:** [PASS] First request records `X-Cache: MISS`; second request served from cache with `X-Cache: HIT` and accurate `Age` header.
5. **LRU eviction:** [PASS] Evicts least recently used items when memory capacity or entry limit is reached.
6. **Cache directives:** [PASS] Respects `Cache-Control` (`no-store`, `private`, `max-age`, `no-cache`).
7. **Revalidation:** [PASS] Sends `If-None-Match` and `If-Modified-Since` on stale cache entries; handles `304 Not Modified`.
8. **Method validation & error handling:** [PASS] Malformed requests return `400 Bad Request`; unsupported methods return `405 Method Not Allowed`.
9. **Integration & Concurrency:** [PASS] Concurrently tested with origin HTTP server under multi-client loads.

## Running Tests

```bash
make test_proxy
# OR
python3 tests/proxy/test_proxy.py
```
