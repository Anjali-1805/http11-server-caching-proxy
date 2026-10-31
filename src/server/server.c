/**
 * @file server.c
 * @brief Common server initialization, socket binding, and request dispatch logic.
 */

#include "server.h"
#include "http_response.h"
#include "file_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

volatile sig_atomic_t g_shutdown = 0;

int create_and_bind_socket(int port, int backlog)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket creation failed");
        return -1;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        close(listen_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, backlog > 0 ? backlog : 128) < 0) {
        perror("listen failed");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

void server_process_request(const http_request_t *req, http_response_t *resp)
{
    if (!req || !resp) return;

    http_response_init(resp);
    resp->version = req->version;
    resp->keep_alive = req->keep_alive;

    /* Handle unsupported or unknown HTTP methods */
    if (req->method == HTTP_METHOD_UNSUPPORTED) {
        http_response_build_error(resp, HTTP_STATUS_METHOD_NOT_ALLOWED, req->keep_alive);
        return;
    }

    if (req->method == HTTP_METHOD_UNKNOWN ||
        (req->method != HTTP_METHOD_GET && req->method != HTTP_METHOD_HEAD)) {
        http_response_build_error(resp, HTTP_STATUS_NOT_IMPLEMENTED, req->keep_alive);
        return;
    }

    /* Process GET or HEAD method */
    file_info_t finfo;
    file_status_t fstat = file_handler_resolve_and_open(req->decoded_path, &finfo);

    if (fstat == FILE_OK) {
        resp->status_code = HTTP_STATUS_OK;
        resp->keep_alive = req->keep_alive;
        strncpy(resp->content_type, finfo.mime_type, sizeof(resp->content_type) - 1);
        resp->content_length = finfo.file_size;
        resp->body_is_file = true;
        resp->file_fd = finfo.file_fd;
        resp->file_size = finfo.file_size;
        resp->file_offset = 0;
    } else if (fstat == FILE_FORBIDDEN) {
        http_response_build_error(resp, HTTP_STATUS_FORBIDDEN, req->keep_alive);
    } else if (fstat == FILE_NOT_FOUND) {
        http_response_build_error(resp, HTTP_STATUS_NOT_FOUND, req->keep_alive);
    } else {
        http_response_build_error(resp, HTTP_STATUS_INTERNAL_SERVER_ERROR, req->keep_alive);
    }
}
