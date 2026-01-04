CC = gcc
LD = gcc

TARGET = cfans

SRC_DIR = src
BUILD_DIR = build

SRCS = src/main.c \
	src/config.c \
	src/hwmon.c \
	src/control.c

OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o)

DEPS = $(OBJS:%.o=$(BUILD_DIR)/%.d)

PKGS = libsystemd libcjson

CFLAGS = -Wall -Wextra
CPPFLAGS = -MMD -MP 
LDLIBS = $(shell pkgconf --libs $(PKGS))

# Add debug symbols if DEBUG=1 is passed to make
ifeq ($(DEBUG), 1)
	CFLAGS += -g
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
