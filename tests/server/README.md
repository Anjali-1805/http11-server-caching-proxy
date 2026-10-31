# HTTP Server Tests

## Purpose

This directory contains automated tests for the HTTP/1.1 server component.

## Test Implementation

Automated testing is implemented in `test_suite.py` and run via `make test` or `python3 tests/server/test_suite.py`.

## Test Categories Implemented

1. **Request parsing:** valid requests, malformed request lines, incomplete headers, missing Host header in HTTP/1.1, and requests split across TCP reads.
2. **Methods:** GET and HEAD behavior (HEAD returns identical headers with no body).
3. **Status codes:** 200, 400, 403, 404, 405, 414, 431, 500, 501, 505.
4. **Response framing:** Content-Length, Content-Type, Connection: keep-alive and Connection: close.
5. **Static files:** HTML, CSS, JavaScript, TXT, JSON, and PDF files.
6. **Path traversal defense:** Path escaping attempts (`/../../etc/passwd`) blocked with 403 Forbidden.
7. **Concurrency:** 50 simultaneous client connections handled by both Thread-Pool and Event-Loop modes.
8. **Graceful shutdown:** Server cleanly terminates worker threads/multiplexers on SIGINT.

## Execution

```bash
python3 tests/server/test_suite.py
```
Both `threadpool` and `eventloop` modes pass all 18 requirements.
