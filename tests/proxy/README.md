# Forward Proxy and Cache Tests

## Purpose

This directory will contain tests for the HTTP forward proxy and caching components.

## Planned Test Categories

1. **Request forwarding:** valid absolute-form HTTP requests and correct forwarding to origin servers.
2. **Response relay:** status line, headers, and body returned correctly to clients.
3. **Connection failures:** handling unavailable or unreachable origin servers.
4. **Cache behavior:** cache hits, misses, and stale entries.
5. **LRU eviction:** least-recently-used entry is evicted when capacity is exceeded.
6. **Cache directives:** respect Cache-Control rules, including private and no-store responses.
7. **Revalidation:** ETag, If-Modified-Since, and conditional GET behavior.
8. **Error handling:** malformed requests and invalid upstream responses.
9. **Integration:** compatibility with the shared HTTP interfaces agreed upon by both team members.

## Test Status

Tests will be marked complete only after they are implemented, executed, and their results recorded.

## Constraints

The proxy must use TCP sockets and implement HTTP handling directly. Prohibited HTTP implementation libraries must not be used.
