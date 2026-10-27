# HTTP/1.1 Server Module

## Responsibility

This module implements the HTTP/1.1 server component of the project using C and raw TCP sockets. High-level HTTP server frameworks are not permitted by the assignment.

## Required Features

- Parse HTTP request lines and headers correctly.
- Support GET and HEAD methods.
- Return the required HTTP status codes.
- Handle persistent connections and correct response framing.
- Support Content-Length and chunked transfer encoding where required.
- Serve static files with MIME type detection.
- Prevent directory traversal.
- Implement both a thread-pool model and an event-loop model.
- Include tests for request parsing, responses, errors, file serving, and concurrency.

## Development Status

The module is currently in the planning stage. Features will be marked complete only after implementation and testing.

## Collaboration

Shared HTTP data structures and interfaces will be agreed with the proxy/cache contributor before integration.
