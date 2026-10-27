# http11-server-caching-proxy
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

### Sipra — Forward Proxy and Caching
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
- Language: C
- Environment: Windows with WSL (Ubuntu)
- Compiler: GCC
- Networking: TCP sockets
- Version control: Git and GitHub

## Project Status
Repository setup and planning. Implementation is in progress.

## AI Use
AI assistance will be documented in AI-USE.md in accordance with the assignment rules.
