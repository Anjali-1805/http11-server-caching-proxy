/**
 * @file cache_validator.h
 * @brief ETag and Last-Modified validation, conditional GET, and cache revalidation.
 *
 * Contributed by Sipra for HTTP/1.1 Forward Proxy validation responsibilities.
 */

#ifndef CACHE_VALIDATOR_H
#define CACHE_VALIDATOR_H

#include "../common/http_types.h"
#include "../proxy/upstream.h"
#include "lru_cache.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check whether a cached entry requires revalidation with the origin server.
 *
 * An entry requires revalidation if:
 * 1. It is stale (age >= max_age).
 * 2. It contains the no-cache directive.
 * 3. It is expired and must-revalidate is set.
 */
bool cache_entry_needs_revalidation(const cache_entry_t *entry);

/**
 * @brief Check if a cached entry possesses at least one validator (ETag or Last-Modified).
 */
bool cache_entry_has_validators(const cache_entry_t *entry);

/**
 * @brief Compare a client's If-None-Match header value against a cached ETag.
 *
 * Supports wildcard (*), exact match, and weak validation (RFC 7232 §3.1).
 */
bool http_etag_matches(const char *client_inm, const char *cached_etag);

/**
 * @brief Compare a client's If-Modified-Since header against a cached Last-Modified timestamp.
 *
 * Evaluates RFC 1123 / RFC 7231 date format. Returns true if cached resource has NOT
 * been modified since the specified timestamp (RFC 7232 §3.3).
 */
bool http_date_not_modified(const char *client_ims, const char *cached_lm);

/**
 * @brief Evaluate client-supplied conditional validators against a cached entry.
 *
 * @param req Client HTTP request.
 * @param entry Cached entry.
 * @return true if the cached representation satisfies the client's condition (indicating 304 Not Modified).
 */
bool cache_check_client_conditional(const http_request_t *req, const cache_entry_t *entry);

/**
 * @brief Transmit an HTTP 304 Not Modified response to the client.
 *
 * Emits required headers (Date, ETag, Last-Modified, Age, Via, X-Cache) with no message body.
 */
void send_proxy_304(int client_fd, const cache_entry_t *entry, const char *cache_status, bool keep_alive);

/**
 * @brief Execute a conditional revalidation request to the upstream origin server.
 *
 * Sends If-None-Match and/or If-Modified-Since validators if available.
 * Handles:
 * - Upstream 304 Not Modified: refreshes cache metadata and preserves cached body.
 * - Upstream 200 OK: stores the updated resource in LRU cache.
 * - Upstream failure / 5xx: returns false so proxy can serve 502 Bad Gateway.
 *
 * @param cache LRU cache instance.
 * @param target Upstream target details (host, port, path).
 * @param client_req Client request.
 * @param cached_entry Existing cached entry copy (to supply validators).
 * @param out_resp Populated with upstream response on 200 or 304.
 * @param out_was_304 Set to true if upstream responded with 304 Not Modified.
 * @return true on successful exchange, false on connection/network failure.
 */
bool cache_revalidate_with_upstream(lru_cache_t *cache,
                                    const upstream_target_t *target,
                                    const http_request_t *client_req,
                                    const cache_entry_t *cached_entry,
                                    upstream_response_t *out_resp,
                                    bool *out_was_304);

#ifdef __cplusplus
}
#endif

#endif /* CACHE_VALIDATOR_H */
