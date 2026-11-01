/**
 * @file event_loop.c
 * @brief Implementation of poll()-based non-blocking event loop.
 */

#include "event_loop.h"
#include "server.h"
#include "http_parser.h"
#include "http_response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define INITIAL_CAPACITY 64
#define CHUNK_SIZE 8192

typedef enum {
    CONN_STATE_READING_REQUEST = 0,
    CONN_STATE_WRITING_HEADERS,
    CONN_STATE_WRITING_BODY_MEM,
    CONN_STATE_WRITING_BODY_FILE
} event_state_t;

typedef struct {
    int fd;
    struct sockaddr_in addr;
    event_state_t state;
    time_t last_activity;

    /* Read buffer */
    char read_buf[MAX_REQUEST_BUFFER_SIZE];
    size_t read_len;

    /* Header write buffer */
    char write_buf[4096];
    size_t write_len;
    size_t write_offset;

    /* Body write state */
    char *mem_body;
    size_t mem_body_len;
    size_t mem_body_offset;

    int file_fd;
    off_t file_size;
    off_t file_sent;
    char file_chunk[CHUNK_SIZE];
    size_t chunk_len;
    size_t chunk_offset;

    bool is_head;
    bool keep_alive;
    int request_count;
} client_entry_t;

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void client_close(client_entry_t *c)
{
    if (!c || c->fd < 0) return;
    close(c->fd);
    c->fd = -1;

    if (c->mem_body) {
        free(c->mem_body);
        c->mem_body = NULL;
    }
    if (c->file_fd >= 0) {
        close(c->file_fd);
        c->file_fd = -1;
    }
}

static void prepare_response_in_client(client_entry_t *c, const http_request_t *req, http_response_t *resp)
{
    c->is_head = (req->method == HTTP_METHOD_HEAD);
    c->keep_alive = resp->keep_alive;
    c->request_count++;

    int hlen = http_response_format_headers(resp, c->write_buf, sizeof(c->write_buf));
    if (hlen < 0) {
        /* Formatting failed; fall back to basic 500 */
        c->write_len = snprintf(c->write_buf, sizeof(c->write_buf),
                                "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        c->keep_alive = false;
    } else {
        c->write_len = (size_t)hlen;
    }
    c->write_offset = 0;

    if (c->is_head) {
        c->state = CONN_STATE_WRITING_HEADERS;
        return;
    }

    if (resp->body_is_file && resp->file_fd >= 0) {
        c->file_fd = resp->file_fd;
        resp->file_fd = -1; /* Take ownership of fd */
        c->file_size = resp->file_size;
        c->file_sent = 0;
        c->chunk_len = 0;
        c->chunk_offset = 0;
        c->state = CONN_STATE_WRITING_HEADERS;
    } else if (resp->body && resp->body_len > 0) {
        c->mem_body = resp->body;
        resp->body = NULL; /* Take ownership of memory */
        c->mem_body_len = resp->body_len;
        c->mem_body_offset = 0;
        c->state = CONN_STATE_WRITING_HEADERS;
    } else {
        c->state = CONN_STATE_WRITING_HEADERS;
    }
}

static bool handle_client_read(client_entry_t *c)
{
    c->last_activity = time(NULL);

    while (1) {
        if (c->read_len >= sizeof(c->read_buf) - 1) {
            /* Header fields too large */
            http_response_t err_resp;
            http_response_build_error(&err_resp, HTTP_STATUS_HEADERS_TOO_LARGE, false);
            http_request_t dummy_req;
            memset(&dummy_req, 0, sizeof(dummy_req));
            prepare_response_in_client(c, &dummy_req, &err_resp);
            http_response_cleanup(&err_resp);
            return true;
        }

        ssize_t n = recv(c->fd, c->read_buf + c->read_len, sizeof(c->read_buf) - 1 - c->read_len, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; /* Finished reading for now */
            }
            if (errno == EINTR) {
                continue;
            }
            return false; /* Socket error */
        }
        if (n == 0) {
            return false; /* Client closed connection */
        }

        c->read_len += (size_t)n;
        c->read_buf[c->read_len] = '\0';
    }

    http_request_t req;
    http_status_code_t error_code = HTTP_STATUS_OK;
    size_t consumed_bytes = 0;

    parse_result_t pr = http_parse_request(c->read_buf, c->read_len, &req, &consumed_bytes, &error_code);
    if (pr == PARSE_INCOMPLETE) {
        return true; /* Wait for more data */
    }

    if (pr == PARSE_ERROR) {
        http_response_t err_resp;
        http_response_build_error(&err_resp, error_code, false);
        http_request_t dummy_req;
        memset(&dummy_req, 0, sizeof(dummy_req));
        prepare_response_in_client(c, &dummy_req, &err_resp);
        http_response_cleanup(&err_resp);
        c->read_len = 0;
        return true;
    }

    /* Valid HTTP request parsed */
    http_response_t resp;
    server_process_request(&req, &resp);
    prepare_response_in_client(c, &req, &resp);
    http_response_cleanup(&resp);

    /* Shift unconsumed bytes in read buffer */
    if (consumed_bytes < c->read_len) {
        memmove(c->read_buf, c->read_buf + consumed_bytes, c->read_len - consumed_bytes);
        c->read_len -= consumed_bytes;
        c->read_buf[c->read_len] = '\0';
    } else {
        c->read_len = 0;
    }

    return true;
}

static bool handle_client_write(client_entry_t *c)
{
    c->last_activity = time(NULL);

    /* Step 1: Write headers */
    if (c->state == CONN_STATE_WRITING_HEADERS) {
        while (c->write_offset < c->write_len) {
            ssize_t n = send(c->fd, c->write_buf + c->write_offset, c->write_len - c->write_offset, 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
                if (errno == EINTR) continue;
                return false;
            }
            if (n == 0) return false;
            c->write_offset += (size_t)n;
        }

        /* Headers finished */
        if (c->is_head) {
            goto response_complete;
        }

        if (c->file_fd >= 0) {
            c->state = CONN_STATE_WRITING_BODY_FILE;
        } else if (c->mem_body && c->mem_body_len > 0) {
            c->state = CONN_STATE_WRITING_BODY_MEM;
        } else {
            goto response_complete;
        }
    }

    /* Step 2: Write in-memory body */
    if (c->state == CONN_STATE_WRITING_BODY_MEM) {
        while (c->mem_body_offset < c->mem_body_len) {
            ssize_t n = send(c->fd, c->mem_body + c->mem_body_offset, c->mem_body_len - c->mem_body_offset, 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
                if (errno == EINTR) continue;
                return false;
            }
            if (n == 0) return false;
            c->mem_body_offset += (size_t)n;
        }
        goto response_complete;
    }

    /* Step 3: Write static file body */
    if (c->state == CONN_STATE_WRITING_BODY_FILE) {
        while (c->file_sent < c->file_size) {
            if (c->chunk_offset >= c->chunk_len) {
                /* Read next chunk from file */
                ssize_t r = read(c->file_fd, c->file_chunk, sizeof(c->file_chunk));
                if (r < 0) {
                    return false;
                }
                if (r == 0) {
                    break; /* End of file reached */
                }
                c->chunk_len = (size_t)r;
                c->chunk_offset = 0;
            }

            /* Send from chunk */
            ssize_t s = send(c->fd, c->file_chunk + c->chunk_offset, c->chunk_len - c->chunk_offset, 0);
            if (s < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
                if (errno == EINTR) continue;
                return false;
            }
            if (s == 0) return false;

            c->chunk_offset += (size_t)s;
            c->file_sent += s;
        }
        goto response_complete;
    }

response_complete:
    /* Clean up body resources */
    if (c->mem_body) {
        free(c->mem_body);
        c->mem_body = NULL;
    }
    if (c->file_fd >= 0) {
        close(c->file_fd);
        c->file_fd = -1;
    }

    if (c->keep_alive && c->request_count < DEFAULT_MAX_REQUESTS) {
        /* Transition back to reading next request */
        c->state = CONN_STATE_READING_REQUEST;
        c->write_offset = 0;
        c->write_len = 0;

        /* If there are already bytes waiting in read buffer, process them */
        if (c->read_len > 0) {
            return handle_client_read(c);
        }
        return true;
    }

    /* Connection not persistent or max requests reached */
    return false;
}

int run_eventloop_server(int listen_fd)
{
    if (set_nonblocking(listen_fd) < 0) {
        perror("Failed to set listening socket non-blocking");
        return 1;
    }

    size_t capacity = INITIAL_CAPACITY;
    struct pollfd *pfds = malloc(capacity * sizeof(struct pollfd));
    client_entry_t *clients = calloc(capacity, sizeof(client_entry_t));
    if (!pfds || !clients) {
        perror("Failed to allocate poll arrays");
        free(pfds);
        free(clients);
        return 1;
    }

    /* Index 0 is reserved for listening socket */
    pfds[0].fd = listen_fd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    clients[0].fd = listen_fd;

    size_t nfds = 1;

    printf("[EventLoop] Server running using poll() I/O multiplexing. Waiting for connections...\n");

    while (!g_shutdown) {
        int ret = poll(pfds, (nfds_t)nfds, 500); /* 500ms timeout */
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll error");
            break;
        }

        time_t now = time(NULL);

        /* Handle new incoming connections on listen_fd */
        if (pfds[0].revents & POLLIN) {
            while (1) {
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
                if (client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                        break; /* All pending connections accepted */
                    }
                    perror("accept error");
                    break;
                }

                if (set_nonblocking(client_fd) < 0) {
                    close(client_fd);
                    continue;
                }

                /* Expand capacity if needed */
                if (nfds >= capacity) {
                    size_t new_cap = capacity * 2;
                    struct pollfd *new_pfds = realloc(pfds, new_cap * sizeof(struct pollfd));
                    client_entry_t *new_clients = realloc(clients, new_cap * sizeof(client_entry_t));
                    if (!new_pfds || !new_clients) {
                        fprintf(stderr, "Failed to grow pollfd capacity, dropping client\n");
                        close(client_fd);
                        continue;
                    }
                    pfds = new_pfds;
                    clients = new_clients;
                    memset(&clients[capacity], 0, (new_cap - capacity) * sizeof(client_entry_t));
                    capacity = new_cap;
                }

                /* Initialize new client slot */
                size_t slot = nfds++;
                memset(&clients[slot], 0, sizeof(client_entry_t));
                clients[slot].fd = client_fd;
                clients[slot].addr = client_addr;
                clients[slot].state = CONN_STATE_READING_REQUEST;
                clients[slot].last_activity = now;
                clients[slot].file_fd = -1;

                pfds[slot].fd = client_fd;
                pfds[slot].events = POLLIN;
                pfds[slot].revents = 0;
            }
        }

        /* Process client events */
        for (size_t i = 1; i < nfds; ) {
            bool keep_client = true;
            client_entry_t *c = &clients[i];

            /* Check idle keep-alive timeout */
            if (c->state == CONN_STATE_READING_REQUEST && (now - c->last_activity) > DEFAULT_KEEP_ALIVE_TIMEOUT) {
                keep_client = false;
            }

            if (keep_client && (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                keep_client = false;
            }

            if (keep_client && (pfds[i].revents & POLLIN)) {
                if (!handle_client_read(c)) {
                    keep_client = false;
                }
            }

            if (keep_client && (pfds[i].revents & POLLOUT)) {
                if (!handle_client_write(c)) {
                    keep_client = false;
                }
            }

            if (!keep_client) {
                client_close(c);
                /* Compact array by replacing current element with last element */
                size_t last = nfds - 1;
                if (i != last) {
                    pfds[i] = pfds[last];
                    clients[i] = clients[last];
                }
                nfds--;
                continue; /* Re-check same index because last moved here */
            }

            /* Update monitored poll events based on client state */
            if (c->state == CONN_STATE_READING_REQUEST) {
                pfds[i].events = POLLIN;
            } else {
                pfds[i].events = POLLOUT;
            }
            pfds[i].revents = 0;
            i++;
        }
    }

    printf("\n[EventLoop] Graceful shutdown initiated. Closing all active connections...\n");
    for (size_t i = 1; i < nfds; i++) {
        client_close(&clients[i]);
    }

    free(pfds);
    free(clients);
    printf("[EventLoop] Event loop stopped cleanly.\n");

    return 0;
}
