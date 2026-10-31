/**
 * @file http_types.h
 * @brief Common HTTP/1.1 protocol types, constants, and status codes.
 *
 * Designed for use across the HTTP server and caching proxy components.
 */

#ifndef HTTP_TYPES_H
#define HTTP_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol limits */
#define MAX_URI_LENGTH            4096
#define MAX_REQUEST_LINE_LENGTH   8192
#define MAX_HEADER_NAME_LENGTH    256
#define MAX_HEADER_VALUE_LENGTH   2048
#define MAX_HEADERS_COUNT         64
#define MAX_TOTAL_HEADERS_SIZE    16384
#define MAX_REQUEST_BUFFER_SIZE   32768
#define DEFAULT_KEEP_ALIVE_TIMEOUT 5   /* seconds */
#define DEFAULT_MAX_REQUESTS       100

/**
 * @brief HTTP Methods supported or recognized by the server.
 */
typedef enum {
    HTTP_METHOD_UNKNOWN = 0,
    HTTP_METHOD_GET,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_CONNECT,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_TRACE,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_UNSUPPORTED
} http_method_t;

/**
 * @brief HTTP protocol versions.
 */
typedef enum {
    HTTP_VERSION_UNKNOWN = 0,
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1
} http_version_t;

/**
 * @brief Standard HTTP status codes.
 */
typedef enum {
    HTTP_STATUS_OK                    = 200,
    HTTP_STATUS_NO_CONTENT            = 204,
    HTTP_STATUS_NOT_MODIFIED          = 304,
    HTTP_STATUS_BAD_REQUEST           = 400,
    HTTP_STATUS_FORBIDDEN             = 403,
    HTTP_STATUS_NOT_FOUND             = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED    = 405,
    HTTP_STATUS_REQUEST_TIMEOUT       = 408,
    HTTP_STATUS_URI_TOO_LONG          = 414,
    HTTP_STATUS_HEADERS_TOO_LARGE     = 431,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    HTTP_STATUS_NOT_IMPLEMENTED       = 501,
    HTTP_STATUS_BAD_GATEWAY           = 502,
    HTTP_STATUS_SERVICE_UNAVAILABLE   = 503,
    HTTP_STATUS_GATEWAY_TIMEOUT       = 504,
    HTTP_STATUS_VERSION_NOT_SUPPORTED = 505
} http_status_code_t;

/**
 * @brief Key-value pair for HTTP headers.
 */
typedef struct {
    char name[MAX_HEADER_NAME_LENGTH];
    char value[MAX_HEADER_VALUE_LENGTH];
} http_header_t;

/**
 * @brief Parsed representation of an HTTP request.
 */
typedef struct {
    http_method_t method;
    char raw_method[32];
    char raw_uri[MAX_URI_LENGTH];
    char decoded_path[MAX_URI_LENGTH];
    http_version_t version;
    char raw_version[16];

    http_header_t headers[MAX_HEADERS_COUNT];
    size_t header_count;

    /* Pre-parsed common headers */
    bool has_host;
    char host[MAX_HEADER_VALUE_LENGTH];
    bool keep_alive;
    int64_t content_length;
    char content_type[MAX_HEADER_VALUE_LENGTH];
    char connection[64];
} http_request_t;

/**
 * @brief Representation of an HTTP response.
 */
typedef struct {
    http_status_code_t status_code;
    http_version_t version;
    char content_type[128];
    int64_t content_length;
    bool keep_alive;

    /* Body configuration */
    char *body;              /* In-memory body (e.g. error page or small string) */
    size_t body_len;
    bool body_is_file;       /* If true, stream content from file_fd */
    int file_fd;             /* Open file descriptor */
    off_t file_offset;       /* Current read offset */
    off_t file_size;         /* Total file size */
} http_response_t;

#ifdef __cplusplus
}
#endif

#endif /* HTTP_TYPES_H */
