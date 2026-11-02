/**
 * @file mock_origin.c
 * @brief Lightweight C HTTP origin server for proxy validation testing (No Python).
 *
 * Contributed by Sipra for automated proxy revalidation testing.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void send_all(int fd, const char *buf, size_t len)
{
    size_t rem = len;
    while (rem > 0) {
        ssize_t n = send(fd, buf, rem, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        buf += n;
        rem -= (size_t)n;
    }
}

int main(int argc, char *argv[])
{
    int port = 18090;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 64) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    /* Dynamic state for tests */
    int etag_version = 1;
    int lm_version = 1;

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        char buf[4096];
        ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            close(client_fd);
            continue;
        }
        buf[n] = '\0';

        char method[16] = {0};
        char uri[256] = {0};
        sscanf(buf, "%15s %255s", method, uri);

        /* Check for conditional headers in request */
        char inm[128] = {0};
        char ims[128] = {0};

        char *line = strtok(buf, "\r\n");
        while (line) {
            if (strncasecmp(line, "If-None-Match:", 14) == 0) {
                const char *val = line + 14;
                while (*val == ' ') val++;
                strncpy(inm, val, sizeof(inm) - 1);
            } else if (strncasecmp(line, "If-Modified-Since:", 18) == 0) {
                const char *val = line + 18;
                while (*val == ' ') val++;
                strncpy(ims, val, sizeof(ims) - 1);
            }
            line = strtok(NULL, "\r\n");
        }

        char resp[2048];
        int resp_len = 0;

        /* Route handling */
        if (strstr(uri, "/etag-update")) {
            etag_version++;
            resp_len = snprintf(resp, sizeof(resp),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 12\r\n"
                "Connection: close\r\n\r\n"
                "ETag updated");
        } else if (strstr(uri, "/lm-update")) {
            lm_version++;
            resp_len = snprintf(resp, sizeof(resp),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 15\r\n"
                "Connection: close\r\n\r\n"
                "Last-Mod updated");
        } else if (strstr(uri, "/etag-resource")) {
            char current_etag[64];
            snprintf(current_etag, sizeof(current_etag), "\"etag-v%d\"", etag_version);

            if (inm[0] && strstr(inm, current_etag)) {
                /* 304 Not Modified */
                resp_len = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 304 Not Modified\r\n"
                    "ETag: %s\r\n"
                    "Cache-Control: max-age=1\r\n"
                    "Connection: close\r\n\r\n",
                    current_etag);
            } else {
                /* 200 OK */
                char body[128];
                int blen = snprintf(body, sizeof(body), "ETag representation version %d\n", etag_version);
                resp_len = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %d\r\n"
                    "ETag: %s\r\n"
                    "Cache-Control: max-age=1\r\n"
                    "Connection: close\r\n\r\n%s",
                    blen, current_etag, body);
            }
        } else if (strstr(uri, "/lastmod-resource")) {
            const char *lm_date = (lm_version == 1) ?
                "Sun, 01 Nov 2026 12:00:00 GMT" :
                "Sun, 01 Nov 2026 14:00:00 GMT";

            if (ims[0] && strcmp(ims, lm_date) == 0 && lm_version == 1) {
                /* 304 Not Modified */
                resp_len = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 304 Not Modified\r\n"
                    "Last-Modified: %s\r\n"
                    "Cache-Control: max-age=1\r\n"
                    "Connection: close\r\n\r\n",
                    lm_date);
            } else {
                /* 200 OK */
                char body[128];
                int blen = snprintf(body, sizeof(body), "Last-Modified representation version %d\n", lm_version);
                resp_len = snprintf(resp, sizeof(resp),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %d\r\n"
                    "Last-Modified: %s\r\n"
                    "Cache-Control: max-age=1\r\n"
                    "Connection: close\r\n\r\n%s",
                    blen, lm_date, body);
            }
        } else if (strstr(uri, "/novalidator-resource")) {
            /* Response without ETag and without Last-Modified */
            resp_len = snprintf(resp, sizeof(resp),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 26\r\n"
                "Cache-Control: max-age=1\r\n"
                "Connection: close\r\n\r\n"
                "No validator resource data\n");
        } else if (strstr(uri, "/error-resource")) {
            /* Simulate upstream 500 error */
            resp_len = snprintf(resp, sizeof(resp),
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 21\r\n"
                "Connection: close\r\n\r\n"
                "Internal Server Error\n");
        } else {
            /* Standard endpoint */
            resp_len = snprintf(resp, sizeof(resp),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: 35\r\n"
                "ETag: \"standard-etag\"\r\n"
                "Cache-Control: max-age=10\r\n"
                "Connection: close\r\n\r\n"
                "<html><body>Origin OK</body></html>\n");
        }

        if (resp_len > 0) {
            send_all(client_fd, resp, (size_t)resp_len);
        }
        close(client_fd);
    }

    close(listen_fd);
    return 0;
}
