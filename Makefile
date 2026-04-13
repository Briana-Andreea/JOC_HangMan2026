# Makefile
CC     = gcc
CFLAGS = -Wall -O2 -pthread -I./common

# Calea unde ai instalat Raylib
RAYLIB_FLAGS = $(shell pkg-config --libs --cflags raylib 2>/dev/null || \
               echo "-I/usr/local/include -L/usr/local/lib -lraylib -lm -ldl -lpthread")

.PHONY: all server client clean

all: server client

server: server/server.c common/protocol.h
	$(CC) $(CFLAGS) server/server.c -o server/server

client: client/client.c common/protocol.h
	$(CC) $(CFLAGS) client/client.c $(RAYLIB_FLAGS) -o client/client

clean:
	rm -f server/server client/client
