#define _POSIX_C_SOURCE 200809L
#include "proxy.h"
#include "lru_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* Upstream forwarding stub. In full implementation, this opens a socket to host:port. */
static int stub_upstream_forward(const char *host, int port, const char *req, size_t rlen,
								 char **res, size_t *res_len) {
	(void)host;
	(void)port;
	(void)req;
	(void)rlen;

	/* Mock response for scaffolding and verification */
	const char *mock_res =
		"HTTP/1.1 200 OK\r\n"
		"Content-Length: 14\r\n"
		"Cache-Control: max-age=60\r\n"
		"ETag: \"stub-etag-v1\"\r\n"
		"Content-Type: text/plain\r\n\r\n"
		"Hello Upstream";

	*res_len = strlen(mock_res);
	*res = malloc(*res_len + 1);
	if (!*res) return -1;
	memcpy(*res, mock_res, *res_len);
	(*res)[*res_len] = '\0';
	return 0;
}

static void extract_header_val(const char *req, const char *header_name, char *out, size_t max_out) {
	out[0] = '\0';
	char pattern[128];
	snprintf(pattern, sizeof(pattern), "\r\n%s:", header_name);

	const char *loc = strstr(req, pattern);
	if (!loc && strncasecmp(req, header_name, strlen(header_name)) == 0 && req[strlen(header_name)] == ':') {
		loc = req;
	} else if (loc) {
		loc += 2;
	}

	if (loc) {
		const char *colon = strchr(loc, ':');
		if (colon) {
			colon++;
			while (*colon == ' ' || *colon == '\t') colon++;
			const char *crlf = strstr(colon, "\r\n");
			if (crlf) {
				size_t len = (size_t)(crlf - colon);
				if (len >= max_out) len = max_out - 1;
				strncpy(out, colon, len);
				out[len] = '\0';
			}
		}
	}
}

int proxy_handle_client(int client_fd, lru_cache_t *cache) {
	char buf[8192];
	ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
	if (n <= 0) return -1;
	buf[n] = '\0';

	char method[16] = {0}, path[1024] = {0}, version[16] = {0}, host[256] = "localhost";
	if (sscanf(buf, "%15s %1023s %15s", method, path, version) < 3) {
		const char *bad_req = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
		send(client_fd, bad_req, strlen(bad_req), 0);
		return -1;
	}

	extract_header_val(buf, "Host", host, sizeof(host));

	char *key = cache_build_key(method, host, path);
	if (!key) return -1;

	cache_entry_t *entry = lru_cache_get(cache, key);
	if (entry) {
		time_t now = time(NULL);
		bool is_stale = (entry->cc.max_age >= 0 && (now - entry->timestamp) > entry->cc.max_age);

		/* Fresh Cache Hit */
		if (!is_stale && !entry->cc.no_cache) {
			if (entry->headers && entry->headers_len > 0) {
				send(client_fd, entry->headers, entry->headers_len, 0);
			}
			if (entry->body && entry->body_len > 0) {
				send(client_fd, entry->body, entry->body_len, 0);
			}
			lru_cache_release(cache);
			free(key);
			return 0;
		}

		/* Stale or no-cache: Requires upstream revalidation */
		/* Build conditional forwarding request using If-None-Match / If-Modified-Since */
		lru_cache_release(cache);
	}

	/* Cache Miss / Revalidation: Forward upstream */
	char *upstream_res = NULL;
	size_t upstream_len = 0;
	if (stub_upstream_forward(host, 80, buf, (size_t)n, &upstream_res, &upstream_len) == 0) {
		send(client_fd, upstream_res, upstream_len, 0);

		/* Parse response and store in cache */
		char *body_delim = strstr(upstream_res, "\r\n\r\n");
		if (body_delim) {
			size_t hlen = (size_t)(body_delim + 4 - upstream_res);
			const char *body = body_delim + 4;
			size_t blen = upstream_len - hlen;

			char cc_hdr[256] = {0};
			char etag_hdr[128] = {0};
			char lm_hdr[128] = {0};

			extract_header_val(upstream_res, "Cache-Control", cc_hdr, sizeof(cc_hdr));
			extract_header_val(upstream_res, "ETag", etag_hdr, sizeof(etag_hdr));
			extract_header_val(upstream_res, "Last-Modified", lm_hdr, sizeof(lm_hdr));

			cache_control_t cc;
			parse_cache_control(cc_hdr, &cc);

			lru_cache_put(cache, key, 200, upstream_res, hlen, body, blen,
						  &cc, etag_hdr[0] ? etag_hdr : NULL,
						  lm_hdr[0] ? lm_hdr : NULL);
		}
		free(upstream_res);
	}

	free(key);
	return 0;
}

int run_proxy_server(int port, size_t cache_capacity) {
	lru_cache_t *cache = lru_cache_create(cache_capacity);
	if (!cache) {
		fprintf(stderr, "Failed to initialize LRU cache\n");
		return 1;
	}

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		perror("socket");
		lru_cache_destroy(cache);
		return 1;
	}

	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind proxy");
		close(server_fd);
		lru_cache_destroy(cache);
		return 1;
	}

	if (listen(server_fd, 128) < 0) {
		perror("listen proxy");
		close(server_fd);
		lru_cache_destroy(cache);
		return 1;
	}

	printf("Proxy running on port %d with cache capacity %zu...\n", port, cache_capacity);

	while (1) {
		int client = accept(server_fd, NULL, NULL);
		if (client >= 0) {
			proxy_handle_client(client, cache);
			close(client);
		}
	}

	close(server_fd);
	lru_cache_destroy(cache);
	return 0;
}
