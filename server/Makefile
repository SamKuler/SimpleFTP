CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Iinclude

# Source files
SOURCES = src/main.c src/utils.c src/logger.c src/filesys.c src/filelock.c src/network.c \
          src/protocol.c src/command.c src/session.c src/transfer.c src/server.c \
          src/auth.c src/handler.c

# Target executable
TARGET = server

# Default target
all: $(TARGET)

# Compile directly to executable
$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

# Clean up
clean:
	rm -f $(TARGET)

.PHONY: all clean
