/**
 * @file cache_validator.c
 * @brief Implementation of ETag/Last-Modified validation and cache revalidation.
 *
 * Contributed by Sipra for HTTP/1.1 Forward Proxy validation responsibilities.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "cache_validator.h"
#include "cache_policy.h"
#include "../server/http_response.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

bool cache_entry_needs_revalidation(const cache_entry_t *entry)
{
    if (!entry) return true;
    if (entry->no_cache) return true;
    if (!cache_entry_is_fresh(entry)) return true;
    return false;
}

bool cache_entry_has_validators(const cache_entry_t *entry)
{
    if (!entry) return false;
    return (entry->etag[0] != '\0' || entry->last_modified[0] != '\0');
}

/**
 * @brief Helper to normalize an entity tag for weak or strong comparison.
 * Strips leading "W/", whitespace, and surrounding double quotes.
 */
static void normalize_etag_token(const char *raw, char *out, size_t max_len)
{
    if (!raw || !out || max_len == 0) return;
    out[0] = '\0';

    const char *p = raw;
    while (*p && isspace((unsigned char)*p)) p++;

    /* Check and strip weak indicator W/ or w/ */
    if ((p[0] == 'W' || p[0] == 'w') && p[1] == '/') {
        p += 2;
    }

    /* Strip opening quote if present */
    if (*p == '"') p++;

    size_t i = 0;
    while (*p && *p != '"' && !isspace((unsigned char)*p) && *p != ',' && i + 1 < max_len) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

bool http_etag_matches(const char *client_inm, const char *cached_etag)
{
    if (!client_inm || !cached_etag) return false;
    if (cached_etag[0] == '\0') return false;

    /* Wildcard matches any current representation (RFC 7232 §3.1) */
    const char *p = client_inm;
    while (*p && isspace((unsigned char)*p)) p++;
    if (strcmp(p, "*") == 0) {
        return true;
    }

    char norm_cached[128];
    normalize_etag_token(cached_etag, norm_cached, sizeof(norm_cached));
    if (norm_cached[0] == '\0') return false;

    /* If-None-Match can contain a comma-separated list of tags */
    char client_copy[512];
    strncpy(client_copy, client_inm, sizeof(client_copy) - 1);
    client_copy[sizeof(client_copy) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(client_copy, ",", &saveptr);
    while (token) {
        char norm_client[128];
        normalize_etag_token(token, norm_client, sizeof(norm_client));
        if (norm_client[0] != '\0' && strcmp(norm_client, norm_cached) == 0) {
            return true;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    return false;
}

/**
 * @brief Parse an HTTP date string into a calendar time_t.
 * Supports RFC 1123 / RFC 7231, RFC 850, and asctime formats.
 */
static time_t parse_http_date(const char *str)
{
    if (!str) return (time_t)-1;
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));

    /* 1. Preferred RFC 1123: "Sun, 06 Nov 1994 08:49:37 GMT" */
    char *res = strptime(str, "%a, %d %b %Y %H:%M:%S GMT", &tm_val);
    if (res) return timegm(&tm_val);

    /* 2. RFC 850: "Sunday, 06-Nov-94 08:49:37 GMT" */
    memset(&tm_val, 0, sizeof(tm_val));
    res = strptime(str, "%A, %d-%b-%y %H:%M:%S GMT", &tm_val);
    if (res) return timegm(&tm_val);

    /* 3. ANSI C asctime: "Sun Nov  6 08:49:37 1994" */
    memset(&tm_val, 0, sizeof(tm_val));
    res = strptime(str, "%a %b %e %H:%M:%S %Y", &tm_val);
    if (res) return timegm(&tm_val);

    return (time_t)-1;
}

bool http_date_not_modified(const char *client_ims, const char *cached_lm)
{
    if (!client_ims || !cached_lm) return false;
    if (cached_lm[0] == '\0') return false;

    time_t client_time = parse_http_date(client_ims);
    time_t cached_time = parse_http_date(cached_lm);

    if (client_time == (time_t)-1 || cached_time == (time_t)-1) {
        return false;
    }

    /* Not modified if cached representation was modified on or before client IMS date */
    return (cached_time <= client_time);
}

bool cache_check_client_conditional(const http_request_t *req, const cache_entry_t *entry)
{
    if (!req || !entry) return false;

    const char *inm_val = NULL;
    const char *ims_val = NULL;

    for (size_t i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, "If-None-Match") == 0) {
            inm_val = req->headers[i].value;
        } else if (strcasecmp(req->headers[i].name, "If-Modified-Since") == 0) {
            ims_val = req->headers[i].value;
        }
    }

    /* RFC 7232 §3.3: If-None-Match takes precedence over If-Modified-Since */
    if (inm_val) {
        if (entry->etag[0] != '\0' && http_etag_matches(inm_val, entry->etag)) {
            return true;
        }
        return false;
    }

    if (ims_val) {
        if (entry->last_modified[0] != '\0' && http_date_not_modified(ims_val, entry->last_modified)) {
            return true;
        }
        return false;
    }

    return false;
}

void send_proxy_304(int client_fd, const cache_entry_t *entry, const char *cache_status, bool keep_alive)
{
    char date_buf[64];
    http_format_date(date_buf, sizeof(date_buf));

    time_t now = time(NULL);
    long age = (entry && entry->cached_at > 0) ? (long)difftime(now, entry->cached_at) : 0;
    if (age < 0) age = 0;

    char header_buf[2048];
    int hlen = snprintf(header_buf, sizeof(header_buf),
        "HTTP/1.1 304 Not Modified\r\n"
        "Date: %s\r\n"
        "Server: Caching-Proxy/1.1\r\n"
        "Via: 1.1 caching-proxy\r\n"
        "X-Cache: %s\r\n"
        "Age: %ld\r\n",
        date_buf,
        cache_status ? cache_status : "REVALIDATED",
        age);

    if (entry && entry->etag[0]) {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "ETag: %s\r\n", entry->etag);
    }
    if (entry && entry->last_modified[0]) {
        hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                         "Last-Modified: %s\r\n", entry->last_modified);
    }

    hlen += snprintf(header_buf + hlen, sizeof(header_buf) - (size_t)hlen,
                     "Connection: %s\r\n\r\n", keep_alive ? "keep-alive" : "close");

    send_all(client_fd, header_buf, (size_t)hlen);
}

bool cache_revalidate_with_upstream(lru_cache_t *cache,
                                    const upstream_target_t *target,
                                    const http_request_t *client_req,
                                    const cache_entry_t *cached_entry,
                                    upstream_response_t *out_resp,
                                    bool *out_was_304)
{
    if (!cache || !target || !client_req || !out_resp || !out_was_304) {
        return false;
    }

    *out_was_304 = false;

    int upstream_fd = upstream_connect(target->host, target->port, 5);
    if (upstream_fd < 0) {
        return false;
    }

    /* Provide conditional validators if available on the cached representation */
    const char *etag_val = (cached_entry && cached_entry->etag[0]) ? cached_entry->etag : NULL;
    const char *lm_val = (cached_entry && cached_entry->last_modified[0]) ? cached_entry->last_modified : NULL;

    if (!upstream_forward_request(upstream_fd, target, client_req, etag_val, lm_val)) {
        close(upstream_fd);
        return false;
    }

    if (!upstream_read_response(upstream_fd, out_resp)) {
        close(upstream_fd);
        return false;
    }
    close(upstream_fd);

    char canon_url[MAX_URI_LENGTH + 512];
    snprintf(canon_url, sizeof(canon_url), "http://%s:%d%s", target->host, target->port, target->path);

    if (out_resp->status_code == 304) {
        /* HTTP 304 Not Modified: origin affirms cached representation is fresh */
        *out_was_304 = true;

        http_cache_control_t cc;
        parse_cache_control(out_resp->cache_control, &cc);

        const char *new_etag = out_resp->etag[0] ? out_resp->etag : (cached_entry ? cached_entry->etag : NULL);
        const char *new_lm = out_resp->last_modified[0] ? out_resp->last_modified : (cached_entry ? cached_entry->last_modified : NULL);

        lru_cache_update_revalidated(cache, canon_url, new_etag, new_lm, cc.max_age);
    } else {
        /* Origin returned new representation (e.g. 200 OK) */
        *out_was_304 = false;

        http_cache_control_t cc;
        parse_cache_control(out_resp->cache_control, &cc);

        if (is_response_cacheable(client_req->method, out_resp->status_code, &cc)) {
            lru_cache_put(cache, canon_url, out_resp->status_code,
                          out_resp->content_type, out_resp->body, out_resp->body_len,
                          out_resp->etag, out_resp->last_modified,
                          cc.max_age, cc.no_cache, cc.must_revalidate);
        }
    }

    return true;
}
