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

PREFIX ?= /usr/local

# Add debug symbols if DEBUG=1 is passed to make
ifeq ($(DEBUG), 1)
	CFLAGS += -g
endif

.PHONY: all clean install

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	
	install -D -m 644 config.json $(DESTDIR)/etc/cfans/config.json.example

	install -D -m 644 cfans.service $(DESTDIR)/usr/lib/systemd/system/cfans.service
	install -D -m 644 51-cfans.rules $(DESTDIR)/usr/lib/udev/rules.d/51-cfans.rules
	install -D -m 644 cfans.sysusers $(DESTDIR)/usr/lib/sysusers.d/cfans.conf

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
