/**
 * @file thread_pool.c
 * @brief Implementation of the thread-pool concurrency server model.
 */

#include "thread_pool.h"
#include "server.h"
#include "http_parser.h"
#include "http_response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

static void process_connection_blocking(int client_fd, struct sockaddr_in client_addr)
{
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);

    /* Set socket read/write timeouts to 5 seconds */
    struct timeval tv;
    tv.tv_sec = DEFAULT_KEEP_ALIVE_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

    char buf[MAX_REQUEST_BUFFER_SIZE];
    size_t buf_len = 0;
    int request_count = 0;

    while (!g_shutdown && request_count < DEFAULT_MAX_REQUESTS) {
        http_request_t req;
        http_status_code_t error_code = HTTP_STATUS_OK;
        size_t consumed_bytes = 0;

        /* Check if we already have a complete request in buffer from earlier read */
        parse_result_t pr = http_parse_request(buf, buf_len, &req, &consumed_bytes, &error_code);

        while (pr == PARSE_INCOMPLETE) {
            if (buf_len >= sizeof(buf) - 1) {
                error_code = HTTP_STATUS_HEADERS_TOO_LARGE;
                pr = PARSE_ERROR;
                break;
            }

            ssize_t n = recv(client_fd, buf + buf_len, sizeof(buf) - 1 - buf_len, 0);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                /* Timeout or socket error - end persistent connection */
                goto connection_done;
            } else if (n == 0) {
                /* Client closed connection gracefully */
                goto connection_done;
            }

            buf_len += (size_t)n;
            buf[buf_len] = '\0';

            pr = http_parse_request(buf, buf_len, &req, &consumed_bytes, &error_code);
        }

        if (pr == PARSE_ERROR) {
            /* Send error response and terminate connection */
            http_response_t resp;
            http_response_build_error(&resp, error_code, false);
            http_response_send_blocking(client_fd, &resp, false);
            http_response_cleanup(&resp);
            break;
        }

        /* pr == PARSE_OK: valid HTTP request received */
        request_count++;
        bool is_head = (req.method == HTTP_METHOD_HEAD);

        http_response_t resp;
        server_process_request(&req, &resp);

        bool sent_ok = http_response_send_blocking(client_fd, &resp, is_head);
        bool keep = resp.keep_alive;
        http_response_cleanup(&resp);

        if (!sent_ok || !keep) {
            break;
        }

        /* Shift unconsumed bytes (pipelined requests or extra payload) */
        if (consumed_bytes < buf_len) {
            memmove(buf, buf + consumed_bytes, buf_len - consumed_bytes);
            buf_len -= consumed_bytes;
            buf[buf_len] = '\0';
        } else {
            buf_len = 0;
        }
    }

connection_done:
    close(client_fd);
    (void)client_ip;
    (void)client_port;
}

static void *worker_thread_func(void *arg)
{
    thread_pool_t *pool = (thread_pool_t *)arg;

    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);

        while (!pool->shutdown && pool->queue_size == 0) {
            pthread_cond_wait(&pool->cond_has_task, &pool->queue_mutex);
        }

        if (pool->shutdown && pool->queue_size == 0) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }

        /* Pop task from queue */
        connection_task_t *task = pool->queue_head;
        pool->queue_head = task->next;
        if (!pool->queue_head) {
            pool->queue_tail = NULL;
        }
        pool->queue_size--;

        pthread_cond_signal(&pool->cond_has_space);
        pthread_mutex_unlock(&pool->queue_mutex);

        /* Process client request */
        process_connection_blocking(task->client_fd, task->client_addr);
        free(task);
    }

    return NULL;
}

thread_pool_t *thread_pool_create(int num_threads, int max_queue_size)
{
    if (num_threads <= 0) num_threads = 8;
    if (max_queue_size <= 0) max_queue_size = 1024;

    thread_pool_t *pool = calloc(1, sizeof(thread_pool_t));
    if (!pool) return NULL;

    pool->num_threads = num_threads;
    pool->max_queue_size = max_queue_size;
    pool->shutdown = false;

    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0 ||
        pthread_cond_init(&pool->cond_has_task, NULL) != 0 ||
        pthread_cond_init(&pool->cond_has_space, NULL) != 0) {
        free(pool);
        return NULL;
    }

    pool->threads = calloc((size_t)num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_cond_destroy(&pool->cond_has_task);
        pthread_cond_destroy(&pool->cond_has_space);
        free(pool);
        return NULL;
    }

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread_func, pool) != 0) {
            pool->shutdown = true;
            pthread_cond_broadcast(&pool->cond_has_task);
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            pthread_mutex_destroy(&pool->queue_mutex);
            pthread_cond_destroy(&pool->cond_has_task);
            pthread_cond_destroy(&pool->cond_has_space);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

bool thread_pool_add_connection(thread_pool_t *pool, int client_fd, struct sockaddr_in client_addr)
{
    if (!pool || client_fd < 0) return false;

    pthread_mutex_lock(&pool->queue_mutex);

    while (!pool->shutdown && pool->queue_size >= pool->max_queue_size) {
        pthread_cond_wait(&pool->cond_has_space, &pool->queue_mutex);
    }

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->queue_mutex);
        return false;
    }

    connection_task_t *task = malloc(sizeof(connection_task_t));
    if (!task) {
        pthread_mutex_unlock(&pool->queue_mutex);
        return false;
    }

    task->client_fd = client_fd;
    task->client_addr = client_addr;
    task->next = NULL;

    if (pool->queue_tail) {
        pool->queue_tail->next = task;
        pool->queue_tail = task;
    } else {
        pool->queue_head = task;
        pool->queue_tail = task;
    }
    pool->queue_size++;

    pthread_cond_signal(&pool->cond_has_task);
    pthread_mutex_unlock(&pool->queue_mutex);

    return true;
}

void thread_pool_destroy(thread_pool_t *pool)
{
    if (!pool) return;

    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->cond_has_task);
    pthread_cond_broadcast(&pool->cond_has_space);
    pthread_mutex_unlock(&pool->queue_mutex);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    /* Drain remaining queued tasks */
    pthread_mutex_lock(&pool->queue_mutex);
    connection_task_t *curr = pool->queue_head;
    while (curr) {
        connection_task_t *next = curr->next;
        close(curr->client_fd);
        free(curr);
        curr = next;
    }
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->queue_size = 0;
    pthread_mutex_unlock(&pool->queue_mutex);

    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->cond_has_task);
    pthread_cond_destroy(&pool->cond_has_space);

    free(pool->threads);
    free(pool);
}

int run_threadpool_server(int listen_fd, int num_threads)
{
    thread_pool_t *pool = thread_pool_create(num_threads, 1024);
    if (!pool) {
        fprintf(stderr, "Failed to initialize thread pool\n");
        return 1;
    }

    printf("[ThreadPool] Server running with %d worker threads. Waiting for connections...\n", num_threads);

    struct pollfd pfd;
    pfd.fd = listen_fd;
    pfd.events = POLLIN;

    while (!g_shutdown) {
        int ret = poll(&pfd, 1, 500); /* 500ms timeout for graceful shutdown check */
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll failed on listen socket");
            break;
        }
        if (ret == 0) {
            continue; /* Timeout, check g_shutdown */
        }

        if (pfd.revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (client_fd < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                perror("accept failed");
                continue;
            }

            if (!thread_pool_add_connection(pool, client_fd, client_addr)) {
                fprintf(stderr, "Task queue full or shutting down, rejecting connection\n");
                close(client_fd);
            }
        }
    }

    printf("\n[ThreadPool] Graceful shutdown initiated. Terminating worker threads...\n");
    thread_pool_destroy(pool);
    printf("[ThreadPool] All worker threads terminated cleanly.\n");

    return 0;
}
