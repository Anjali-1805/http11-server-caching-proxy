CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g -D_GNU_SOURCE -Isrc/common -Isrc/server
LDFLAGS = -pthread

SERVER_SRCS = src/server/main.c \
              src/server/server.c \
              src/server/http_parser.c \
              src/server/http_response.c \
              src/server/file_handler.c \
              src/server/thread_pool.c \
              src/server/event_loop.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)

.PHONY: all server http_server clean test benchmark

all: server http_server

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $(SERVER_OBJS) -o server $(LDFLAGS)

http_server: server
	cp -f server http_server

src/server/%.o: src/server/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: server
	python3 tests/server/test_suite.py

benchmark: server
	python3 benchmarks/benchmark.py

clean:
	rm -f server http_server socket_learning src/server/*.o *.o
