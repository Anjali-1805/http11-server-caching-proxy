/**
 * @file cache_policy.c
 * @brief Implementation of Cache-Control directive parsing and cache policy decisions.
 */

#include "cache_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void trim_whitespace(char *str)
{
    if (!str) return;
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
    if (start != str) {
        memmove(str, start, end - start + 1);
    }
}

void parse_cache_control(const char *header_val, http_cache_control_t *out_cc)
{
    if (!out_cc) return;
    memset(out_cc, 0, sizeof(*out_cc));
    out_cc->max_age = -1;

    if (!header_val || strlen(header_val) == 0) {
        return;
    }

    char copy[512];
    strncpy(copy, header_val, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(copy, ",", &saveptr);

    while (token) {
        trim_whitespace(token);

        if (strcasecmp(token, "no-store") == 0) {
            out_cc->no_store = true;
        } else if (strcasecmp(token, "no-cache") == 0) {
            out_cc->no_cache = true;
        } else if (strcasecmp(token, "private") == 0) {
            out_cc->is_private = true;
        } else if (strcasecmp(token, "public") == 0) {
            out_cc->is_public = true;
        } else if (strcasecmp(token, "must-revalidate") == 0) {
            out_cc->must_revalidate = true;
        } else if (strncasecmp(token, "max-age=", 8) == 0) {
            const char *val_str = token + 8;
            char *endptr = NULL;
            long ma = strtol(val_str, &endptr, 10);
            if (ma >= 0) {
                out_cc->max_age = (int)ma;
            }
        }

        token = strtok_r(NULL, ",", &saveptr);
    }
}

bool is_response_cacheable(http_method_t method,
                           int status_code,
                           const http_cache_control_t *cc)
{
    /* Only GET and HEAD requests may be cached */
    if (method != HTTP_METHOD_GET && method != HTTP_METHOD_HEAD) {
        return false;
    }

    /* Only 200 OK responses are eligible for caching */
    if (status_code != 200) {
        return false;
    }

    if (cc) {
        /* Responses marked no-store must NEVER be cached */
        if (cc->no_store) {
            return false;
        }

        /* Responses marked private are intended for a single user and MUST NOT be cached by a shared forward proxy */
        if (cc->is_private) {
            return false;
        }
    }

    return true;
}
