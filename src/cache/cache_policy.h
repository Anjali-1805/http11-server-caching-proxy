/**
 * @file cache_policy.h
 * @brief HTTP/1.1 Cache-Control directive parsing and cache eligibility rules.
 */

#ifndef CACHE_POLICY_H
#define CACHE_POLICY_H

#include "../common/http_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool no_cache;
    bool no_store;
    bool is_private;
    bool is_public;
    bool must_revalidate;
    int max_age;         /* -1 if not specified */
} http_cache_control_t;

/**
 * @brief Parse a Cache-Control header string into structured directives.
 */
void parse_cache_control(const char *header_val, http_cache_control_t *out_cc);

/**
 * @brief Determine if an HTTP response is eligible for forward proxy caching.
 *
 * Checks HTTP method, status code, and Cache-Control directives (no-store, private).
 */
bool is_response_cacheable(http_method_t method,
                           int status_code,
                           const http_cache_control_t *cc);

#ifdef __cplusplus
}
#endif

#endif /* CACHE_POLICY_H */
