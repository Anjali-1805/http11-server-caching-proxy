/**
 * @file http_response.h
 * @brief HTTP/1.1 Response generation and serialization.
 */

#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "../common/http_types.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get standard RFC reason phrase for status code.
 */
const char *http_status_reason_phrase(http_status_code_t code);

/**
 * @brief Format current UTC time in RFC 1123 / RFC 7231 format.
 *
 * @param buf Output buffer (at least 32 bytes).
 * @param buf_len Size of buffer.
 */
void http_format_date(char *buf, size_t buf_len);

/**
 * @brief Initialize an http_response_t with defaults.
 */
void http_response_init(http_response_t *resp);

/**
 * @brief Clean up any dynamically allocated resources in response.
 */
void http_response_cleanup(http_response_t *resp);

/**
 * @brief Generate a clean HTML error page response.
 *
 * @param resp Response structure to populate.
 * @param code HTTP status code.
 * @param keep_alive Whether connection should remain open.
 */
void http_response_build_error(http_response_t *resp, http_status_code_t code, bool keep_alive);

/**
 * @brief Format response headers into a string buffer.
 *
 * @param resp Response to serialize.
 * @param buf Output buffer.
 * @param max_len Capacity of output buffer.
 * @return Number of bytes written, or -1 on overflow/error.
 */
int http_response_format_headers(const http_response_t *resp, char *buf, size_t max_len);

/**
 * @brief Send complete HTTP response over a blocking socket.
 *
 * Handles partial writes and streams file body if body_is_file is true.
 * For HEAD requests, does not transmit the body.
 *
 * @param socket_fd Connected client socket.
 * @param resp Response structure.
 * @param is_head_request If true, send headers only.
 * @return true on success, false on socket error.
 */
bool http_response_send_blocking(int socket_fd, const http_response_t *resp, bool is_head_request);

/**
 * @brief Helper to write all bytes to a socket, handling partial writes.
 *
 * @param fd Socket descriptor.
 * @param data Data buffer.
 * @param len Total length to send.
 * @return true if all bytes sent successfully, false on error.
 */
bool send_all(int fd, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_RESPONSE_H */
