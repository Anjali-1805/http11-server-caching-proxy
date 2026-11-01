/**
 * @file thread_pool.h
 * @brief Thread-pool concurrency model for the HTTP server.
 */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct connection_task {
    int client_fd;
    struct sockaddr_in client_addr;
    struct connection_task *next;
} connection_task_t;

typedef struct {
    pthread_t *threads;
    int num_threads;

    connection_task_t *queue_head;
    connection_task_t *queue_tail;
    int queue_size;
    int max_queue_size;

    pthread_mutex_t queue_mutex;
    pthread_cond_t cond_has_task;
    pthread_cond_t cond_has_space;

    volatile bool shutdown;
} thread_pool_t;

/**
 * @brief Create and start a thread pool.
 *
 * @param num_threads Number of worker threads to spawn.
 * @param max_queue_size Maximum number of queued pending connections.
 * @return Pointer to thread_pool_t, or NULL on failure.
 */
thread_pool_t *thread_pool_create(int num_threads, int max_queue_size);

/**
 * @brief Push a new accepted client connection to the task queue.
 *
 * @param pool Thread pool.
 * @param client_fd Socket descriptor of client.
 * @param client_addr Client network address.
 * @return true if enqueued successfully, false if queue is full or shutting down.
 */
bool thread_pool_add_connection(thread_pool_t *pool, int client_fd, struct sockaddr_in client_addr);

/**
 * @brief Gracefully shut down thread pool and free all resources.
 *
 * Signals all worker threads, waits for them to terminate via pthread_join,
 * and drains any remaining queued tasks.
 *
 * @param pool Thread pool.
 */
void thread_pool_destroy(thread_pool_t *pool);

/**
 * @brief Run the server in thread-pool mode.
 *
 * Creates server socket, binds, listens, and dispatches incoming clients
 * to the thread pool until shutdown signal is received.
 *
 * @param listen_fd Bound and listening server socket.
 * @param num_threads Number of worker threads.
 * @return 0 on clean shutdown, non-zero on error.
 */
int run_threadpool_server(int listen_fd, int num_threads);

#ifdef __cplusplus
}
#endif

#endif /* THREAD_POOL_H */
