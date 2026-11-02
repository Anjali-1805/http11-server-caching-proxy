/**
 * @file upstream.h
 * @brief Upstream TCP connection, URI resolution, and HTTP request forwarding.
 */

#ifndef UPSTREAM_H
#define UPSTREAM_H

#include "../common/http_types.h"
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char host[256];
    int port;
    char path[MAX_URI_LENGTH];
} upstream_target_t;

typedef struct {
    int status_code;
    char status_line[128];
    char content_type[128];
    int64_t content_length;
    char etag[128];
    char last_modified[128];
    char cache_control[256];
    char date[64];

    char *raw_headers;
    size_t raw_headers_len;

    char *body;
    size_t body_len;
} upstream_response_t;

/**
 * @brief Parse client request URI and Host header into an upstream target.
 *
 * Supports both absolute-form URI (e.g. http://localhost:8080/index.html)
 * and origin-form URI (e.g. /index.html with Host header).
 */
bool parse_upstream_target(const http_request_t *req, upstream_target_t *target);

/**
 * @brief Connect to an upstream server via TCP.
 *
 * @param host Upstream hostname or IP string.
 * @param port Upstream port number.
 * @param timeout_sec Socket timeout in seconds.
 * @return Connected socket file descriptor, or -1 on error.
 */
int upstream_connect(const char *host, int port, int timeout_sec);

/**
 * @brief Forward client request to upstream server, optionally adding conditional headers.
 */
bool upstream_forward_request(int upstream_fd,
                              const upstream_target_t *target,
                              const http_request_t *client_req,
                              const char *conditional_etag,
                              const char *conditional_last_modified);

/**
 * @brief Read full response from upstream origin server.
 */
bool upstream_read_response(int upstream_fd, upstream_response_t *out_resp);

/**
 * @brief Free resources allocated in upstream_response_t.
 */
void upstream_response_free(upstream_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* UPSTREAM_H */
