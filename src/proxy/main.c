/**
 * @file main.c
 * @brief Forward Caching Proxy Server entry point with CLI options and signal handling.
 */

#include "proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>

static void handle_proxy_signal(int sig)
{
    (void)sig;
    g_proxy_shutdown = 1;
}

static void print_proxy_usage(const char *prog_name)
{
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  --port <port>         Proxy listening port (default: 8081)\n");
    printf("  --cache-size <MB>     Maximum cache memory size in MB (default: 10)\n");
    printf("  --max-entries <count> Maximum number of cached items (default: 500)\n");
    printf("  --threads <num>       Number of worker threads (default: 8)\n");
    printf("  --timeout <sec>       Connection timeout in seconds (default: 5)\n");
    printf("  --help, -h            Show this help message\n\n");
    printf("Example:\n");
    printf("  %s --port 8081 --cache-size 10 --threads 8\n", prog_name);
}

int main(int argc, char *argv[])
{
    proxy_config_t config;
    config.port = 8081;
    config.cache_max_bytes = 10 * 1024 * 1024; /* 10 MB */
    config.cache_max_entries = 500;
    config.num_threads = 8;
    config.timeout_sec = 5;
    config.backlog = 128;

    static struct option long_options[] = {
        {"port",        required_argument, 0, 'p'},
        {"cache-size",  required_argument, 0, 'c'},
        {"max-entries", required_argument, 0, 'e'},
        {"threads",     required_argument, 0, 't'},
        {"timeout",     required_argument, 0, 'o'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "p:c:e:t:o:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p':
                config.port = atoi(optarg);
                if (config.port <= 0 || config.port > 65535) {
                    fprintf(stderr, "Error: Invalid port '%s'. Must be between 1 and 65535.\n", optarg);
                    return 1;
                }
                break;
            case 'c': {
                int mb = atoi(optarg);
                if (mb <= 0) {
                    fprintf(stderr, "Error: Invalid cache size '%s'. Must be positive integer.\n", optarg);
                    return 1;
                }
                config.cache_max_bytes = (size_t)mb * 1024 * 1024;
                break;
            }
            case 'e': {
                int entries = atoi(optarg);
                if (entries <= 0) {
                    fprintf(stderr, "Error: Invalid max entries '%s'. Must be positive integer.\n", optarg);
                    return 1;
                }
                config.cache_max_entries = (size_t)entries;
                break;
            }
            case 't':
                config.num_threads = atoi(optarg);
                if (config.num_threads <= 0 || config.num_threads > 256) {
                    fprintf(stderr, "Error: Invalid thread count '%s'. Must be between 1 and 256.\n", optarg);
                    return 1;
                }
                break;
            case 'o':
                config.timeout_sec = atoi(optarg);
                break;
            case 'h':
                print_proxy_usage(argv[0]);
                return 0;
            default:
                print_proxy_usage(argv[0]);
                return 1;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_proxy_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    return run_proxy_server(&config);
}
