# HTTP LRU Cache Module

Maintainers: Sipra & Akshat

## Responsibilities

- [x] Store eligible HTTP responses for reuse using an in-memory Hash Table and Doubly Linked List (MRU to LRU).
- [x] Implement memory-bounded LRU eviction when cache capacity or entry limits are exceeded.
- [x] Respect HTTP cache directives:
  - `Cache-Control: no-store` $\rightarrow$ Never cached.
  - `Cache-Control: private` $\rightarrow$ Never cached by shared proxy.
  - `Cache-Control: no-cache` $\rightarrow$ Must revalidate before serving.
  - `Cache-Control: max-age=N` $\rightarrow$ Fresh for $N$ seconds.
- [x] Support conditional requests and revalidation with validators (`ETag`, `Last-Modified`).
- [x] Handle Cache HIT, Cache MISS, and `304 Not Modified` revalidations.
- [x] Thread-safe concurrency: all cache operations protected by `pthread_mutex_t`.
- [x] Cache telemetry & statistics tracking (hits, misses, evictions, revalidations).

## Development Status

Implementation complete, verified, and tested against all assignment requirements.
