
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -g $(EXTRA_CFLAGS)
LDFLAGS = -lpcap -lpthread -lmosquitto $(EXTRA_LDFLAGS)

BUILD_DIR = build
SRC_DIR = src

SRCS = $(wildcard $(SRC_DIR)/*.c)

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TARGET = $(BUILD_DIR)/wfs

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Rule to compile each C file into an object file (.o)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule to remove generated files
clean:
	rm -rf $(BUILD_DIR)

# Phony targets
.PHONY: all clean
