/**
 * @file http_parser.c
 * @brief Implementation of HTTP/1.1 request parser.
 */

#include "http_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void trim_inplace(char *str)
{
    if (!str) return;
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
    if (start != str) {
        memmove(str, start, end - start + 1);
    }
}

static int hex_char_to_int(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool url_decode(const char *src, char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0) return false;

    size_t s = 0;
    size_t d = 0;

    while (src[s] != '\0') {
        if (d + 1 >= dst_size) {
            return false; /* Buffer overflow */
        }

        if (src[s] == '%') {
            if (src[s + 1] == '\0' || src[s + 2] == '\0') {
                return false; /* Incomplete hex sequence */
            }
            int h1 = hex_char_to_int(src[s + 1]);
            int h2 = hex_char_to_int(src[s + 2]);
            if (h1 < 0 || h2 < 0) {
                return false; /* Non-hex characters */
            }
            char decoded = (char)((h1 << 4) | h2);
            if (decoded == '\0') {
                return false; /* Null byte injection attack */
            }
            dst[d++] = decoded;
            s += 3;
        } else {
            dst[d++] = src[s++];
        }
    }

    dst[d] = '\0';
    return true;
}

void http_request_init(http_request_t *req)
{
    if (!req) return;
    memset(req, 0, sizeof(*req));
    req->method = HTTP_METHOD_UNKNOWN;
    req->version = HTTP_VERSION_1_1;
    req->keep_alive = true; /* HTTP/1.1 default */
    req->content_length = -1;
}

static const char *find_header_end(const char *buf, size_t len, size_t *boundary_len)
{
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            *boundary_len = 4;
            return &buf[i];
        }
    }
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\n' && buf[i + 1] == '\n') {
            *boundary_len = 2;
            return &buf[i];
        }
    }
    return NULL;
}

parse_result_t http_parse_request(const char *buf,
                                  size_t len,
                                  http_request_t *req,
                                  size_t *consumed_bytes,
                                  http_status_code_t *error_code)
{
    if (!buf || !req || !consumed_bytes || !error_code) {
        return PARSE_ERROR;
    }

    *error_code = HTTP_STATUS_OK;
    http_request_init(req);

    size_t boundary_len = 0;
    const char *header_end = find_header_end(buf, len, &boundary_len);

    if (!header_end) {
        if (len >= MAX_TOTAL_HEADERS_SIZE) {
            *error_code = HTTP_STATUS_HEADERS_TOO_LARGE;
            return PARSE_ERROR;
        }
        if (len >= MAX_REQUEST_BUFFER_SIZE) {
            *error_code = HTTP_STATUS_BAD_REQUEST;
            return PARSE_ERROR;
        }
        return PARSE_INCOMPLETE;
    }

    size_t total_header_len = (size_t)(header_end - buf) + boundary_len;
    if (total_header_len > MAX_TOTAL_HEADERS_SIZE) {
        *error_code = HTTP_STATUS_HEADERS_TOO_LARGE;
        return PARSE_ERROR;
    }

    *consumed_bytes = total_header_len;

    /* Copy raw headers into a temporary working buffer */
    char temp_buf[MAX_TOTAL_HEADERS_SIZE + 1];
    size_t headers_content_len = (size_t)(header_end - buf);
    memcpy(temp_buf, buf, headers_content_len);
    temp_buf[headers_content_len] = '\0';

    /* Parse request line (up to first \n) */
    char *line_save = NULL;
    char *first_line = strtok_r(temp_buf, "\r\n", &line_save);
    if (!first_line || strlen(first_line) == 0) {
        *error_code = HTTP_STATUS_BAD_REQUEST;
        return PARSE_ERROR;
    }

    if (strlen(first_line) > MAX_REQUEST_LINE_LENGTH) {
        *error_code = HTTP_STATUS_URI_TOO_LONG;
        return PARSE_ERROR;
    }

    /* Tokenize request line: METHOD URI VERSION */
    char *tok_save = NULL;
    char *method_tok = strtok_r(first_line, " \t", &tok_save);
    char *uri_tok = strtok_r(NULL, " \t", &tok_save);
    char *version_tok = strtok_r(NULL, " \t", &tok_save);
    char *extra_tok = strtok_r(NULL, " \t", &tok_save);

    if (!method_tok || !uri_tok || !version_tok || extra_tok) {
        *error_code = HTTP_STATUS_BAD_REQUEST;
        return PARSE_ERROR;
    }

    /* Store raw method and map to enum */
    strncpy(req->raw_method, method_tok, sizeof(req->raw_method) - 1);
    if (strcmp(method_tok, "GET") == 0) {
        req->method = HTTP_METHOD_GET;
    } else if (strcmp(method_tok, "HEAD") == 0) {
        req->method = HTTP_METHOD_HEAD;
    } else if (strcmp(method_tok, "POST") == 0 ||
               strcmp(method_tok, "PUT") == 0 ||
               strcmp(method_tok, "DELETE") == 0 ||
               strcmp(method_tok, "CONNECT") == 0 ||
               strcmp(method_tok, "OPTIONS") == 0 ||
               strcmp(method_tok, "TRACE") == 0 ||
               strcmp(method_tok, "PATCH") == 0) {
        req->method = HTTP_METHOD_UNSUPPORTED;
    } else {
        req->method = HTTP_METHOD_UNKNOWN;
    }

    /* Validate URI length */
    if (strlen(uri_tok) >= MAX_URI_LENGTH) {
        *error_code = HTTP_STATUS_URI_TOO_LONG;
        return PARSE_ERROR;
    }
    strncpy(req->raw_uri, uri_tok, sizeof(req->raw_uri) - 1);

    /* Normalize URI: handle absolute URI e.g. "http://localhost:8080/path" */
    const char *path_start = uri_tok;
    if (strncmp(path_start, "http://", 7) == 0) {
        path_start += 7;
        const char *slash = strchr(path_start, '/');
        path_start = slash ? slash : "/";
    } else if (strncmp(path_start, "https://", 8) == 0) {
        path_start += 8;
        const char *slash = strchr(path_start, '/');
        path_start = slash ? slash : "/";
    }

    if (path_start[0] != '/') {
        *error_code = HTTP_STATUS_BAD_REQUEST;
        return PARSE_ERROR;
    }

    /* Strip query string before URL decoding */
    char clean_path[MAX_URI_LENGTH];
    strncpy(clean_path, path_start, sizeof(clean_path) - 1);
    clean_path[sizeof(clean_path) - 1] = '\0';
    char *query = strchr(clean_path, '?');
    if (query) {
        *query = '\0';
    }

    /* Percent-decode the path */
    if (!url_decode(clean_path, req->decoded_path, sizeof(req->decoded_path))) {
        *error_code = HTTP_STATUS_BAD_REQUEST;
        return PARSE_ERROR;
    }

    /* Parse and validate HTTP version */
    strncpy(req->raw_version, version_tok, sizeof(req->raw_version) - 1);
    if (strcmp(version_tok, "HTTP/1.1") == 0) {
        req->version = HTTP_VERSION_1_1;
        req->keep_alive = true;
    } else if (strcmp(version_tok, "HTTP/1.0") == 0) {
        req->version = HTTP_VERSION_1_0;
        req->keep_alive = false;
    } else if (strncmp(version_tok, "HTTP/", 5) == 0) {
        *error_code = HTTP_STATUS_VERSION_NOT_SUPPORTED;
        return PARSE_ERROR;
    } else {
        *error_code = HTTP_STATUS_BAD_REQUEST;
        return PARSE_ERROR;
    }

    /* Parse header fields */
    char *header_line = strtok_r(NULL, "\r\n", &line_save);
    while (header_line) {
        char *colon = strchr(header_line, ':');
        if (!colon) {
            *error_code = HTTP_STATUS_BAD_REQUEST;
            return PARSE_ERROR;
        }

        *colon = '\0';
        char *name = header_line;
        char *val = colon + 1;

        trim_inplace(name);
        trim_inplace(val);

        if (strlen(name) == 0) {
            *error_code = HTTP_STATUS_BAD_REQUEST;
            return PARSE_ERROR;
        }

        if (req->header_count >= MAX_HEADERS_COUNT) {
            *error_code = HTTP_STATUS_HEADERS_TOO_LARGE;
            return PARSE_ERROR;
        }

        /* Check for duplicate Host header */
        if (strcasecmp(name, "Host") == 0) {
            if (req->has_host) {
                *error_code = HTTP_STATUS_BAD_REQUEST;
                return PARSE_ERROR;
            }
            req->has_host = true;
            strncpy(req->host, val, sizeof(req->host) - 1);
        } else if (strcasecmp(name, "Connection") == 0) {
            strncpy(req->connection, val, sizeof(req->connection) - 1);
            if (strcasecmp(val, "close") == 0) {
                req->keep_alive = false;
            } else if (strcasecmp(val, "keep-alive") == 0) {
                req->keep_alive = true;
            }
        } else if (strcasecmp(name, "Content-Length") == 0) {
            char *endptr = NULL;
            long long cl = strtoll(val, &endptr, 10);
            if (cl < 0 || (endptr && *endptr != '\0')) {
                *error_code = HTTP_STATUS_BAD_REQUEST;
                return PARSE_ERROR;
            }
            req->content_length = (int64_t)cl;
        } else if (strcasecmp(name, "Content-Type") == 0) {
            strncpy(req->content_type, val, sizeof(req->content_type) - 1);
        }

        strncpy(req->headers[req->header_count].name, name, sizeof(req->headers[0].name) - 1);
        strncpy(req->headers[req->header_count].value, val, sizeof(req->headers[0].value) - 1);
        req->header_count++;

        header_line = strtok_r(NULL, "\r\n", &line_save);
    }

    /* HTTP/1.1 requires Host header */
    if (req->version == HTTP_VERSION_1_1 && !req->has_host) {
        *error_code = HTTP_STATUS_BAD_REQUEST;
        return PARSE_ERROR;
    }

    return PARSE_OK;
}
