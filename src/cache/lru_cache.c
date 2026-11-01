/**
 * @file lru_cache.c
 * @brief Thread-safe LRU Cache implementation with eviction and revalidation support.
 */

#include "lru_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Hash function (djb2) */
static unsigned long hash_url(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + (unsigned long)c; /* hash * 33 + c */
    }
    return hash % CACHE_HASH_BUCKETS;
}

lru_cache_t *lru_cache_create(size_t max_bytes, size_t max_entries)
{
    lru_cache_t *cache = calloc(1, sizeof(lru_cache_t));
    if (!cache) return NULL;

    cache->max_bytes = max_bytes > 0 ? max_bytes : DEFAULT_CACHE_MAX_BYTES;
    cache->max_entries = max_entries > 0 ? max_entries : DEFAULT_CACHE_MAX_ENTRIES;

    if (pthread_mutex_init(&cache->lock, NULL) != 0) {
        free(cache);
        return NULL;
    }

    return cache;
}

/* Helper to remove node from doubly-linked list (Must be called with cache->lock held) */
static void list_remove(lru_cache_t *cache, cache_entry_t *entry)
{
    if (!entry) return;

    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        cache->head = entry->next;
    }

    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        cache->tail = entry->prev;
    }

    entry->prev = NULL;
    entry->next = NULL;
}

/* Helper to add node to head of doubly-linked list (MRU) */
static void list_add_to_head(lru_cache_t *cache, cache_entry_t *entry)
{
    if (!entry) return;

    entry->prev = NULL;
    entry->next = cache->head;

    if (cache->head) {
        cache->head->prev = entry;
    }
    cache->head = entry;

    if (!cache->tail) {
        cache->tail = entry;
    }
}

/* Helper to free a cache entry and its allocated body */
static void free_entry_internal(cache_entry_t *entry)
{
    if (!entry) return;
    if (entry->body) {
        free(entry->body);
        entry->body = NULL;
    }
    free(entry);
}

/* Evict the tail (LRU) entry from cache (Must be called with cache->lock held) */
static bool evict_tail(lru_cache_t *cache)
{
    if (!cache->tail) return false;

    cache_entry_t *victim = cache->tail;
    list_remove(cache, victim);

    /* Remove from hash table bucket */
    unsigned long bucket = hash_url(victim->url);
    cache_entry_t **curr = &cache->buckets[bucket];
    while (*curr) {
        if (*curr == victim) {
            *curr = victim->hash_next;
            break;
        }
        curr = &((*curr)->hash_next);
    }

    size_t freed_bytes = sizeof(cache_entry_t) + victim->body_len;
    if (cache->current_bytes >= freed_bytes) {
        cache->current_bytes -= freed_bytes;
    } else {
        cache->current_bytes = 0;
    }
    cache->current_entries--;
    cache->stats.evictions++;

    free_entry_internal(victim);
    return true;
}

void lru_cache_destroy(lru_cache_t *cache)
{
    if (!cache) return;

    pthread_mutex_lock(&cache->lock);
    cache_entry_t *curr = cache->head;
    while (curr) {
        cache_entry_t *next = curr->next;
        free_entry_internal(curr);
        curr = next;
    }
    pthread_mutex_unlock(&cache->lock);

    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

cache_entry_t *lru_cache_get(lru_cache_t *cache, const char *url)
{
    if (!cache || !url) return NULL;

    pthread_mutex_lock(&cache->lock);

    unsigned long bucket = hash_url(url);
    cache_entry_t *entry = cache->buckets[bucket];

    while (entry) {
        if (strcmp(entry->url, url) == 0) {
            break;
        }
        entry = entry->hash_next;
    }

    if (!entry) {
        cache->stats.misses++;
        pthread_mutex_unlock(&cache->lock);
        return NULL;
    }

    /* Move found entry to MRU head */
    if (cache->head != entry) {
        list_remove(cache, entry);
        list_add_to_head(cache, entry);
    }

    cache->stats.hits++;

    /* Create an isolated copy for the caller to safely read without blocking the cache */
    cache_entry_t *copy = calloc(1, sizeof(cache_entry_t));
    if (copy) {
        memcpy(copy, entry, sizeof(cache_entry_t));
        copy->prev = NULL;
        copy->next = NULL;
        copy->hash_next = NULL;
        if (entry->body && entry->body_len > 0) {
            copy->body = malloc(entry->body_len);
            if (copy->body) {
                memcpy(copy->body, entry->body, entry->body_len);
            } else {
                free(copy);
                copy = NULL;
            }
        } else {
            copy->body = NULL;
        }
    }

    pthread_mutex_unlock(&cache->lock);
    return copy;
}

void cache_entry_free_copy(cache_entry_t *copy)
{
    if (!copy) return;
    if (copy->body) {
        free(copy->body);
        copy->body = NULL;
    }
    free(copy);
}

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
                   bool must_revalidate)
{
    if (!cache || !url) return false;

    size_t needed_bytes = sizeof(cache_entry_t) + body_len;
    if (needed_bytes > cache->max_bytes) {
        /* Single item exceeds entire cache capacity */
        return false;
    }

    pthread_mutex_lock(&cache->lock);

    unsigned long bucket = hash_url(url);
    cache_entry_t *existing = cache->buckets[bucket];
    while (existing) {
        if (strcmp(existing->url, url) == 0) {
            break;
        }
        existing = existing->hash_next;
    }

    /* If item already exists, remove it first */
    if (existing) {
        list_remove(cache, existing);
        cache_entry_t **curr = &cache->buckets[bucket];
        while (*curr) {
            if (*curr == existing) {
                *curr = existing->hash_next;
                break;
            }
            curr = &((*curr)->hash_next);
        }
        size_t old_bytes = sizeof(cache_entry_t) + existing->body_len;
        if (cache->current_bytes >= old_bytes) {
            cache->current_bytes -= old_bytes;
        } else {
            cache->current_bytes = 0;
        }
        cache->current_entries--;
        free_entry_internal(existing);
    }

    /* Evict LRU entries until we have sufficient space and count */
    while (cache->tail && ((cache->current_bytes + needed_bytes > cache->max_bytes) ||
                           (cache->current_entries >= cache->max_entries))) {
        evict_tail(cache);
    }

    cache_entry_t *entry = calloc(1, sizeof(cache_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&cache->lock);
        return false;
    }

    strncpy(entry->url, url, sizeof(entry->url) - 1);
    entry->status_code = status_code;
    if (content_type) {
        strncpy(entry->content_type, content_type, sizeof(entry->content_type) - 1);
    }
    entry->content_length = (int64_t)body_len;
    entry->body_len = body_len;

    if (body_len > 0 && body) {
        entry->body = malloc(body_len);
        if (!entry->body) {
            free(entry);
            pthread_mutex_unlock(&cache->lock);
            return false;
        }
        memcpy(entry->body, body, body_len);
    }

    if (etag) {
        strncpy(entry->etag, etag, sizeof(entry->etag) - 1);
    }
    if (last_modified) {
        strncpy(entry->last_modified, last_modified, sizeof(entry->last_modified) - 1);
    }

    entry->cached_at = time(NULL);
    entry->max_age = max_age;
    entry->no_cache = no_cache;
    entry->must_revalidate = must_revalidate;

    /* Insert into hash table */
    entry->hash_next = cache->buckets[bucket];
    cache->buckets[bucket] = entry;

    /* Add to MRU head */
    list_add_to_head(cache, entry);

    cache->current_bytes += needed_bytes;
    cache->current_entries++;

    pthread_mutex_unlock(&cache->lock);
    return true;
}

bool lru_cache_update_revalidated(lru_cache_t *cache,
                                 const char *url,
                                 const char *new_etag,
                                 const char *new_last_modified,
                                 int new_max_age)
{
    if (!cache || !url) return false;

    pthread_mutex_lock(&cache->lock);

    unsigned long bucket = hash_url(url);
    cache_entry_t *entry = cache->buckets[bucket];
    while (entry) {
        if (strcmp(entry->url, url) == 0) {
            break;
        }
        entry = entry->hash_next;
    }

    if (!entry) {
        pthread_mutex_unlock(&cache->lock);
        return false;
    }

    /* Update freshness timestamp and headers */
    entry->cached_at = time(NULL);
    if (new_etag && strlen(new_etag) > 0) {
        strncpy(entry->etag, new_etag, sizeof(entry->etag) - 1);
    }
    if (new_last_modified && strlen(new_last_modified) > 0) {
        strncpy(entry->last_modified, new_last_modified, sizeof(entry->last_modified) - 1);
    }
    if (new_max_age >= 0) {
        entry->max_age = new_max_age;
    }

    /* Move revalidated entry to MRU */
    if (cache->head != entry) {
        list_remove(cache, entry);
        list_add_to_head(cache, entry);
    }

    cache->stats.revalidations++;

    pthread_mutex_unlock(&cache->lock);
    return true;
}

bool cache_entry_is_fresh(const cache_entry_t *entry)
{
    if (!entry) return false;

    /* If server specified no-cache, entry is never fresh without revalidation */
    if (entry->no_cache) {
        return false;
    }

    time_t now = time(NULL);
    double age = difftime(now, entry->cached_at);

    if (entry->max_age >= 0) {
        return age < (double)entry->max_age;
    }

    /* Default freshness: 60 seconds if not specified */
    return age < 60.0;
}

void lru_cache_get_stats(lru_cache_t *cache, cache_stats_t *out_stats)
{
    if (!cache || !out_stats) return;

    pthread_mutex_lock(&cache->lock);
    *out_stats = cache->stats;
    out_stats->current_bytes = cache->current_bytes;
    out_stats->current_entries = cache->current_entries;
    pthread_mutex_unlock(&cache->lock);
}
