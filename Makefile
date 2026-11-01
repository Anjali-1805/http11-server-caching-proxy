CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g -D_GNU_SOURCE -Isrc/common -Isrc/server -Isrc/proxy -Isrc/cache
LDFLAGS = -pthread

SERVER_SRCS = src/server/main.c \
              src/server/server.c \
              src/server/http_parser.c \
              src/server/http_response.c \
              src/server/file_handler.c \
              src/server/thread_pool.c \
              src/server/event_loop.c

PROXY_SRCS = src/proxy/main.c \
             src/proxy/proxy.c \
             src/proxy/upstream.c \
             src/cache/lru_cache.c \
             src/cache/cache_policy.c \
             src/server/server.c \
             src/server/http_parser.c \
             src/server/http_response.c \
             src/server/file_handler.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
PROXY_OBJS  = $(PROXY_SRCS:.c=.o)

.PHONY: all server http_server proxy caching_proxy clean test test_proxy benchmark

all: server http_server proxy caching_proxy

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $(SERVER_OBJS) -o server $(LDFLAGS)

http_server: server
	cp -f server http_server

proxy: $(PROXY_OBJS)
	$(CC) $(CFLAGS) $(PROXY_OBJS) -o proxy $(LDFLAGS)

caching_proxy: proxy
	cp -f proxy caching_proxy

src/server/%.o: src/server/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/proxy/%.o: src/proxy/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/cache/%.o: src/cache/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: server
	python3 tests/server/test_suite.py

test_proxy: server proxy
	python3 tests/proxy/test_proxy.py

benchmark: server
	python3 benchmarks/benchmark.py

clean:
	rm -f server http_server proxy caching_proxy socket_learning src/server/*.o src/proxy/*.o src/cache/*.o *.o
