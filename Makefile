# File: Makefile
CC = gcc
CFLAGS = -Wall -Wextra -pthread -g -I./include
LDFLAGS = -lpthread

# Source files
COMMON_SRC = src/common/network.c src/common/logger.c src/common/utils.c src/common/table.c src/common/cJSON.c src/common/network_utils.c
NS_SRC = src/name_server/main.c src/name_server/file_registry.c src/name_server/handlers.c src/name_server/search.c src/name_server/ss_registry.c
SS_SRC = src/storage_server/main.c src/storage_server/file_ops.c src/storage_server/sentence.c src/storage_server/lock_registry.c src/storage_server/checkpoint.c src/storage_server/piece_table.c src/storage_server/document.c
CLIENT_SRC = src/client/main.c src/client/commands.c src/client/parser.c src/client/input.c src/client/ai_agent.c src/client/editor.c

# Targets
all: name_server storage_server client

name_server: $(NS_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o name_server $(NS_SRC) $(COMMON_SRC) $(LDFLAGS)

storage_server: $(SS_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o storage_server $(SS_SRC) $(COMMON_SRC) $(LDFLAGS)

client: $(CLIENT_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o client $(CLIENT_SRC) $(COMMON_SRC) $(LDFLAGS)

clean:
	rm -f name_server storage_server client
	rm -f src/*/*.o
	rm -rf data/* logs/*

test: all
	./tests/run_tests.sh

.PHONY: all clean test
