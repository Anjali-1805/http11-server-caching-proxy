/**
 * @file main.c
 * @brief HTTP/1.1 Server entry point with CLI options and signal handling.
 */

#include "server.h"
#include "file_handler.h"
#include "thread_pool.h"
#include "event_loop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>

static void handle_signal(int sig)
{
    (void)sig;
    g_shutdown = 1;
}

static void print_usage(const char *prog_name)
{
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  --port <port>         Port to listen on (default: 8080)\n");
    printf("  --root <path>         Document root directory (default: ./www)\n");
    printf("  --mode <mode>         Concurrency mode: 'threadpool' or 'eventloop' (default: threadpool)\n");
    printf("  --threads <num>       Number of worker threads for threadpool mode (default: 8)\n");
    printf("  --help, -h            Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s --port 8080 --root ./www --mode threadpool\n", prog_name);
    printf("  %s --port 8080 --root ./www --mode eventloop\n", prog_name);
}

int main(int argc, char *argv[])
{
    server_config_t config;
    config.port = 8080;
    strncpy(config.doc_root, "./www", sizeof(config.doc_root) - 1);
    config.mode = SERVER_MODE_THREADPOOL;
    config.num_threads = 8;
    config.backlog = 128;

    static struct option long_options[] = {
        {"port",    required_argument, 0, 'p'},
        {"root",    required_argument, 0, 'r'},
        {"mode",    required_argument, 0, 'm'},
        {"threads", required_argument, 0, 't'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "p:r:m:t:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p':
                config.port = atoi(optarg);
                if (config.port <= 0 || config.port > 65535) {
                    fprintf(stderr, "Error: Invalid port number '%s'. Must be between 1 and 65535.\n", optarg);
                    return 1;
                }
                break;
            case 'r':
                strncpy(config.doc_root, optarg, sizeof(config.doc_root) - 1);
                config.doc_root[sizeof(config.doc_root) - 1] = '\0';
                break;
            case 'm':
                if (strcasecmp(optarg, "threadpool") == 0) {
                    config.mode = SERVER_MODE_THREADPOOL;
                } else if (strcasecmp(optarg, "eventloop") == 0) {
                    config.mode = SERVER_MODE_EVENTLOOP;
                } else {
                    fprintf(stderr, "Error: Unknown concurrency mode '%s'. Use 'threadpool' or 'eventloop'.\n", optarg);
                    return 1;
                }
                break;
            case 't':
                config.num_threads = atoi(optarg);
                if (config.num_threads <= 0 || config.num_threads > 256) {
                    fprintf(stderr, "Error: Invalid thread count '%s'. Must be between 1 and 256.\n", optarg);
                    return 1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Set up signal handlers for graceful shutdown */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignore SIGPIPE so broken client pipes don't kill server */
    signal(SIGPIPE, SIG_IGN);

    /* Initialize document root and verify path */
    if (!file_handler_init(config.doc_root)) {
        fprintf(stderr, "Error: Failed to initialize document root: %s\n", config.doc_root);
        return 1;
    }

    /* Bind and listen on TCP socket */
    int listen_fd = create_and_bind_socket(config.port, config.backlog);
    if (listen_fd < 0) {
        fprintf(stderr, "Error: Failed to bind to port %d\n", config.port);
        return 1;
    }

    /* Print startup information */
    printf("=====================================================\n");
    printf("       HTTP/1.1 Server (Anjali's Implementation)      \n");
    printf("=====================================================\n");
    printf(" Listening Port:      %d\n", config.port);
    printf(" Document Root:       %s\n", file_handler_get_root());
    printf(" Concurrency Mode:    %s\n", config.mode == SERVER_MODE_THREADPOOL ? "threadpool" : "eventloop");
    if (config.mode == SERVER_MODE_THREADPOOL) {
        printf(" Worker Threads:      %d\n", config.num_threads);
    }
    printf(" Protocol:            HTTP/1.1\n");
    printf(" Status:              Server is ready and listening\n");
    printf("=====================================================\n\n");

    int exit_code = 0;
    if (config.mode == SERVER_MODE_THREADPOOL) {
        exit_code = run_threadpool_server(listen_fd, config.num_threads);
    } else {
        exit_code = run_eventloop_server(listen_fd);
    }

    close(listen_fd);
    printf("Server stopped cleanly. Goodbye!\n");
    return exit_code;
}
