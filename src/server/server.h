/**
 * @file server.h
 * @brief Common server configuration, shared interfaces, and runtime state.
 */

#ifndef SERVER_H
#define SERVER_H

#include "../common/http_types.h"
#include "file_handler.h"
#include <signal.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVER_MODE_THREADPOOL = 0,
    SERVER_MODE_EVENTLOOP  = 1
} server_mode_t;

typedef struct {
    int port;
    char doc_root[1024];
    server_mode_t mode;
    int num_threads;
    int backlog;
} server_config_t;

/* Global shutdown flag for signal handling */
extern volatile sig_atomic_t g_shutdown;

/**
 * @brief Create, bind, and listen on a TCP server socket.
 *
 * @param port Port number to bind to.
 * @param backlog Connection listen backlog.
 * @return Listening socket descriptor, or -1 on failure.
 */
int create_and_bind_socket(int port, int backlog);

/**
 * @brief Dispatch a parsed HTTP request to generate an appropriate response.
 *
 * Used by both thread-pool and event-loop concurrency engines.
 *
 * @param req Parsed HTTP request.
 * @param resp Output HTTP response structure.
 */
void server_process_request(const http_request_t *req, http_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_H */
