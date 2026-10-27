# HTTP Server Tests

## Purpose

This directory contains tests for the HTTP/1.1 server component.

## Planned Test Categories

1. **Request parsing:** valid requests, malformed request lines, incomplete headers, and requests split across TCP reads.
2. **Methods:** GET and HEAD behavior.
3. **Status codes:** successful responses and required error responses, including 400, 404, 405, 414, 431, 500, and 505 where applicable.
4. **Response framing:** Content-Length, chunked transfer encoding, and persistent connections.
5. **Static files:** existing files, missing files, MIME types, and directory traversal attempts.
6. **Concurrency:** simultaneous client requests handled by both the thread-pool and event-loop implementations.
7. **Regression tests:** previously fixed bugs must remain fixed.

## Test Status

Tests will be marked complete only after they have been implemented, run, and their results recorded.

## Constraints

The server must use raw TCP sockets. Prohibited HTTP server frameworks must not be used.
