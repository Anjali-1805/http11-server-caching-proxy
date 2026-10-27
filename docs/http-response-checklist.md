# HTTP/1.1 Response Checklist

Use this checklist during implementation and testing. Mark an item complete only after verifying it against the assignment and running an appropriate test.

## Status Codes

- [ ] 200 OK
- [ ] 304 Not Modified
- [ ] 400 Bad Request
- [ ] 404 Not Found
- [ ] 405 Method Not Allowed
- [ ] 414 URI Too Long
- [ ] 431 Request Header Fields Too Large
- [ ] 500 Internal Server Error
- [ ] 505 HTTP Version Not Supported

## Response Framing

- [ ] Correct Content-Length for applicable responses
- [ ] HEAD returns headers without a response body
- [ ] Chunked transfer encoding when a response length is unknown
- [ ] Persistent connections follow HTTP/1.1 connection rules
- [ ] No conflicting Content-Length and Transfer-Encoding headers

## Static File Responses

- [ ] Correct MIME type
- [ ] Missing files return an appropriate error
- [ ] Directory traversal attempts are rejected
- [ ] File access remains inside the configured document root

## Verification

Record the test command, expected result, actual result, and any known limitations for each requirement.
