# Project Architecture

## 1. Overview

This project implements an HTTP/1.1 server and a forward caching proxy using TCP sockets. The implementation will follow the assignment requirements and use C.

## 2. Components

### Common (`src/common/`)

Shared HTTP utilities, request and response representations, parsing helpers, and protocol-related constants. The team will agree on shared interfaces before implementing components that depend on them.

### HTTP Server (`src/server/`)

Responsible for accepting client connections, parsing HTTP requests, serving static files, generating responses, handling supported methods and status codes, and implementing the required connection and concurrency behavior.

### Forward Proxy (`src/proxy/`)

Responsible for accepting proxy requests, connecting to upstream servers, forwarding requests and responses, and handling upstream connection failures.

### Cache (`src/cache/`)

Responsible for cache entry storage, eviction policy, freshness rules, and HTTP cache validation behavior as required by the assignment.

## 3. Testing

- `tests/server/`: HTTP server tests.
- `tests/proxy/`: Forward proxy and cache tests.
- `tests/integration/`: Tests involving multiple components.
- `benchmarks/`: Performance and concurrency measurements.

## 4. Static Content

The `www/` directory will contain test files served by the HTTP server.

## 5. Collaboration

The HTTP server and proxy/cache will be developed on separate Git branches. Shared interfaces and integration changes will be reviewed by both team members before merging.

## 6. Design Decisions

Implementation details, interfaces, concurrency strategy, error handling, and cache behavior will be documented as the team makes and verifies those decisions against the assignment requirements.
