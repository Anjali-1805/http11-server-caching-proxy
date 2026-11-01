# Forward Proxy Module

Maintainers: Sipra & Akshat

## Responsibilities
- [x] Forward HTTP requests to upstream origin servers via raw TCP sockets.
- [x] Support both absolute-form URI (`http://host:port/path`) and origin-form URI with Host header.
- [x] Relay upstream responses accurately to clients with status lines, headers, and body.
- [x] Strip hop-by-hop headers (`Proxy-Connection`, `Connection`, `Keep-Alive`, `Upgrade`) before upstream relay.
- [x] Integrated LRU cache engine for fast in-memory response retrieval.
- [x] Handle HTTP caching directives (`Cache-Control: no-store`, `private`, `max-age`, `no-cache`).
- [x] Support conditional revalidation via `If-None-Match` (ETag) and `If-Modified-Since` (Last-Modified).
- [x] Emit standard proxy diagnostic headers: `Via: 1.1 caching-proxy`, `X-Cache: HIT / MISS / REVALIDATED`, and `Age`.
- [x] Multithreaded client concurrency using synchronized worker thread pools.
- [x] Graceful shutdown on `SIGINT` / `SIGTERM` with clean resource teardown.

## Development Status
Implementation complete, verified, and tested against all assignment requirements.

## Architecture & Workflow
1. Client connects to proxy listening socket (`--port 8081`).
2. Worker thread parses incoming HTTP/1.1 request.
3. Proxy queries LRU Cache:
   - **Fresh Cache HIT**: Directly relays cached response with `X-Cache: HIT`.
   - **Stale Entry / Revalidation**: Sends conditional GET (`If-None-Match`, `If-Modified-Since`) to upstream. On `304 Not Modified`, updates freshness and relays cached entry with `X-Cache: REVALIDATED`.
   - **Cache MISS**: Connects to upstream server, forwards request, receives response, stores eligible responses in cache, and relays to client with `X-Cache: MISS`.
