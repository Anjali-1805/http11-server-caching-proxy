CC = gcc
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

SERVER_OBJS = $(SERVER_SRCS:.c=.o)

.PHONY: all server http_server clean test benchmark test_proxy_lru

all: server http_server test_proxy_lru

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $(SERVER_OBJS) -o server $(LDFLAGS)

http_server: server
	cp -f server http_server

test_proxy_lru: tests/proxy/test_lru.c src/lru_cache.c
	$(CC) $(CFLAGS) tests/proxy/test_lru.c src/lru_cache.c -o test_proxy_lru $(LDFLAGS)

src/server/%.o: src/server/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: server test_proxy_lru
	@echo "--- Running Proxy LRU Unit Tests ---"
	./test_proxy_lru
	@echo "--- Running Server Test Suite (Threadpool) ---"
	python3 tests/server/test_suite.py --mode threadpool
	@echo "--- Running Server Test Suite (Eventloop) ---"
	python3 tests/server/test_suite.py --mode eventloop

benchmark: server
	python3 benchmarks/benchmark.py

clean:
	rm -f server http_server test_proxy_lru socket_learning src/server/*.o src/*.o *.o
