/**
 * @file lru_cache.h
 * @brief Thread-safe LRU Cache implementation for HTTP responses.
 */

#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include "../common/http_types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CACHE_HASH_BUCKETS 1024
#define DEFAULT_CACHE_MAX_BYTES (10 * 1024 * 1024) /* 10 MB */
#define DEFAULT_CACHE_MAX_ENTRIES 500

/**
 * @brief Representation of a cached HTTP response.
 */
typedef struct cache_entry {
    char url[MAX_URI_LENGTH];              /* Lookup key: canonical absolute URI */
    int status_code;
    char content_type[128];
    int64_t content_length;
    
    char *body;                            /* Cached payload */
    size_t body_len;

    /* HTTP Caching metadata */
    char etag[128];
    char last_modified[128];
    char date[64];
    time_t cached_at;                      /* Wall clock time cached */
    int max_age;                           /* max-age in seconds, -1 if not set */
    bool no_cache;                         /* Must revalidate before serving */
    bool must_revalidate;
    
    /* Doubly linked list for LRU eviction order */
    struct cache_entry *prev;
    struct cache_entry *next;

    /* Hash table chaining for collision resolution */
    struct cache_entry *hash_next;
} cache_entry_t;

/**
 * @brief Cache statistics.
 */
typedef struct {
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t revalidations;
    size_t current_bytes;
    size_t current_entries;
} cache_stats_t;

/**
 * @brief LRU Cache container.
 */
typedef struct {
    cache_entry_t *buckets[CACHE_HASH_BUCKETS];
    cache_entry_t *head;                   /* MRU (Most Recently Used) */
    cache_entry_t *tail;                   /* LRU (Least Recently Used) */

    size_t max_bytes;
    size_t current_bytes;
    size_t max_entries;
    size_t current_entries;

    pthread_mutex_t lock;
    cache_stats_t stats;
} lru_cache_t;

/**
 * @brief Create and initialize a new thread-safe LRU cache.
 */
lru_cache_t *lru_cache_create(size_t max_bytes, size_t max_entries);

/**
 * @brief Destroy LRU cache and free all allocated entries.
 */
void lru_cache_destroy(lru_cache_t *cache);

/**
 * @brief Look up an entry by URL.
 * 
 * If found and valid, moves entry to MRU position.
 * Caller receives a duplicated copy of the entry to prevent race conditions during reads.
 * Caller MUST free the returned entry using cache_entry_free_copy().
 */
cache_entry_t *lru_cache_get(lru_cache_t *cache, const char *url);

/**
 * @brief Free an entry copy returned by lru_cache_get.
 */
void cache_entry_free_copy(cache_entry_t *copy);

/**
 * @brief Insert or update an entry in the cache.
 * 
 * Performs LRU eviction if size or entry limit is exceeded.
 */
bool lru_cache_put(lru_cache_t *cache,
                   const char *url,
                   int status_code,
                   const char *content_type,
                   const char *body,
                   size_t body_len,
                   const char *etag,
                   const char *last_modified,
                   int max_age,
                   bool no_cache,
                   bool must_revalidate);

/**
 * @brief Update caching headers/freshness of an existing cached entry upon receiving 304 Not Modified.
 */
bool lru_cache_update_revalidated(lru_cache_t *cache,
                                 const char *url,
                                 const char *new_etag,
                                 const char *new_last_modified,
                                 int new_max_age);

/**
 * @brief Check if a cached entry is currently fresh.
 */
bool cache_entry_is_fresh(const cache_entry_t *entry);

/**
 * @brief Retrieve cache statistics.
 */
void lru_cache_get_stats(lru_cache_t *cache, cache_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* LRU_CACHE_H */
