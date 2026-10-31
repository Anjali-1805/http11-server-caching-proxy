/**
 * @file http_response.c
 * @brief Implementation of HTTP/1.1 response generation and transmission.
 */

#include "http_response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

const char *http_status_reason_phrase(http_status_code_t code)
{
    switch (code) {
        case HTTP_STATUS_OK:                    return "OK";
        case HTTP_STATUS_NO_CONTENT:            return "No Content";
        case HTTP_STATUS_NOT_MODIFIED:          return "Not Modified";
        case HTTP_STATUS_BAD_REQUEST:           return "Bad Request";
        case HTTP_STATUS_FORBIDDEN:             return "Forbidden";
        case HTTP_STATUS_NOT_FOUND:             return "Not Found";
        case HTTP_STATUS_METHOD_NOT_ALLOWED:    return "Method Not Allowed";
        case HTTP_STATUS_REQUEST_TIMEOUT:       return "Request Timeout";
        case HTTP_STATUS_URI_TOO_LONG:          return "URI Too Long";
        case HTTP_STATUS_HEADERS_TOO_LARGE:     return "Request Header Fields Too Large";
        case HTTP_STATUS_INTERNAL_SERVER_ERROR: return "Internal Server Error";
        case HTTP_STATUS_NOT_IMPLEMENTED:       return "Not Implemented";
        case HTTP_STATUS_BAD_GATEWAY:           return "Bad Gateway";
        case HTTP_STATUS_SERVICE_UNAVAILABLE:   return "Service Unavailable";
        case HTTP_STATUS_GATEWAY_TIMEOUT:       return "Gateway Timeout";
        case HTTP_STATUS_VERSION_NOT_SUPPORTED: return "HTTP Version Not Supported";
        default:                                return "Unknown Status";
    }
}

void http_format_date(char *buf, size_t buf_len)
{
    if (!buf || buf_len < 30) return;
    time_t now = time(NULL);
    struct tm tm_info;
    gmtime_r(&now, &tm_info);
    strftime(buf, buf_len, "%a, %d %b %Y %H:%M:%S GMT", &tm_info);
}

void http_response_init(http_response_t *resp)
{
    if (!resp) return;
    memset(resp, 0, sizeof(*resp));
    resp->status_code = HTTP_STATUS_OK;
    resp->version = HTTP_VERSION_1_1;
    resp->keep_alive = true;
    resp->file_fd = -1;
    strncpy(resp->content_type, "text/plain; charset=utf-8", sizeof(resp->content_type) - 1);
}

void http_response_cleanup(http_response_t *resp)
{
    if (!resp) return;
    if (resp->body) {
        free(resp->body);
        resp->body = NULL;
    }
    if (resp->file_fd >= 0) {
        close(resp->file_fd);
        resp->file_fd = -1;
    }
}

void http_response_build_error(http_response_t *resp, http_status_code_t code, bool keep_alive)
{
    if (!resp) return;
    http_response_init(resp);
    resp->status_code = code;
    resp->keep_alive = keep_alive;
    strncpy(resp->content_type, "text/html; charset=utf-8", sizeof(resp->content_type) - 1);

    const char *reason = http_status_reason_phrase(code);

    char html_buf[2048];
    int len = snprintf(html_buf, sizeof(html_buf),
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <title>%d %s</title>\n"
        "    <style>\n"
        "        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f7f9fa; color: #333; margin: 0; padding: 40px; text-align: center; }\n"
        "        .container { background: #fff; max-width: 600px; margin: 50px auto; padding: 30px; border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); }\n"
        "        h1 { color: #d9534f; margin-bottom: 10px; }\n"
        "        p { font-size: 16px; line-height: 1.5; color: #666; }\n"
        "        hr { border: 0; border-top: 1px solid #eee; margin: 25px 0; }\n"
        "        .footer { font-size: 13px; color: #999; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <h1>%d %s</h1>\n"
        "        <p>The requested operation could not be completed by the server.</p>\n"
        "        <hr>\n"
        "        <div class=\"footer\">Anjali HTTP/1.1 Server &bull; Computer Networks Project</div>\n"
        "    </div>\n"
        "</body>\n"
        "</html>\n",
        code, reason, code, reason);

    if (len > 0) {
        resp->body = malloc((size_t)len + 1);
        if (resp->body) {
            memcpy(resp->body, html_buf, (size_t)len + 1);
            resp->body_len = (size_t)len;
            resp->content_length = (int64_t)len;
        }
    }
}

int http_response_format_headers(const http_response_t *resp, char *buf, size_t max_len)
{
    if (!resp || !buf || max_len == 0) return -1;

    char date_str[64];
    http_format_date(date_str, sizeof(date_str));

    const char *reason = http_status_reason_phrase(resp->status_code);
    const char *conn_val = resp->keep_alive ? "keep-alive" : "close";

    int written = 0;
    if (resp->status_code == HTTP_STATUS_METHOD_NOT_ALLOWED) {
        written = snprintf(buf, max_len,
            "HTTP/1.1 %d %s\r\n"
            "Date: %s\r\n"
            "Server: Anjali-HTTP-Server/1.1\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Connection: %s\r\n"
            "Allow: GET, HEAD\r\n"
            "\r\n",
            resp->status_code, reason,
            date_str,
            resp->content_type,
            (long)resp->content_length,
            conn_val);
    } else {
        written = snprintf(buf, max_len,
            "HTTP/1.1 %d %s\r\n"
            "Date: %s\r\n"
            "Server: Anjali-HTTP-Server/1.1\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Connection: %s\r\n"
            "\r\n",
            resp->status_code, reason,
            date_str,
            resp->content_type,
            (long)resp->content_length,
            conn_val);
    }

    if (written < 0 || (size_t)written >= max_len) {
        return -1;
    }

    return written;
}

bool send_all(int fd, const void *data, size_t len)
{
    const char *ptr = (const char *)data;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = send(fd, ptr, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false; /* Client closed socket */
        }
        ptr += sent;
        remaining -= (size_t)sent;
    }

    return true;
}

bool http_response_send_blocking(int socket_fd, const http_response_t *resp, bool is_head_request)
{
    if (!resp || socket_fd < 0) return false;

    char header_buf[4096];
    int header_len = http_response_format_headers(resp, header_buf, sizeof(header_buf));
    if (header_len < 0) {
        return false;
    }

    if (!send_all(socket_fd, header_buf, (size_t)header_len)) {
        return false;
    }

    /* For HEAD requests, do not send the body */
    if (is_head_request) {
        return true;
    }

    /* Send response body */
    if (resp->body_is_file && resp->file_fd >= 0) {
        if (lseek(resp->file_fd, 0, SEEK_SET) < 0) {
            return false;
        }

        char file_buf[8192];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(resp->file_fd, file_buf, sizeof(file_buf))) > 0) {
            if (!send_all(socket_fd, file_buf, (size_t)bytes_read)) {
                return false;
            }
        }
        if (bytes_read < 0) {
            return false;
        }
    } else if (resp->body && resp->body_len > 0) {
        if (!send_all(socket_fd, resp->body, resp->body_len)) {
            return false;
        }
    }

    return true;
}
