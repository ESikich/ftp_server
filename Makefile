# Makefile - build the FTP server and integration tests.

CC      ?= cc
CFLAGS  ?= -std=c2x -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS ?= -D_GNU_SOURCE -Iinclude -Isrc
LDFLAGS ?=
LDLIBS  ?=

BUILD_DIR  := build
BIN        := $(BUILD_DIR)/ftp-server

CLIENT_DIR  := ../ftp_client
CLIENT_INC  := -I$(CLIENT_DIR)/include
CLIENT_OBJ  := \
    $(CLIENT_DIR)/build/ftp_conn.o \
    $(CLIENT_DIR)/build/ftp_data.o \
    $(CLIENT_DIR)/build/ftp_reply.o \
    $(CLIENT_DIR)/build/ftp_session.o

SRC := \
    src/main.c \
    src/ftp_cmd.c \
    src/ftp_data.c \
    src/ftp_log.c \
    src/ftp_path.c \
    src/ftp_reply.c \
    src/ftp_session.c

OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

TEST_SRCS := \
    tests/test_server_greeting.c \
    tests/test_server_auth.c \
    tests/test_server_auth_fail.c \
    tests/test_server_list.c \
    tests/test_server_retr.c \
    tests/test_server_stor.c \
    tests/test_server_dele.c \
    tests/test_server_traversal.c

TEST_BINS := $(patsubst tests/%.c,$(BUILD_DIR)/%,$(TEST_SRCS))

.PHONY: all clean test client-lib

compile_commands.json: Makefile scripts/gen_compile_commands.py
	@CPP="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" CC="$(CC)" CLIENT_INC="$(CLIENT_INC)" BUILD_DIR="$(BUILD_DIR)" SRCS="$(SRC)" TEST_SRCS="$(TEST_SRCS)" python3 scripts/gen_compile_commands.py > $@

all: $(BIN)

test: $(BIN) client-lib $(TEST_BINS)
	@for t in $(TEST_BINS); do \
	    printf "%-40s" "$$t ..."; \
	    if $$t; then echo "ok"; else echo "FAIL"; exit 1; fi; \
	done

client-lib:
	$(MAKE) -C $(CLIENT_DIR) CFLAGS="$(CFLAGS)" all

$(BIN): $(OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c include/ftp_server.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_server_%.o: tests/test_server_%.c \
    tests/test_helper.h include/ftp_server.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CLIENT_INC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_server_%: $(BUILD_DIR)/test_server_%.o $(CLIENT_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -rf $(BUILD_DIR)
