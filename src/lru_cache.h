#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

typedef struct cache_control {
	bool no_store;
	bool no_cache;
	bool is_private;
	bool is_public;
	int max_age; /* in seconds; -1 if not specified */
} cache_control_t;

typedef struct cache_entry {
	char *key;                 /* "METHOD:host:path" */
	int status_code;
	char *headers;
	size_t headers_len;
	char *body;
	size_t body_len;
	time_t timestamp;
	cache_control_t cc;
	char *etag;
	char *last_modified;

	/* Doubly-linked list pointers for LRU order */
	struct cache_entry *prev;
	struct cache_entry *next;

	/* Hash bucket chain */
	struct cache_entry *hnext;
} cache_entry_t;

typedef struct lru_cache {
	size_t capacity;
	size_t count;
	cache_entry_t *head;      /* Most recently used */
	cache_entry_t *tail;      /* Least recently used */
	cache_entry_t **buckets;
	size_t num_buckets;
	pthread_mutex_t lock;
} lru_cache_t;

/* Header parsing & key utilities */
void parse_cache_control(const char *header_val, cache_control_t *out_cc);
char *cache_build_key(const char *method, const char *host, const char *path);

/* Lifecycle */
lru_cache_t *lru_cache_create(size_t capacity);
void lru_cache_destroy(lru_cache_t *cache);

/* Put & Get operations */
int lru_cache_put(lru_cache_t *cache, const char *key, int status,
				  const char *headers, size_t hlen,
				  const char *body, size_t blen,
				  const cache_control_t *cc,
				  const char *etag, const char *last_modified);

/* Returns a borrowed pointer while holding internal lock or caller releases via lru_cache_release */
cache_entry_t *lru_cache_get(lru_cache_t *cache, const char *key);
void lru_cache_release(lru_cache_t *cache);

#endif /* LRU_CACHE_H */
