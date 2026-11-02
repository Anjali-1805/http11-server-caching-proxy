#ifndef PROXY_H
#define PROXY_H

#include "lru_cache.h"

int run_proxy_server(int port, size_t cache_capacity);
int proxy_handle_client(int client_fd, lru_cache_t *cache);

#endif /* PROXY_H */
