/**
 * @file event_loop.h
 * @brief Non-blocking I/O event-loop concurrency model using poll().
 */

#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the HTTP server in single-threaded event-loop mode using poll().
 *
 * Multiplexes listening socket and all active client connections without
 * spawning threads. Handles non-blocking reads, writes, persistent connections,
 * and timeouts.
 *
 * @param listen_fd Server listening socket.
 * @return 0 on clean shutdown, non-zero on error.
 */
int run_eventloop_server(int listen_fd);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_LOOP_H */
