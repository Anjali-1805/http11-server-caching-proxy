CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g -D_GNU_SOURCE -Isrc/common -Isrc/server -Isrc/proxy -Isrc/cache
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g -D_GNU_SOURCE -Isrc -Isrc/common -Isrc/server
LDFLAGS = -pthread

SERVER_SRCS = src/server/main.c \
              src/server/server.c \
              src/server/http_parser.c \
              src/server/http_response.c \
              src/server/file_handler.c \
              src/server/thread_pool.c \
              src/server/event_loop.c \
              src/lru_cache.c \
              src/proxy.c

PROXY_SRCS = src/proxy/main.c \
             src/proxy/proxy.c \
             src/proxy/upstream.c \
             src/cache/lru_cache.c \
             src/cache/cache_policy.c \
             src/cache/cache_validator.c \
             src/server/server.c \
             src/server/http_parser.c \
             src/server/http_response.c \
             src/server/file_handler.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
PROXY_OBJS  = $(PROXY_SRCS:.c=.o)

.PHONY: all server http_server proxy caching_proxy clean test test_proxy benchmark test_validation benchmark_validation

all: server http_server proxy caching_proxy
.PHONY: all server http_server clean test benchmark test_proxy_lru

all: server http_server test_proxy_lru

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $(SERVER_OBJS) -o server $(LDFLAGS)

http_server: server
	cp -f server http_server

proxy: $(PROXY_OBJS)
	$(CC) $(CFLAGS) $(PROXY_OBJS) -o proxy $(LDFLAGS)

caching_proxy: proxy
	cp -f proxy caching_proxy
test_proxy_lru: tests/proxy/test_lru.c src/lru_cache.c
	$(CC) $(CFLAGS) tests/proxy/test_lru.c src/lru_cache.c -o test_proxy_lru $(LDFLAGS)

src/server/%.o: src/server/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/proxy/%.o: src/proxy/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/cache/%.o: src/cache/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: server
	python3 tests/server/test_suite.py
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: server test_proxy_lru
	@echo "--- Running Proxy LRU Unit Tests ---"
	./test_proxy_lru
	@echo "--- Running Server Test Suite (Threadpool) ---"
	python3 tests/server/test_suite.py --mode threadpool
	@echo "--- Running Server Test Suite (Eventloop) ---"
	python3 tests/server/test_suite.py --mode eventloop

test_proxy: server proxy
	python3 tests/proxy/test_proxy.py

benchmark: server
	python3 benchmarks/benchmark.py

test_validation: proxy
	./tests/proxy/test_validation.sh

benchmark_validation: proxy
	./benchmarks/run_benchmarks.sh

clean:
	rm -f server http_server proxy caching_proxy socket_learning src/server/*.o src/proxy/*.o src/cache/*.o *.o
	rm -f server http_server test_proxy_lru socket_learning src/server/*.o src/*.o *.o
