# HTTP/1.1 Server and Caching Proxy

## Project Overview
This project aims to implement an HTTP/1.1 server and a forward proxy with an LRU cache using TCP sockets.

## Team Members

### Anjali — HTTP Server
- TCP socket setup and client connections
- HTTP request parsing and response generation
- GET and HEAD methods
- Persistent connections and static file serving
- Thread-pool concurrency
- Event-loop concurrency
- Server testing and benchmarks

### Sipra & Akshat — Forward Proxy and Caching
- Upstream TCP connections and HTTP forwarding
- LRU cache implementation
- Cache-Control handling
- ETag and Last-Modified validation
- Conditional GET and cache revalidation
- Proxy testing and benchmarks

## Planned Features
- HTTP/1.1 request parsing and response handling
- Static file serving
- Persistent connections
- Thread-pool and event-loop concurrency models
- Forward proxy with an LRU cache
- Cache validation and conditional requests
- Performance testing and benchmarking

## Technology Stack
- Language: C (C11 standard)
- Environment: Windows with WSL (Ubuntu 24.04)
- Compiler: GCC (`-Wall -Wextra -Wpedantic -std=c11 -g -pthread`)
- Networking: Raw POSIX TCP sockets
- Version control: Git and GitHub

## Project Status
- **HTTP Server**: Complete and verified (both Thread-Pool and Event-Loop concurrency models fully implemented and tested).
- **Forward Proxy and Cache**: Complete and verified (LRU caching, Cache-Control handling, conditional revalidation, and multithreading implemented and tested on `feature/proxy-cache`).

## AI Use
AI assistance is documented in `AI-USE.md` in accordance with the assignment rules.

---

### Anjali — HTTP Server Implementation

#### 1. Features Implemented

- **TCP Sockets & Connection Lifecycle**:
  - Raw POSIX socket creation via `socket(AF_INET, SOCK_STREAM, 0)`.
  - Address reuse configuration via `setsockopt(SO_REUSEADDR)`.
  - Safe IP/port binding with `bind()` and backlog queue handling with `listen()`.
  - Client accept loop with logging, socket timeout configuration, and graceful teardown.
  - Signal handling (`SIGINT` and `SIGTERM`) for zero-loss server shutdown.
- **HTTP/1.1 Request Parser**:
  - Request line parsing: extraction and validation of method, URI, and HTTP version.
  - Supported methods: `GET` and `HEAD`.
  - Rejected methods: `POST`, `PUT`, `DELETE`, `CONNECT`, `OPTIONS`, `TRACE`, `PATCH` return `405 Method Not Allowed` with `Allow: GET, HEAD`.
  - Unknown method tokens return `501 Not Implemented`.
  - Safe URL percent-decoding (`%20` $\rightarrow$ space, etc.) with null-byte (`%00`) injection rejection.
  - Query string stripping (`?param=value`) prior to static file lookup.
  - RFC 7230 compliance: mandatory `Host` header check for HTTP/1.1 (missing or duplicate Host returns `400 Bad Request`).
  - Request size boundaries: request lines $> 8192$ bytes return `414 URI Too Long`; headers $> 16384$ bytes or $> 64$ headers return `431 Request Header Fields Too Large`.
  - Incomplete TCP read handling: stream buffering until `\r\n\r\n` is encountered, supporting packet fragmentation.
- **HTTP/1.1 Response Generation**:
  - Accurate status line formatting with standard RFC reason phrases.
  - Headers: `Date` (RFC 1123/7231 GMT), `Server`, `Content-Type`, `Content-Length`, `Connection`, and `Allow` (for 405).
  - Rich HTML error pages for `400`, `403`, `404`, `405`, `414`, `431`, `500`, `501`, and `505`.
  - Proper handling of partial network writes via `send_all()` loops and non-blocking chunk tracking.
- **GET and HEAD Methods**:
  - `GET`: Reads and transmits static file contents with correct `Content-Length` and MIME type.
  - `HEAD`: Generates identical headers as `GET` but omits the response body entirely.
- **Static File Serving & Security**:
  - Configurable document root via `--root <path>` (defaults to `./www`).
  - Automatic canonical root path resolution via `realpath()`.
  - Path normalization: parses and eliminates `.` and `..` components before file resolution.
  - Directory traversal prevention: rejects all escapes outside the document root with `403 Forbidden` or `404 Not Found`.
  - Default index mapping: requests to `/` or directories serve `index.html`.
  - MIME type detection for HTML, CSS, JavaScript, TXT, PNG, JPG/JPEG, GIF, PDF, ICO, SVG, JSON, and XML.
- **Persistent Connections**:
  - HTTP/1.1 persistent connections by default (`Connection: keep-alive`).
  - Sequential request handling over a single TCP stream.
  - `Connection: close` header detection and socket shutdown.
  - Configurable idle read/write timeouts (5 seconds) to prevent resource hogging.
  - Maximum request cap per connection (100 requests) before clean disconnection.
- **Dual Concurrency Architectures**:
  - **Thread-Pool Model**: Worker threads pulling from a synchronized task queue with mutexes and condition variables.
  - **Event-Loop Model**: Single-threaded non-blocking I/O multiplexing using `poll()`, state machine per client connection, non-blocking reads/writes, and timeout management.
  - Selectable at runtime via `--mode threadpool` or `--mode eventloop`.

---

#### 2. Server Architecture

```
                       +-------------------------+
                       |    CLI & Configuration  |
                       |  (--port, --root, etc.) |
                       +------------+------------+
                                    |
                                    v
                       +-------------------------+
                       |  Listening TCP Socket   |
                       |     (bind & listen)     |
                       +------------+------------+
                                    |
            +-----------------------+-----------------------+
            |                                               |
            v                                               v
+-----------------------+                       +-----------------------+
|  Thread-Pool Mode     |                       |   Event-Loop Mode     |
| - Synchronized Queue  |                       | - Non-blocking poll() |
| - Mutex & Condvars    |                       | - Client State Machine|
| - N Worker Threads    |                       | - Non-blocking I/O    |
+-----------+-----------+                       +-----------+-----------+
            |                                               |
            +-----------------------+-----------------------+
                                    |
                                    v
                       +-------------------------+
                       |   HTTP Request Parser   |
                       | (http_parse_request)    |
                       +------------+------------+
                                    |
                                    v
                       +-------------------------+
                       |   Safe File Handler     |
                       | (file_handler_resolve)  |
                       | - Traversal defense     |
                       | - MIME type lookup      |
                       +------------+------------+
                                    |
                                    v
                       +-------------------------+
                       | Response Generator      |
                       | (http_response_format)  |
                       +-------------------------+
```

---

#### 3. TCP Socket Workflow

1. **Initialization**: Server creates an `AF_INET` streaming socket (`SOCK_STREAM`), sets `SO_REUSEADDR` to prevent bind delay on restarts, binds to `0.0.0.0:<port>`, and enters listening state with a backlog of 128.
2. **Accepting Connections**:
   - In **Thread-Pool Mode**: The main thread accepts clients and pushes `client_fd` into a bounded FIFO task queue protected by `pthread_mutex_t` and `pthread_cond_t`.
   - In **Event-Loop Mode**: Sockets are set to non-blocking (`O_NONBLOCK`). `poll()` monitors `listen_fd` for `POLLIN`, accepting connections in a loop until `EAGAIN` and adding client entries into the poll array.
3. **Graceful Teardown**: Upon receiving `SIGINT` or `SIGTERM`, `g_shutdown` is flagged. The listener stops accepting new connections, worker threads drain existing connections and join, open client sockets are closed, and resources are cleanly freed.

---

#### 4. Concurrency Models

##### A. Thread-Pool Model
- **Queue Synchronization**: Uses a thread-safe linked-list task queue. `cond_has_task` wakes idle workers when a new client arrives; `cond_has_space` throttles the accept loop if the queue reaches capacity.
- **Worker Lifecycle**: Each worker pops a client descriptor, configures `SO_RCVTIMEO` and `SO_SNDTIMEO`, and enters the HTTP request loop.
- **Persistent Processing**: Handles successive requests on the same socket until `Connection: close`, idle timeout, or client disconnection, then closes `client_fd` and waits for the next task.

##### B. Event-Loop Model
- **Non-blocking Multiplexing**: A single thread uses `poll()` to monitor all open connections simultaneously.
- **Client State Machine**:
  - `CONN_STATE_READING_REQUEST`: Buffers incoming stream data. When `\r\n\r\n` is detected, parses request and transitions to writing.
  - `CONN_STATE_WRITING_HEADERS`: Transmits response headers without blocking.
  - `CONN_STATE_WRITING_BODY_FILE`: Transmits static file chunks (8 KB) non-blockingly.
  - `CONN_STATE_WRITING_BODY_MEM`: Transmits generated error bodies non-blockingly.
- **Keep-Alive & Timeout Handling**: Once a response completes, if `keep_alive` is true, the state resets to reading on the same connection. If a connection remains idle beyond 5 seconds, it is automatically closed.

---

#### 5. Compilation Instructions

The project uses GCC with strict compiler warnings.

```bash
# Build the HTTP server executable
make

# Clean up build artifacts
make clean
```

The build produces both `./server` and `./http_server`.

---

#### 6. Execution Commands

```bash
# Run with Thread-Pool concurrency (default port 8080, document root ./www, 8 threads)
./http_server --port 8080 --root ./www --mode threadpool --threads 8

# Run with Event-Loop concurrency
./http_server --port 8080 --root ./www --mode eventloop

# Display CLI options
./http_server --help
```

---

#### 7. Testing Instructions

An automated test suite tests all 18 requirements against **both** concurrency modes:

```bash
# Run automated test suite
make test
# OR directly
python3 tests/server/test_suite.py
```

##### Automated Test Coverage:
1. Server startup and banner initialization.
2. TCP client connection acceptance and termination.
3. Valid `GET` request (`200 OK` with accurate file body).
4. Valid `HEAD` request (`200 OK` with accurate headers and zero body).
5. Malformed HTTP request syntax (`400 Bad Request`).
6. Missing `Host` header in HTTP/1.1 (`400 Bad Request`).
7. Nonexistent file request (`404 Not Found`).
8. Unsupported HTTP method `POST` (`405 Method Not Allowed` with `Allow` header).
9. Unknown HTTP method `CUSTOMMETHOD` (`501 Not Implemented`).
10. Accurate `Content-Length` and `Content-Type` for HTML, CSS, JS, TXT, JSON, and PDF files.
11. Persistent connections with multiple requests over a single TCP stream.
12. `Connection: close` behavior and server socket termination.
13. Directory traversal defense (`/../../etc/passwd` rejected with `403` or `404`).
14. Incomplete TCP read accumulation across packet boundaries.
15. URI length boundary validation (`414 URI Too Long`).
16. Header size boundary validation (`431 Request Header Fields Too Large`).
17. High concurrent load with simultaneous worker connections.
18. Graceful server shutdown on `SIGINT`.

---

#### 8. Performance Benchmarking

A dedicated benchmark script measures throughput, concurrency handling, and latency:

```bash
# Run benchmarks comparing Thread-Pool vs Event-Loop
make benchmark
# OR directly
python3 benchmarks/benchmark.py
```

##### Benchmark Results (Actual Measurements):

- **Environment**: Ubuntu 24.04 (WSL 2) on Windows, GCC 13.3.0, Intel Core i5.
- **Workload**: 1,000 total HTTP requests under 40 concurrent clients.

| Metric | Thread-Pool (8 threads) | Event-Loop (poll single-thread) |
| :--- | :--- | :--- |
| **Total Requests** | 1,000 | 1,000 |
| **Successful Requests (200 OK)** | 1,000 (100%) | 1,000 (100%) |
| **Failed Requests** | 0 (0%) | 0 (0%) |
| **Test Duration** | **0.668 s** | 2.286 s |
| **Requests Per Second (RPS)** | **1,497.08 req/sec** | 437.37 req/sec |
| **Average Latency** | **23.73 ms** | 88.83 ms |
| **P50 Latency** | **23.69 ms** | 89.18 ms |
| **P95 Latency** | **36.54 ms** | 117.15 ms |
| **P99 Latency** | **40.87 ms** | 124.98 ms |

*Analysis*: The Thread-Pool server achieves approximately $3.4\times$ higher throughput on multi-core hardware due to parallel blocking I/O across worker threads, while the Event-Loop server maintains a minimal memory footprint and zero thread synchronization overhead.


---

### Sipra & Akshat — Forward Proxy and Cache Implementation

#### 1. Features Implemented

- **Upstream TCP Relay & Request Rewriting**:
  - Raw POSIX socket connections to upstream origin servers using `socket()` and `connect()`.
  - DNS resolution and IP address lookup via `getaddrinfo()`.
  - Absolute-form URI parser (`http://host:port/path`) extracting host, port (default 80), and path.
  - Origin-form URI fallback with mandatory `Host: <host>:<port>` header support.
  - Stripping of hop-by-hop headers (`Proxy-Connection`, `Connection`, `Keep-Alive`, `Upgrade`).
  - Insertion of proxy identification headers: `Via: 1.1 caching-proxy`.
  - Unreachable origin failure handling: returns `502 Bad Gateway` HTML error page.
- **Thread-Safe LRU Cache Engine**:
  - $O(1)$ lookup via a 1024-bucket hash table with djb2 hashing algorithm.
  - $O(1)$ eviction order via doubly linked list tracking Most Recently Used (head) to Least Recently Used (tail).
  - Memory-bounded eviction: user-configurable maximum byte capacity (`--cache-size <MB>`).
  - Entry-bounded eviction: user-configurable maximum entry count (`--max-entries <count>`).
  - Isolated copy generation: readers receive a detached snapshot of the cache entry so client transmissions never hold the cache mutex lock.
  - Thread safety: all cache operations synchronized with `pthread_mutex_t`.
  - Performance counters tracking hits, misses, evictions, and revalidations.
- **HTTP/1.1 Cache-Control & Freshness Evaluation**:
  - `Cache-Control` directive parsing: `no-store`, `no-cache`, `private`, `public`, `max-age=N`, `must-revalidate`.
  - Cache eligibility enforcement: only `GET` and `HEAD` responses with status `200 OK` can be cached.
  - Privacy defense: responses with `Cache-Control: private` or `no-store` are never stored.
  - Freshness lifetime calculation: checks elapsed age against `max-age=N` (or default 60s).
  - Diagnostic response headers: `X-Cache: HIT`, `X-Cache: MISS`, `X-Cache: REVALIDATED`, and `Age: <seconds>`.
- **Conditional GET & Cache Revalidation**:
  - Detects stale cache entries or entries marked `no-cache`.
  - Injects `If-None-Match: <etag>` when ETag validator is available.
  - Injects `If-Modified-Since: <date>` when Last-Modified validator is available.
  - Handles `304 Not Modified` responses: refreshes cache metadata (`cached_at`, `max_age`) and serves the cached body with `X-Cache: REVALIDATED` without re-transmitting payload over the network.
  - Handles `200 OK` responses: updates cache with fresh response headers and body.
- **Multithreaded Proxy Concurrency**:
  - Worker thread pool with synchronized FIFO task queue (`pthread_mutex_t`, `pthread_cond_t`).
  - Configurable worker thread count (`--threads <num>`).
  - Socket read/write timeouts (`SO_RCVTIMEO`, `SO_SNDTIMEO`) preventing hanging connections.
  - Graceful shutdown on `SIGINT` and `SIGTERM` cleanly terminating worker threads and freeing cache memory.

---

#### 2. Forward Proxy Architecture

```
Client Request
      |
      v
+-------------------------------+
|  Proxy Listener Socket        |
|  (port 8081, accept loop)     |
+---------------+---------------+
                |
                v
+-------------------------------+
|  Synchronized Task Queue      |
+---------------+---------------+
                |
                v
+-------------------------------+
|  Worker Thread Pool           |
+---------------+---------------+
                |
                v
+-------------------------------+
|  Parse Request URI & Target   |
+---------------+---------------+
                |
                v
+-------------------------------+
|  LRU Cache Lookup (Hash Table)|
+-------+---------------+-------+
        |               |
[Fresh HIT]         [MISS]
        |               |
        |       +-------v-----------------------+
        |       | Connect to Upstream Origin    |
        |       | Forward Request (Via, Host)   |
        |       +-------+-----------------------+
        |               |
        |       +-------v-----------------------+
        |       | Read Upstream Response        |
        |       | If cacheable, store in LRU    |
        |       +-------+-----------------------+
        |               |
        v               v
+-------------------------------+
| Relay Response to Client      |
| (X-Cache: HIT / MISS, Via)    |
+-------------------------------+
```

---

#### 3. Compilation Instructions

```bash
# Build both HTTP server and Forward Proxy executables
make

# Build only the proxy
make proxy
```

The build produces both `./proxy` and `./caching_proxy`.

---

#### 4. Execution Commands

```bash
# Run Forward Proxy on port 8081 with 10 MB cache and 8 worker threads
./proxy --port 8081 --cache-size 10 --threads 8

# Display CLI options
./proxy --help
```

---

#### 5. Testing Instructions

```bash
# Run automated proxy test suite (spawns origin server and proxy)
make test_proxy
# OR directly
python3 tests/proxy/test_proxy.py
```

##### Automated Test Coverage:
1. Absolute-form URI forwarding (`GET http://127.0.0.1:18080/index.html`).
2. Response relay verification (headers, Content-Type, Content-Length, body byte accuracy).
3. Unreachable origin failure handling (`502 Bad Gateway`).
4. Cache MISS on initial request followed by Cache HIT on repeat request (`X-Cache: HIT`, `Age` header).
5. HEAD request forwarding through proxy (headers relayed with zero body bytes).
6. Origin-form URI with `Host` header resolution.
7. Unsupported method validation (`POST` returns `405 Method Not Allowed`).
8. Malformed request syntax rejected (`400 Bad Request`).
9. Concurrent client requests under multi-threaded load.

---

#### 6. Performance Benchmarking

```bash
python3 benchmarks/benchmark_proxy.py
```

##### Measured Benchmark Results:

- **Environment**: Ubuntu 24.04 (WSL 2) on Windows, GCC 13.3.0.
- **Workload**: 500 requests under 20 concurrent workers.

| Metric | Cache HIT (In-Memory) | Cache MISS (Upstream Relay) |
| :--- | :--- | :--- |
| **Total Requests** | 500 | 500 |
| **Successful Requests (200 OK)** | 500 (100%) | 500 (100%) |
| **Requests Per Second (RPS)** | **1,068.38 req/sec** | 1,115.35 req/sec |
| **Average Latency** | **17.53 ms** | 17.14 ms |
| **P50 Latency** | **17.10 ms** | 16.85 ms |
| **P95 Latency** | **26.89 ms** | 23.71 ms |
