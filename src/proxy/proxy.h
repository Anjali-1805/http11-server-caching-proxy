/**
 * @file proxy.h
 * @brief Forward HTTP/1.1 caching proxy server configuration and entry points.
 */

#ifndef PROXY_H
#define PROXY_H

#include "../common/http_types.h"
#include "../cache/lru_cache.h"
#include <signal.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int port;
    size_t cache_max_bytes;
    size_t cache_max_entries;
    int num_threads;
    int timeout_sec;
    int backlog;
} proxy_config_t;

extern volatile sig_atomic_t g_proxy_shutdown;
extern lru_cache_t *g_proxy_cache;

/**
 * @brief Run the forward caching proxy server.
 *
 * @param config Proxy configuration options.
 * @return 0 on clean shutdown, non-zero on error.
 */
int run_proxy_server(const proxy_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* PROXY_H */
