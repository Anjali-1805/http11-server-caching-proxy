# HTTP/1.1 Server Module

## Responsibility

This module implements the HTTP/1.1 server component of the project using C and raw TCP sockets. High-level HTTP server frameworks are not permitted by the assignment.

## Required Features

- [x] Parse HTTP request lines and headers correctly.
- [x] Support GET and HEAD methods.
- [x] Return the required HTTP status codes (200, 400, 403, 404, 405, 414, 431, 500, 501, 505).
- [x] Handle persistent connections and correct response framing.
- [x] Support Content-Length and accurate MIME type headers.
- [x] Serve static files with MIME type detection.
- [x] Prevent directory traversal attacks.
- [x] Implement thread-pool concurrency model.
- [x] Implement event-loop concurrency model (`poll`).
- [x] Include comprehensive tests for request parsing, responses, errors, file serving, and concurrency.

## Development Status

Implementation complete, verified, and tested against all 18 requirements.

## File Organization

- `main.c`: CLI argument parsing, server configuration, signal handling (`SIGINT`, `SIGTERM`), and entry point.
- `server.h` & `server.c`: Socket creation, `SO_REUSEADDR` configuration, and request dispatch logic.
- `http_parser.h` & `http_parser.c`: HTTP/1.1 stream parser, URL percent-decoder, and boundary validator.
- `http_response.h` & `http_response.c`: Response serialization, error page generators, and partial write handler.
- `file_handler.h` & `file_handler.c`: Canonical path resolution, traversal prevention, and MIME type lookup.
- `thread_pool.h` & `thread_pool.c`: Thread-pool concurrency with mutex-protected queue and condition variables.
- `event_loop.h` & `event_loop.c`: Single-threaded event loop with non-blocking I/O multiplexing via `poll()`.
- `socket_learning.c`: Preserved introductory socket testing code.
