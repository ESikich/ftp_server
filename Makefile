# Makefile - build the FTP server.

CC      ?= cc
CFLAGS  ?= -std=c2x -Wall -Wextra -Wpedantic -Werror -O2
CPPFLAGS ?= -D_GNU_SOURCE -Iinclude -Isrc
LDFLAGS ?=
LDLIBS  ?=

BUILD_DIR := build
BIN       := $(BUILD_DIR)/ftp-server

SRC := \
    src/main.c \
    src/ftp_cmd.c \
    src/ftp_data.c \
    src/ftp_log.c \
    src/ftp_path.c \
    src/ftp_reply.c \
    src/ftp_session.c

OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c include/ftp_server.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
