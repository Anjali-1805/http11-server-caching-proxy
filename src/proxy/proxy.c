/**
 * @file proxy.c
 * @brief Forward caching proxy server implementation with multithreading and LRU cache.
 */

#include "proxy.h"
#include "upstream.h"
#include "../cache/lru_cache.h"
#include "../cache/cache_policy.h"
#include "../cache/cache_validator.h"
#include "../server/server.h"
#include "../server/http_parser.h"
#include "../server/http_response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

volatile sig_atomic_t g_proxy_shutdown = 0;
lru_cache_t *g_proxy_cache = NULL;

/* Task queue for proxy worker threads */
typedef struct proxy_task {
    int client_fd;
    struct sockaddr_in client_addr;
    struct proxy_task *next;
} proxy_task_t;

typedef struct {
    pthread_t *threads;
    int num_threads;
    proxy_task_t *queue_head;
    proxy_task_t *queue_tail;
    int queue_size;
    pthread_mutex_t queue_mutex;
    pthread_cond_t cond_has_task;
    bool shutdown;
} proxy_pool_t;

static void send_proxy_error(int client_fd, int status_code, const char *reason, const char *message, bool keep_alive)
{
    char date_buf[64];
    http_format_date(date_buf, sizeof(date_buf));

    char body_buf[1024];
    int body_len = snprintf(body_buf, sizeof(body_buf),
        "<!DOCTYPE html>\n"
        "<html><head><title>%d %s</title></head>\n"
        "<body><h1>%d %s</h1><p>%s</p><hr><small>HTTP Caching Proxy</small></body></html>\n",
        status_code, reason, status_code, reason, message);

    char header_buf[1024];
    int header_len = snprintf(header_buf, sizeof(header_buf),
        "HTTP/1.1 %d %s\r\n"
        "Date: %s\r\n"
        "Server: Caching-Proxy/1.1\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: %s\r\n"
        "X-Cache: MISS\r\n"
        "Via: 1.1 caching-proxy\r\n\r\n",
        status_code, reason, date_buf, body_len, keep_alive ? "keep-alive" : "close");

    send_all(client_fd, header_buf, (size_t)header_len);
    send_all(client_fd, body_buf, (size_t)body_len);
}

static void relay_cached_entry(int client_fd,
                               const cache_entry_t *entry,
                               const char *cache_status,
                               bool is_head,
                               bool keep_alive)
{
    char date_buf[64];
    http_format_date(date_buf, sizeof(date_buf));

    time_t now = time(NULL);
    long age = (long)difftime(now, entry->cached_at);
    if (age < 0) age = 0;

    char header_buf[4096];
    int hlen = snprintf(header_buf, sizeof(header_buf),
        "HTTP/1.1 %d OK\r\n"
        "Date: %s\r\n"
        "Server: Caching-Proxy/1.1\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Age: %ld\r\n"
        "X-Cache: %s\r\n"
        "Via: 1.1 caching-proxy\r\n",
        entry->status_code,
        date_buf,
        entry->content_type[0] ? entry->content_type : "application/octet-stream",
        (long)entry->body_len,
        age,
        cache_status);

    if (entry->etag[0]) {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "ETag: %s\r\n", entry->etag);
    }
    if (entry->last_modified[0]) {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "Last-Modified: %s\r\n", entry->last_modified);
    }

    hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                     "Connection: %s\r\n\r\n", keep_alive ? "keep-alive" : "close");

    send_all(client_fd, header_buf, (size_t)hlen);

    if (!is_head && entry->body && entry->body_len > 0) {
        send_all(client_fd, entry->body, entry->body_len);
    }
}

static void relay_upstream_response(int client_fd,
                                    const upstream_response_t *resp,
                                    const char *cache_status,
                                    bool is_head,
                                    bool keep_alive)
{
    char date_buf[64];
    http_format_date(date_buf, sizeof(date_buf));

    char header_buf[4096];
    int hlen = snprintf(header_buf, sizeof(header_buf),
        "%s\r\n"
        "Date: %s\r\n"
        "Via: 1.1 caching-proxy\r\n"
        "X-Cache: %s\r\n"
        "Connection: %s\r\n",
        resp->status_line[0] ? resp->status_line : "HTTP/1.1 200 OK",
        date_buf,
        cache_status,
        keep_alive ? "keep-alive" : "close");

    if (resp->content_type[0]) {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "Content-Type: %s\r\n", resp->content_type);
    }
    if (resp->content_length >= 0) {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "Content-Length: %ld\r\n", (long)resp->content_length);
    } else {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "Content-Length: %ld\r\n", (long)resp->body_len);
    }
    if (resp->etag[0]) {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "ETag: %s\r\n", resp->etag);
    }
    if (resp->last_modified[0]) {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "Last-Modified: %s\r\n", resp->last_modified);
    }
    if (resp->cache_control[0]) {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "Cache-Control: %s\r\n", resp->cache_control);
    }

    hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen, "\r\n");

    send_all(client_fd, header_buf, (size_t)hlen);

    if (!is_head && resp->body && resp->body_len > 0) {
        send_all(client_fd, resp->body, resp->body_len);
    }
}

static void process_proxy_client(int client_fd)
{
    struct timeval tv;
    tv.tv_sec = DEFAULT_KEEP_ALIVE_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

    char buf[MAX_REQUEST_BUFFER_SIZE];
    size_t buf_len = 0;
    int request_count = 0;

    while (!g_proxy_shutdown && request_count < DEFAULT_MAX_REQUESTS) {
        http_request_t req;
        http_status_code_t error_code = HTTP_STATUS_OK;
        size_t consumed_bytes = 0;

        parse_result_t pr = http_parse_request(buf, buf_len, &req, &consumed_bytes, &error_code);

        while (pr == PARSE_INCOMPLETE) {
            if (buf_len >= sizeof(buf) - 1) {
                error_code = HTTP_STATUS_HEADERS_TOO_LARGE;
                pr = PARSE_ERROR;
                break;
            }

            ssize_t n = recv(client_fd, buf + buf_len, sizeof(buf) - 1 - buf_len, 0);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                goto client_done;
            }

            buf_len += (size_t)n;
            buf[buf_len] = '\0';
            pr = http_parse_request(buf, buf_len, &req, &consumed_bytes, &error_code);
        }

        if (pr == PARSE_ERROR) {
            send_proxy_error(client_fd, (int)error_code, "Bad Request", "Malformed request syntax.", false);
            break;
        }

        request_count++;
        bool is_head = (req.method == HTTP_METHOD_HEAD);

        /* Only GET and HEAD methods are supported by the forward proxy */
        if (req.method != HTTP_METHOD_GET && req.method != HTTP_METHOD_HEAD) {
            if (req.method == HTTP_METHOD_UNSUPPORTED) {
                send_proxy_error(client_fd, 405, "Method Not Allowed", "Method not supported by proxy.", false);
            } else {
                send_proxy_error(client_fd, 501, "Not Implemented", "Method not implemented.", false);
            }
            break;
        }

        upstream_target_t target;
        if (!parse_upstream_target(&req, &target)) {
            send_proxy_error(client_fd, 400, "Bad Request", "Invalid or missing request URI/Host.", false);
            break;
        }

        /* Build canonical URL key for cache lookup */
        char canon_url[MAX_URI_LENGTH + 512];
        snprintf(canon_url, sizeof(canon_url), "http://%s:%d%s", target.host, target.port, target.path);

        /* 1. Check LRU Cache */
        cache_entry_t *cached = lru_cache_get(g_proxy_cache, canon_url);

        if (cached && !cache_entry_needs_revalidation(cached)) {
            /* Case 1: Fresh Cache HIT */
            if (cache_check_client_conditional(&req, cached)) {
                send_proxy_304(client_fd, cached, "HIT", req.keep_alive);
            } else {
                relay_cached_entry(client_fd, cached, "HIT", is_head, req.keep_alive);
            }
            cache_entry_free_copy(cached);
        } else if (cached) {
            /* Case 2: Stale Cache Entry -> Conditional Revalidation with origin */
            upstream_response_t up_resp;
            bool was_304 = false;

            if (!cache_revalidate_with_upstream(g_proxy_cache, &target, &req, cached, &up_resp, &was_304)) {
                send_proxy_error(client_fd, 502, "Bad Gateway", "Unable to connect to upstream server for revalidation.", false);
                cache_entry_free_copy(cached);
                break;
            }

            if (was_304) {
                /* 304 Not Modified: Cache revalidated! Preserved cached body */
                if (cache_check_client_conditional(&req, cached)) {
                    send_proxy_304(client_fd, cached, "REVALIDATED", req.keep_alive);
                } else {
                    relay_cached_entry(client_fd, cached, "REVALIDATED", is_head, req.keep_alive);
                }
            } else {
                /* Origin returned full response (e.g. 200 OK or 404) */
                relay_upstream_response(client_fd, &up_resp, "MISS", is_head, req.keep_alive);
            }

            upstream_response_free(&up_resp);
            cache_entry_free_copy(cached);
        } else {
            /* Case 3: Cache MISS -> Fetch from upstream origin server */
            int upstream_fd = upstream_connect(target.host, target.port, 5);
            if (upstream_fd < 0) {
                send_proxy_error(client_fd, 502, "Bad Gateway", "Could not connect to upstream origin server.", false);
                break;
            }

            if (!upstream_forward_request(upstream_fd, &target, &req, NULL, NULL)) {
                close(upstream_fd);
                send_proxy_error(client_fd, 502, "Bad Gateway", "Failed forwarding request to upstream.", false);
                break;
            }

            upstream_response_t up_resp;
            if (!upstream_read_response(upstream_fd, &up_resp)) {
                close(upstream_fd);
                send_proxy_error(client_fd, 502, "Bad Gateway", "Failed reading response from upstream.", false);
                break;
            }
            close(upstream_fd);

            http_cache_control_t cc;
            parse_cache_control(up_resp.cache_control, &cc);
            if (is_response_cacheable(req.method, up_resp.status_code, &cc)) {
                lru_cache_put(g_proxy_cache, canon_url, up_resp.status_code,
                              up_resp.content_type, up_resp.body, up_resp.body_len,
                              up_resp.etag, up_resp.last_modified,
                              cc.max_age, cc.no_cache, cc.must_revalidate);
            }

            relay_upstream_response(client_fd, &up_resp, "MISS", is_head, req.keep_alive);
            upstream_response_free(&up_resp);
        }

        if (!req.keep_alive) {
            break;
        }

        /* Shift unconsumed bytes in read buffer */
        if (consumed_bytes < buf_len) {
            memmove(buf, buf + consumed_bytes, buf_len - consumed_bytes);
            buf_len -= consumed_bytes;
            buf[buf_len] = '\0';
        } else {
            buf_len = 0;
        }
    }

client_done:
    close(client_fd);
}

static void *proxy_worker_loop(void *arg)
{
    proxy_pool_t *pool = (proxy_pool_t *)arg;

    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        while (!pool->shutdown && pool->queue_size == 0) {
            pthread_cond_wait(&pool->cond_has_task, &pool->queue_mutex);
        }

        if (pool->shutdown && pool->queue_size == 0) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }

        proxy_task_t *task = pool->queue_head;
        pool->queue_head = task->next;
        if (!pool->queue_head) {
            pool->queue_tail = NULL;
        }
        pool->queue_size--;
        pthread_mutex_unlock(&pool->queue_mutex);

        process_proxy_client(task->client_fd);
        free(task);
    }

    return NULL;
}

int run_proxy_server(const proxy_config_t *config)
{
    if (!config) return 1;

    /* Initialize LRU cache */
    g_proxy_cache = lru_cache_create(config->cache_max_bytes, config->cache_max_entries);
    if (!g_proxy_cache) {
        fprintf(stderr, "Error: Failed to allocate LRU cache\n");
        return 1;
    }

    int listen_fd = create_and_bind_socket(config->port, config->backlog);
    if (listen_fd < 0) {
        fprintf(stderr, "Error: Failed to bind proxy listening socket on port %d\n", config->port);
        lru_cache_destroy(g_proxy_cache);
        return 1;
    }

    /* Start worker thread pool */
    proxy_pool_t pool;
    memset(&pool, 0, sizeof(pool));
    pool.num_threads = config->num_threads > 0 ? config->num_threads : 8;
    pthread_mutex_init(&pool.queue_mutex, NULL);
    pthread_cond_init(&pool.cond_has_task, NULL);

    pool.threads = calloc((size_t)pool.num_threads, sizeof(pthread_t));
    for (int i = 0; i < pool.num_threads; i++) {
        pthread_create(&pool.threads[i], NULL, proxy_worker_loop, &pool);
    }

    printf("=====================================================\n");
    printf("   HTTP/1.1 Forward Proxy & LRU Cache (Sipra & Akshat)\n");
    printf("=====================================================\n");
    printf(" Listening Port:      %d\n", config->port);
    printf(" Cache Max Memory:    %zu MB\n", config->cache_max_bytes / (1024 * 1024));
    printf(" Cache Max Entries:   %zu\n", config->cache_max_entries);
    printf(" Worker Threads:      %d\n", pool.num_threads);
    printf(" Status:              Proxy ready and listening\n");
    printf("=====================================================\n\n");

    struct pollfd pfd;
    pfd.fd = listen_fd;
    pfd.events = POLLIN;

    while (!g_proxy_shutdown) {
        int ret = poll(&pfd, 1, 500);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        if (pfd.revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (client_fd < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                break;
            }

            proxy_task_t *task = malloc(sizeof(proxy_task_t));
            if (task) {
                task->client_fd = client_fd;
                task->client_addr = client_addr;
                task->next = NULL;

                pthread_mutex_lock(&pool.queue_mutex);
                if (pool.queue_tail) {
                    pool.queue_tail->next = task;
                    pool.queue_tail = task;
                } else {
                    pool.queue_head = task;
                    pool.queue_tail = task;
                }
                pool.queue_size++;
                pthread_cond_signal(&pool.cond_has_task);
                pthread_mutex_unlock(&pool.queue_mutex);
            } else {
                close(client_fd);
            }
        }
    }

    printf("\n[Proxy] Shutting down. Joining worker threads...\n");
    pthread_mutex_lock(&pool.queue_mutex);
    pool.shutdown = true;
    pthread_cond_broadcast(&pool.cond_has_task);
    pthread_mutex_unlock(&pool.queue_mutex);

    for (int i = 0; i < pool.num_threads; i++) {
        pthread_join(pool.threads[i], NULL);
    }
    free(pool.threads);

    /* Drain queued tasks */
    proxy_task_t *curr = pool.queue_head;
    while (curr) {
        proxy_task_t *next = curr->next;
        close(curr->client_fd);
        free(curr);
        curr = next;
    }
    pthread_mutex_destroy(&pool.queue_mutex);
    pthread_cond_destroy(&pool.cond_has_task);

    close(listen_fd);

    /* Print cache final stats */
    cache_stats_t stats;
    lru_cache_get_stats(g_proxy_cache, &stats);
    printf("[Cache Stats] Hits: %lu | Misses: %lu | Revalidations: %lu | Evictions: %lu | Cached Entries: %zu\n",
           stats.hits, stats.misses, stats.revalidations, stats.evictions, stats.current_entries);

    lru_cache_destroy(g_proxy_cache);
    printf("[Proxy] Clean shutdown complete.\n");

    return 0;
}
