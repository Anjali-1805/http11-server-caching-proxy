#include "../../src/lru_cache.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
	printf("[RUN] Starting LRU cache unit tests...\n");

	/* Test 1: Insert and Get */
	lru_cache_t *c = lru_cache_create(2);
	assert(c != NULL);

	cache_control_t cc = {.max_age = 60, .no_store = false, .no_cache = false, .is_private = false, .is_public = true};
	int put_res = lru_cache_put(c, "GET:localhost:/file", 200, "Header: val\r\n\r\n", 15, "BodyContent", 11, &cc, "\"etag-1\"", NULL);
	assert(put_res == 1);

	cache_entry_t *entry = lru_cache_get(c, "GET:localhost:/file");
	assert(entry != NULL);
	assert(entry->status_code == 200);
	assert(entry->body_len == 11);
	assert(memcmp(entry->body, "BodyContent", 11) == 0);
	assert(strcmp(entry->etag, "\"etag-1\"") == 0);
	lru_cache_release(c);

	/* Test 2: Eviction Order (Capacity = 2) */
	lru_cache_put(c, "GET:localhost:/file2", 200, "H\r\n\r\n", 5, "B2", 2, &cc, NULL, NULL);
	lru_cache_put(c, "GET:localhost:/file3", 200, "H\r\n\r\n", 5, "B3", 2, &cc, NULL, NULL);

	/* /file must be evicted because capacity is 2 */
	assert(lru_cache_get(c, "GET:localhost:/file") == NULL);
	assert(lru_cache_get(c, "GET:localhost:/file2") != NULL);
	lru_cache_release(c);
	assert(lru_cache_get(c, "GET:localhost:/file3") != NULL);
	lru_cache_release(c);
	lru_cache_destroy(c);

	/* Test 3: Edge Case Capacity = 1 */
	c = lru_cache_create(1);
	assert(c != NULL);
	lru_cache_put(c, "GET:k:1", 200, "H", 1, "V1", 2, &cc, NULL, NULL);
	entry = lru_cache_get(c, "GET:k:1");
	assert(entry != NULL);
	lru_cache_release(c);

	lru_cache_put(c, "GET:k:2", 200, "H", 1, "V2", 2, &cc, NULL, NULL);
	assert(lru_cache_get(c, "GET:k:1") == NULL);
	entry = lru_cache_get(c, "GET:k:2");
	assert(entry != NULL);
	lru_cache_release(c);
	lru_cache_destroy(c);

	/* Test 4: Cache-Control Bypass (no-store & private) */
	c = lru_cache_create(2);
	cache_control_t cc_nostore = {.no_store = true};
	int res_ns = lru_cache_put(c, "GET:k:ns", 200, "H", 1, "V", 1, &cc_nostore, NULL, NULL);
	assert(res_ns == 0);
	assert(lru_cache_get(c, "GET:k:ns") == NULL);

	cache_control_t cc_private = {.is_private = true};
	int res_pr = lru_cache_put(c, "GET:k:pr", 200, "H", 1, "V", 1, &cc_private, NULL, NULL);
	assert(res_pr == 0);
	assert(lru_cache_get(c, "GET:k:pr") == NULL);
	lru_cache_destroy(c);

	printf("[PASS] All LRU cache unit tests passed.\n");
	return 0;
}
