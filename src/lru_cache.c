#define _POSIX_C_SOURCE 200809L
#include "lru_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NUM_BUCKETS 1024

static uint32_t hash_str(const char *str) {
	uint32_t hash = 5381;
	int c;
	while ((c = (unsigned char)*str++)) {
		hash = ((hash << 5) + hash) + c;
	}
	return hash % NUM_BUCKETS;
}

void parse_cache_control(const char *header_val, cache_control_t *out_cc) {
	if (!header_val || !out_cc) return;
	memset(out_cc, 0, sizeof(*out_cc));
	out_cc->max_age = -1;

	char *dup = strdup(header_val);
	if (!dup) return;

	char *saveptr = NULL;
	char *token = strtok_r(dup, ",", &saveptr);
	while (token) {
		while (isspace((unsigned char)*token)) token++;
		char *end = token + strlen(token) - 1;
		while (end > token && isspace((unsigned char)*end)) *end-- = '\0';

		if (strcasecmp(token, "no-store") == 0) {
			out_cc->no_store = true;
		} else if (strcasecmp(token, "no-cache") == 0) {
			out_cc->no_cache = true;
		} else if (strcasecmp(token, "private") == 0) {
			out_cc->is_private = true;
		} else if (strcasecmp(token, "public") == 0) {
			out_cc->is_public = true;
		} else if (strncasecmp(token, "max-age=", 8) == 0) {
			out_cc->max_age = atoi(token + 8);
		}
		token = strtok_r(NULL, ",", &saveptr);
	}
	free(dup);
}

char *cache_build_key(const char *method, const char *host, const char *path) {
	if (!method || !host || !path) return NULL;
	size_t len = strlen(method) + strlen(host) + strlen(path) + 3;
	char *buf = malloc(len);
	if (buf) {
		snprintf(buf, len, "%s:%s:%s", method, host, path);
	}
	return buf;
}

lru_cache_t *lru_cache_create(size_t capacity) {
	if (capacity == 0) return NULL;
	lru_cache_t *cache = calloc(1, sizeof(lru_cache_t));
	if (!cache) return NULL;

	cache->capacity = capacity;
	cache->num_buckets = NUM_BUCKETS;
	cache->buckets = calloc(cache->num_buckets, sizeof(cache_entry_t *));
	if (!cache->buckets) {
		free(cache);
		return NULL;
	}
	if (pthread_mutex_init(&cache->lock, NULL) != 0) {
		free(cache->buckets);
		free(cache);
		return NULL;
	}
	return cache;
}

static void free_entry(cache_entry_t *e) {
	if (!e) return;
	free(e->key);
	free(e->headers);
	free(e->body);
	free(e->etag);
	free(e->last_modified);
	free(e);
}

static void remove_node(lru_cache_t *cache, cache_entry_t *node) {
	if (node->prev) {
		node->prev->next = node->next;
	} else {
		cache->head = node->next;
	}

	if (node->next) {
		node->next->prev = node->prev;
	} else {
		cache->tail = node->prev;
	}
}

static void push_front(lru_cache_t *cache, cache_entry_t *node) {
	node->prev = NULL;
	node->next = cache->head;
	if (cache->head) {
		cache->head->prev = node;
	}
	cache->head = node;
	if (!cache->tail) {
		cache->tail = node;
	}
}

static void evict_tail(lru_cache_t *cache) {
	cache_entry_t *old = cache->tail;
	if (!old) return;
	remove_node(cache, old);

	uint32_t b = hash_str(old->key);
	cache_entry_t **curr = &cache->buckets[b];
	while (*curr && *curr != old) {
		curr = &((*curr)->hnext);
	}
	if (*curr) {
		*curr = old->hnext;
	}

	free_entry(old);
	cache->count--;
}

int lru_cache_put(lru_cache_t *cache, const char *key, int status,
				  const char *headers, size_t hlen,
				  const char *body, size_t blen,
				  const cache_control_t *cc,
				  const char *etag, const char *last_modified) {
	if (!cache || !key) return -1;
	/* Cache-Control: no-store and private responses must not be cached by a shared proxy */
	if (cc && (cc->no_store || cc->is_private)) return 0;

	pthread_mutex_lock(&cache->lock);

	uint32_t b = hash_str(key);
	cache_entry_t *e = cache->buckets[b];
	while (e) {
		if (strcmp(e->key, key) == 0) {
			remove_node(cache, e);
			break;
		}
		e = e->hnext;
	}

	if (!e) {
		if (cache->count >= cache->capacity) {
			evict_tail(cache);
		}
		e = calloc(1, sizeof(cache_entry_t));
		if (!e) {
			pthread_mutex_unlock(&cache->lock);
			return -1;
		}
		e->key = strdup(key);
		e->hnext = cache->buckets[b];
		cache->buckets[b] = e;
		cache->count++;
	} else {
		free(e->headers);
		free(e->body);
		free(e->etag);
		free(e->last_modified);
	}

	e->status_code = status;
	e->headers = headers ? strdup(headers) : NULL;
	e->headers_len = hlen;
	if (body && blen > 0) {
		e->body = malloc(blen);
		if (e->body) memcpy(e->body, body, blen);
	} else {
		e->body = NULL;
	}
	e->body_len = blen;
	e->timestamp = time(NULL);
	if (cc) e->cc = *cc;
	e->etag = etag ? strdup(etag) : NULL;
	e->last_modified = last_modified ? strdup(last_modified) : NULL;

	push_front(cache, e);
	pthread_mutex_unlock(&cache->lock);
	return 1;
}

cache_entry_t *lru_cache_get(lru_cache_t *cache, const char *key) {
	if (!cache || !key) return NULL;
	pthread_mutex_lock(&cache->lock);

	uint32_t b = hash_str(key);
	cache_entry_t *e = cache->buckets[b];
	while (e) {
		if (strcmp(e->key, key) == 0) {
			remove_node(cache, e);
			push_front(cache, e);
			return e; /* Lock remains held until lru_cache_release is called */
		}
		e = e->hnext;
	}
	pthread_mutex_unlock(&cache->lock);
	return NULL;
}

void lru_cache_release(lru_cache_t *cache) {
	if (cache) {
		pthread_mutex_unlock(&cache->lock);
	}
}

void lru_cache_destroy(lru_cache_t *cache) {
	if (!cache) return;
	pthread_mutex_lock(&cache->lock);
	cache_entry_t *curr = cache->head;
	while (curr) {
		cache_entry_t *next = curr->next;
		free_entry(curr);
		curr = next;
	}
	free(cache->buckets);
	pthread_mutex_unlock(&cache->lock);
	pthread_mutex_destroy(&cache->lock);
	free(cache);
}
