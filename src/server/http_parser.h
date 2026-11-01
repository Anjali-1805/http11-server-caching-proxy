/**
 * @file http_parser.h
 * @brief HTTP/1.1 Request Parser module.
 */

#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include "../common/http_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PARSE_OK = 0,         /**< Request parsed completely */
    PARSE_INCOMPLETE = 1, /**< Need more bytes from TCP socket */
    PARSE_ERROR = -1      /**< Malformed request or protocol violation */
} parse_result_t;

/**
 * @brief Initialize an HTTP request structure with default values.
 *
 * @param req Pointer to http_request_t to initialize.
 */
void http_request_init(http_request_t *req);

/**
 * @brief Parse an HTTP/1.1 request from a buffer.
 *
 * @param buf Input buffer containing received TCP stream bytes.
 * @param len Number of bytes available in buf.
 * @param req Pointer to http_request_t where parsed data will be stored.
 * @param consumed_bytes Output pointer storing number of bytes consumed (headers boundary).
 * @param error_code Output pointer storing HTTP status code if PARSE_ERROR is returned.
 * @return PARSE_OK if complete request was parsed,
 *         PARSE_INCOMPLETE if more data is needed,
 *         PARSE_ERROR if request is malformed.
 */
parse_result_t http_parse_request(const char *buf,
                                  size_t len,
                                  http_request_t *req,
                                  size_t *consumed_bytes,
                                  http_status_code_t *error_code);

/**
 * @brief Decode URL percent-encoded string.
 *
 * @param src Input string (e.g. "/hello%20world").
 * @param dst Destination buffer.
 * @param dst_size Size of destination buffer.
 * @return true on success, false if invalid encoding or buffer overflow.
 */
bool url_decode(const char *src, char *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_PARSER_H */
