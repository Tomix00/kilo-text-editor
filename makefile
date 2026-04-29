# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99

# Target names and folders
TARGET = kilo
SRC = kilo.c
BUILD_DIR = build
OUT = $(BUILD_DIR)/$(TARGET)

# Ensure build directory exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Default target: build the program
$(OUT): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

# Run the program
.PHONY: run
run: $(OUT)
	./$(OUT)

# Clean build folder
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/

# Reload: clean and run
.PHONY: reload
reload: clean run
