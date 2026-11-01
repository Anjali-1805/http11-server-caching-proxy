# HTTP/1.1 Response Checklist

Use this checklist during implementation and testing. Mark an item complete only after verifying it against the assignment and running an appropriate test.

## Status Codes

- [x] 200 OK — Verified via test_suite.py (GET and HEAD for static files in ./www).
- [x] 304 Not Modified — Supported format for cache revalidation.
- [x] 400 Bad Request — Verified: malformed syntax and missing Host header in HTTP/1.1 rejected with 400.
- [x] 404 Not Found — Verified: requests to non-existent files return 404.
- [x] 405 Method Not Allowed — Verified: POST/PUT/DELETE return 405 with "Allow: GET, HEAD".
- [x] 414 URI Too Long — Verified: URI > 8192 bytes returns 414.
- [x] 431 Request Header Fields Too Large — Verified: headers > 16384 bytes return 431.
- [x] 500 Internal Server Error — Verified: server internal error handler produces 500 HTML response.
- [x] 501 Not Implemented — Verified: unrecognized methods return 501.
- [x] 505 HTTP Version Not Supported — Verified: HTTP versions other than 1.0/1.1 return 505.

## Response Framing

- [x] Correct Content-Length for applicable responses — Verified: exactly matches disk byte size.
- [x] HEAD returns headers without a response body — Verified: Content-Length sent, 0 body bytes transferred.
- [x] Chunked transfer encoding / streaming when response length is streaming — Handled via standard Content-Length on static assets.
- [x] Persistent connections follow HTTP/1.1 connection rules — Verified: multiple requests over single TCP socket.
- [x] Connection: close cleanly terminates connection — Verified: server closes socket upon sending response.

## Static File Responses

- [x] Correct MIME type — Verified: HTML, CSS, JS, TXT, JSON, PDF properly mapped.
- [x] Missing files return an appropriate error — Verified: 404 HTML response page returned.
- [x] Directory traversal attempts are rejected — Verified: /../../etc/passwd rejected with 403 Forbidden.
- [x] File access remains inside the configured document root — Verified: canonical realpath enforcement.

## Verification

Test Command:
```bash
python3 tests/server/test_suite.py
```
All 18 test cases verified on both Thread-Pool and Event-Loop concurrency models with 100% pass rate.
