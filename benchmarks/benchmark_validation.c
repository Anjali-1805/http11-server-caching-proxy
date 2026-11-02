/**
 * @file benchmark_validation.c
 * @brief High-precision native C HTTP benchmark suite for proxy caching & revalidation.
 *
 * Contributed by Sipra (No Python).
 * Measures throughput, latency percentiles, hit ratios, and revalidation efficiency.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_SAMPLES 50000

typedef struct {
    double elapsed_ms;
    int status_code;
    char x_cache[32];
    int success;
} sample_result_t;

typedef struct {
    int thread_id;
    int proxy_port;
    int origin_port;
    const char *mode; /* "HIT", "MISS", "REVALIDATE", "UNCACHED", "MIXED" */
    int start_idx;
    int count;
    sample_result_t *results;
} worker_args_t;

static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t rem = len;
    while (rem > 0) {
        ssize_t n = send(fd, buf, rem, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        buf += n;
        rem -= (size_t)n;
    }
    return 0;
}

static void execute_request(int proxy_port,
                            int origin_port,
                            const char *path,
                            const char *extra_headers,
                            sample_result_t *out_res)
{
    out_res->success = 0;
    out_res->status_code = 0;
    out_res->x_cache[0] = '\0';

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)proxy_port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    double t0 = get_time_ms();

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        out_res->elapsed_ms = get_time_ms() - t0;
        return;
    }

    char req[1024];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "User-Agent: C-Benchmark/1.0\r\n"
        "Connection: close\r\n"
        "%s\r\n",
        path, origin_port, extra_headers ? extra_headers : "");

    if (send_all(sock, req, (size_t)req_len) < 0) {
        close(sock);
        out_res->elapsed_ms = get_time_ms() - t0;
        return;
    }

    char buf[4096];
    size_t total = 0;
    while (1) {
        ssize_t n = recv(sock, buf + total, sizeof(buf) - 1 - total, 0);
        if (n <= 0) break;
        total += (size_t)n;
        if (total >= sizeof(buf) - 1) break;
    }
    buf[total] = '\0';
    close(sock);

    double t1 = get_time_ms();
    out_res->elapsed_ms = t1 - t0;

    /* Parse status code and X-Cache header */
    if (total > 0) {
        char *line = strtok(buf, "\r\n");
        if (line) {
            char *sp = strchr(line, ' ');
            if (sp) out_res->status_code = atoi(sp + 1);
        }

        while (line) {
            if (strncasecmp(line, "X-Cache:", 8) == 0) {
                const char *val = line + 8;
                while (*val == ' ') val++;
                strncpy(out_res->x_cache, val, sizeof(out_res->x_cache) - 1);
            }
            line = strtok(NULL, "\r\n");
        }

        if (out_res->status_code == 200 || out_res->status_code == 304) {
            out_res->success = 1;
        }
    }
}

static void *worker_func(void *arg)
{
    worker_args_t *w = (worker_args_t *)arg;

    for (int i = 0; i < w->count; i++) {
        int idx = w->start_idx + i;
        char path[256];
        char extra[256] = "";

        if (strcmp(w->mode, "HIT") == 0) {
            strcpy(path, "/etag-resource");
        } else if (strcmp(w->mode, "MISS") == 0) {
            snprintf(path, sizeof(path), "/miss-resource?q=%d_%d", w->thread_id, i);
        } else if (strcmp(w->mode, "REVALIDATE") == 0) {
            strcpy(path, "/etag-resource");
            strcpy(extra, "If-None-Match: \"etag-v1\"\r\n");
        } else if (strcmp(w->mode, "UNCACHED") == 0) {
            snprintf(path, sizeof(path), "/uncached?q=%d_%d", w->thread_id, i);
            strcpy(extra, "Cache-Control: no-store\r\n");
        } else {
            /* MIXED: 70% HIT, 20% REVALIDATE, 10% MISS */
            int r = rand() % 100;
            if (r < 70) {
                strcpy(path, "/etag-resource");
            } else if (r < 90) {
                strcpy(path, "/etag-resource");
                strcpy(extra, "If-None-Match: \"etag-v1\"\r\n");
            } else {
                snprintf(path, sizeof(path), "/miss-q?q=%d_%d", w->thread_id, i);
            }
        }

        execute_request(w->proxy_port, w->origin_port, path, extra, &w->results[idx]);
    }

    return NULL;
}

static int compare_doubles(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

typedef struct {
    int total_requests;
    int successful_requests;
    int failed_requests;
    int hits;
    int revalidations;
    int misses;
    double total_duration_sec;
    double rps;
    double avg_latency;
    double p50_latency;
    double p95_latency;
    double p99_latency;
    double hit_ratio;
} benchmark_metrics_t;

static void run_benchmark_scenario(const char *name,
                                  const char *mode,
                                  int proxy_port,
                                  int origin_port,
                                  int total_reqs,
                                  int num_threads,
                                  benchmark_metrics_t *out_m)
{
    memset(out_m, 0, sizeof(*out_m));
    out_m->total_requests = total_reqs;

    sample_result_t *results = calloc((size_t)total_reqs, sizeof(sample_result_t));
    pthread_t *threads = calloc((size_t)num_threads, sizeof(pthread_t));
    worker_args_t *args = calloc((size_t)num_threads, sizeof(worker_args_t));

    int per_thread = total_reqs / num_threads;
    int rem = total_reqs % num_threads;

    /* Warm up 1 request for HIT/REVALIDATE scenarios */
    sample_result_t warmup;
    execute_request(proxy_port, origin_port, "/etag-resource", NULL, &warmup);

    double t_start = get_time_ms();

    int curr_idx = 0;
    for (int i = 0; i < num_threads; i++) {
        int count = per_thread + (i == num_threads - 1 ? rem : 0);
        args[i].thread_id = i;
        args[i].proxy_port = proxy_port;
        args[i].origin_port = origin_port;
        args[i].mode = mode;
        args[i].start_idx = curr_idx;
        args[i].count = count;
        args[i].results = results;

        pthread_create(&threads[i], NULL, worker_func, &args[i]);
        curr_idx += count;
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    double t_end = get_time_ms();
    out_m->total_duration_sec = (t_end - t_start) / 1000.0;
    out_m->rps = out_m->total_duration_sec > 0 ? (double)total_reqs / out_m->total_duration_sec : 0;

    double *latencies = malloc((size_t)total_reqs * sizeof(double));
    double sum_lat = 0;

    for (int i = 0; i < total_reqs; i++) {
        if (results[i].success) {
            out_m->successful_requests++;
        } else {
            out_m->failed_requests++;
        }

        if (strstr(results[i].x_cache, "HIT")) {
            out_m->hits++;
        } else if (strstr(results[i].x_cache, "REVALIDATED")) {
            out_m->revalidations++;
        } else {
            out_m->misses++;
        }

        latencies[i] = results[i].elapsed_ms;
        sum_lat += results[i].elapsed_ms;
    }

    out_m->avg_latency = sum_lat / (double)total_reqs;
    out_m->hit_ratio = (double)(out_m->hits + out_m->revalidations) / (double)total_reqs * 100.0;

    qsort(latencies, (size_t)total_reqs, sizeof(double), compare_doubles);
    out_m->p50_latency = latencies[(int)(total_reqs * 0.50)];
    out_m->p95_latency = latencies[(int)(total_reqs * 0.95)];
    out_m->p99_latency = latencies[(int)(total_reqs * 0.99)];

    free(latencies);
    free(threads);
    free(args);
    free(results);

    printf("  Completed %-24s: %d reqs in %.3fs | %.2f req/s | Avg: %.2f ms | P95: %.2f ms\n",
           name, total_reqs, out_m->total_duration_sec, out_m->rps, out_m->avg_latency, out_m->p95_latency);
}

int main(int argc, char *argv[])
{
    int proxy_port = 18091;
    int origin_port = 18090;
    int total_reqs = 500;
    int num_threads = 10;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--proxy-port") == 0 && i + 1 < argc) {
            proxy_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--origin-port") == 0 && i + 1 < argc) {
            origin_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--requests") == 0 && i + 1 < argc) {
            total_reqs = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[++i]);
        }
    }

    printf("========================================================================================\n");
    printf("   FORWARD PROXY & CACHE VALIDATION PERFORMANCE BENCHMARKS (Sipra)\n");
    printf("========================================================================================\n");
    printf(" Proxy Port:      %d\n", proxy_port);
    printf(" Origin Port:     %d\n", origin_port);
    printf(" Workload:        %d requests per scenario (%d concurrent threads)\n", total_reqs, num_threads);
    printf(" Timing Engine:   clock_gettime(CLOCK_MONOTONIC) microsecond precision\n");
    printf("========================================================================================\n\n");

    benchmark_metrics_t m_hit, m_miss, m_reval, m_uncached, m_cached;

    run_benchmark_scenario("1. Cache HITs", "HIT", proxy_port, origin_port, total_reqs, num_threads, &m_hit);
    run_benchmark_scenario("2. Cache MISSes", "MISS", proxy_port, origin_port, total_reqs, num_threads, &m_miss);
    run_benchmark_scenario("3. Conditional Reval", "REVALIDATE", proxy_port, origin_port, total_reqs, num_threads, &m_reval);
    run_benchmark_scenario("4. Without Caching", "UNCACHED", proxy_port, origin_port, total_reqs, num_threads, &m_uncached);
    run_benchmark_scenario("5. With Caching (Mixed)", "MIXED", proxy_port, origin_port, total_reqs, num_threads, &m_cached);

    double latency_savings = ((m_uncached.avg_latency - m_hit.avg_latency) / m_uncached.avg_latency) * 100.0;
    if (latency_savings < 0) latency_savings = 0;

    printf("\n=========================================================================================================\n");
    printf("%-26s | %-12s | %-12s | %-12s | %-10s | %-10s | %-10s\n",
           "Benchmark Scenario", "Reqs (OK/Tot)", "Throughput", "Hit Ratio", "Avg Latency", "P50 Latency", "P95 Latency");
    printf("---------------------------------------------------------------------------------------------------------\n");
    printf("%-26s | %4d / %4d   | %8.2f r/s | %9.1f%% | %7.2f ms | %7.2f ms | %7.2f ms\n",
           "Cache HITs (In-Memory)", m_hit.successful_requests, m_hit.total_requests, m_hit.rps, m_hit.hit_ratio, m_hit.avg_latency, m_hit.p50_latency, m_hit.p95_latency);
    printf("%-26s | %4d / %4d   | %8.2f r/s | %9.1f%% | %7.2f ms | %7.2f ms | %7.2f ms\n",
           "Conditional Revalidation", m_reval.successful_requests, m_reval.total_requests, m_reval.rps, m_reval.hit_ratio, m_reval.avg_latency, m_reval.p50_latency, m_reval.p95_latency);
    printf("%-26s | %4d / %4d   | %8.2f r/s | %9.1f%% | %7.2f ms | %7.2f ms | %7.2f ms\n",
           "Mixed Workload (Caching)", m_cached.successful_requests, m_cached.total_requests, m_cached.rps, m_cached.hit_ratio, m_cached.avg_latency, m_cached.p50_latency, m_cached.p95_latency);
    printf("%-26s | %4d / %4d   | %8.2f r/s | %9.1f%% | %7.2f ms | %7.2f ms | %7.2f ms\n",
           "Cache MISSes (Relayed)", m_miss.successful_requests, m_miss.total_requests, m_miss.rps, m_miss.hit_ratio, m_miss.avg_latency, m_miss.p50_latency, m_miss.p95_latency);
    printf("%-26s | %4d / %4d   | %8.2f r/s | %9.1f%% | %7.2f ms | %7.2f ms | %7.2f ms\n",
           "Without Caching (Bypass)", m_uncached.successful_requests, m_uncached.total_requests, m_uncached.rps, m_uncached.hit_ratio, m_uncached.avg_latency, m_uncached.p50_latency, m_uncached.p95_latency);
    printf("=========================================================================================================\n");
    printf(" Latency Savings (HIT vs Uncached): %.1f%% Latency Reduction\n", latency_savings);
    printf(" Throughput Acceleration:           %.2fx Throughput Improvement\n", m_hit.rps / (m_uncached.rps > 0 ? m_uncached.rps : 1.0));
    printf("=========================================================================================================\n\n");

    return 0;
}
