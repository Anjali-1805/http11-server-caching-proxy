/**
 * @file upstream.c
 * @brief Implementation of upstream connection and request/response forwarding.
 */

#include "upstream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

bool parse_upstream_target(const http_request_t *req, upstream_target_t *target)
{
    if (!req || !target) return false;
    memset(target, 0, sizeof(*target));
    target->port = 80; /* Default HTTP port */

    const char *uri = req->raw_uri;

    /* Check for absolute-form URI: http://host[:port]/path */
    if (strncasecmp(uri, "http://", 7) == 0) {
        const char *host_start = uri + 7;
        const char *slash = strchr(host_start, '/');
        const char *colon = strchr(host_start, ':');

        if (colon && (!slash || colon < slash)) {
            /* Port is explicitly specified */
            size_t host_len = (size_t)(colon - host_start);
            if (host_len >= sizeof(target->host)) return false;
            strncpy(target->host, host_start, host_len);
            target->host[host_len] = '\0';

            target->port = atoi(colon + 1);
            if (target->port <= 0 || target->port > 65535) return false;
        } else {
            /* No port specified */
            size_t host_len = slash ? (size_t)(slash - host_start) : strlen(host_start);
            if (host_len >= sizeof(target->host)) return false;
            strncpy(target->host, host_start, host_len);
            target->host[host_len] = '\0';
        }

        if (slash) {
            strncpy(target->path, slash, sizeof(target->path) - 1);
        } else {
            strcpy(target->path, "/");
        }
        return true;
    }

    /* Fallback: Origin-form URI with mandatory Host header */
    if (req->has_host && strlen(req->host) > 0) {
        const char *colon = strchr(req->host, ':');
        if (colon) {
            size_t host_len = (size_t)(colon - req->host);
            if (host_len >= sizeof(target->host)) return false;
            strncpy(target->host, req->host, host_len);
            target->host[host_len] = '\0';
            target->port = atoi(colon + 1);
            if (target->port <= 0 || target->port > 65535) return false;
        } else {
            strncpy(target->host, req->host, sizeof(target->host) - 1);
        }

        if (uri[0] == '/') {
            strncpy(target->path, uri, sizeof(target->path) - 1);
            target->path[sizeof(target->path) - 1] = '\0';
        } else {
            target->path[0] = '/';
            strncpy(target->path + 1, uri, sizeof(target->path) - 2);
            target->path[sizeof(target->path) - 1] = '\0';
        }
        return true;
    }

    return false;
}

int upstream_connect(const char *host, int port, int timeout_sec)
{
    if (!host || port <= 0 || port > 65535) return -1;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0 || !res) {
        return -1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return -1;
    }

    if (timeout_sec > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return sock;
}

static bool send_all_fd(int fd, const char *buf, size_t len)
{
    size_t remaining = len;
    const char *ptr = buf;
    while (remaining > 0) {
        ssize_t n = send(fd, ptr, remaining, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        ptr += n;
        remaining -= (size_t)n;
    }
    return true;
}

bool upstream_forward_request(int upstream_fd,
                              const upstream_target_t *target,
                              const http_request_t *client_req,
                              const char *conditional_etag,
                              const char *conditional_last_modified)
{
    if (upstream_fd < 0 || !target || !client_req) return false;

    char req_buf[8192];
    int len = snprintf(req_buf, sizeof(req_buf),
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "User-Agent: Caching-Proxy/1.1\r\n"
        "Connection: close\r\n"
        "Via: 1.1 caching-proxy\r\n",
        client_req->raw_method[0] ? client_req->raw_method : "GET",
        target->path,
        target->host,
        target->port);

    if (len < 0 || (size_t)len >= sizeof(req_buf)) return false;

    /* Add conditional revalidation headers if provided */
    if (conditional_etag && strlen(conditional_etag) > 0) {
        int extra = snprintf(req_buf + len, sizeof(req_buf) - (size_t)len,
                             "If-None-Match: %s\r\n", conditional_etag);
        if (extra > 0) len += extra;
    }

    if (conditional_last_modified && strlen(conditional_last_modified) > 0) {
        int extra = snprintf(req_buf + len, sizeof(req_buf) - (size_t)len,
                             "If-Modified-Since: %s\r\n", conditional_last_modified);
        if (extra > 0) len += extra;
    }

    /* Forward eligible client request headers (stripping hop-by-hop headers) */
    for (size_t i = 0; i < client_req->header_count; i++) {
        const char *name = client_req->headers[i].name;
        const char *val = client_req->headers[i].value;

        if (strcasecmp(name, "Host") == 0 ||
            strcasecmp(name, "Connection") == 0 ||
            strcasecmp(name, "Proxy-Connection") == 0 ||
            strcasecmp(name, "Keep-Alive") == 0 ||
            strcasecmp(name, "Upgrade") == 0 ||
            strcasecmp(name, "If-None-Match") == 0 ||
            strcasecmp(name, "If-Modified-Since") == 0) {
            continue;
        }

        int extra = snprintf(req_buf + len, sizeof(req_buf) - (size_t)len,
                             "%s: %s\r\n", name, val);
        if (extra > 0) len += extra;
    }

    if ((size_t)len + 2 >= sizeof(req_buf)) return false;
    strcat(req_buf, "\r\n");
    len += 2;

    return send_all_fd(upstream_fd, req_buf, (size_t)len);
}

bool upstream_read_response(int upstream_fd, upstream_response_t *out_resp)
{
    if (upstream_fd < 0 || !out_resp) return false;
    memset(out_resp, 0, sizeof(*out_resp));
    out_resp->content_length = -1;

    size_t cap = 32768;
    char *buf = malloc(cap);
    if (!buf) return false;
    size_t total_read = 0;

    const char *header_boundary = NULL;
    size_t boundary_len = 0;

    /* Read until headers boundary (\r\n\r\n or \n\n) */
    while (1) {
        if (total_read + 2048 >= cap) {
            cap *= 2;
            char *new_buf = realloc(buf, cap);
            if (!new_buf) {
                free(buf);
                return false;
            }
            buf = new_buf;
        }

        ssize_t n = recv(upstream_fd, buf + total_read, cap - 1 - total_read, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break; /* EOF or error */
        }
        total_read += (size_t)n;
        buf[total_read] = '\0';

        /* Search for header boundary */
        for (size_t i = 0; i + 3 < total_read; i++) {
            if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                header_boundary = &buf[i];
                boundary_len = 4;
                break;
            }
        }
        if (!header_boundary) {
            for (size_t i = 0; i + 1 < total_read; i++) {
                if (buf[i] == '\n' && buf[i + 1] == '\n') {
                    header_boundary = &buf[i];
                    boundary_len = 2;
                    break;
                }
            }
        }

        if (header_boundary) break;
    }

    if (!header_boundary) {
        free(buf);
        return false;
    }

    size_t header_len = (size_t)(header_boundary - buf);
    out_resp->raw_headers = malloc(header_len + boundary_len + 1);
    if (!out_resp->raw_headers) {
        free(buf);
        return false;
    }
    memcpy(out_resp->raw_headers, buf, header_len + boundary_len);
    out_resp->raw_headers[header_len + boundary_len] = '\0';
    out_resp->raw_headers_len = header_len + boundary_len;

    /* Parse status line */
    char temp_headers[8192];
    size_t copy_len = header_len < sizeof(temp_headers) - 1 ? header_len : sizeof(temp_headers) - 1;
    memcpy(temp_headers, buf, copy_len);
    temp_headers[copy_len] = '\0';

    char *saveptr = NULL;
    char *line = strtok_r(temp_headers, "\r\n", &saveptr);
    if (line) {
        strncpy(out_resp->status_line, line, sizeof(out_resp->status_line) - 1);
        char *space1 = strchr(line, ' ');
        if (space1) {
            out_resp->status_code = atoi(space1 + 1);
        }
    }

    /* Parse response headers */
    line = strtok_r(NULL, "\r\n", &saveptr);
    while (line) {
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *name = line;
            char *val = colon + 1;
            while (*val == ' ' || *val == '\t') val++;

            if (strcasecmp(name, "Content-Length") == 0) {
                out_resp->content_length = (int64_t)strtoll(val, NULL, 10);
            } else if (strcasecmp(name, "Content-Type") == 0) {
                strncpy(out_resp->content_type, val, sizeof(out_resp->content_type) - 1);
            } else if (strcasecmp(name, "ETag") == 0) {
                strncpy(out_resp->etag, val, sizeof(out_resp->etag) - 1);
            } else if (strcasecmp(name, "Last-Modified") == 0) {
                strncpy(out_resp->last_modified, val, sizeof(out_resp->last_modified) - 1);
            } else if (strcasecmp(name, "Cache-Control") == 0) {
                strncpy(out_resp->cache_control, val, sizeof(out_resp->cache_control) - 1);
            } else if (strcasecmp(name, "Date") == 0) {
                strncpy(out_resp->date, val, sizeof(out_resp->date) - 1);
            }
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }

    /* Read remaining response body */
    size_t body_start_offset = header_len + boundary_len;
    size_t initial_body_bytes = total_read - body_start_offset;

    size_t target_body_size = (out_resp->content_length >= 0) ? (size_t)out_resp->content_length : 0;
    size_t body_cap = target_body_size > initial_body_bytes ? target_body_size + 1024 : initial_body_bytes + 4096;
    char *body = malloc(body_cap + 1);
    if (!body) {
        free(buf);
        return false;
    }

    if (initial_body_bytes > 0) {
        memcpy(body, buf + body_start_offset, initial_body_bytes);
    }
    size_t current_body_len = initial_body_bytes;
    free(buf); /* free temporary read buffer */

    /* Read until Content-Length is satisfied or socket closes */
    while (1) {
        if (out_resp->content_length >= 0 && current_body_len >= (size_t)out_resp->content_length) {
            break;
        }

        if (current_body_len + 2048 >= body_cap) {
            body_cap *= 2;
            char *new_body = realloc(body, body_cap + 1);
            if (!new_body) {
                free(body);
                return false;
            }
            body = new_body;
        }

        ssize_t n = recv(upstream_fd, body + current_body_len, body_cap - current_body_len, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        current_body_len += (size_t)n;
    }

    body[current_body_len] = '\0';
    out_resp->body = body;
    out_resp->body_len = current_body_len;

    return true;
}

void upstream_response_free(upstream_response_t *resp)
{
    if (!resp) return;
    if (resp->raw_headers) {
        free(resp->raw_headers);
        resp->raw_headers = NULL;
    }
    if (resp->body) {
        free(resp->body);
        resp->body = NULL;
    }
}
