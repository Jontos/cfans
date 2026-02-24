CC ?= gcc

TARGET = cfans

SRC_DIR = src
BUILD_DIR = build

SRCS = src/main.c \
	src/config.c \
	src/hwmon.c \
	src/control.c

OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS = $(OBJS:%.o=$(BUILD_DIR)/%.d)

PKGS = libsystemd libcjson ncurses

CFLAGS ?= -O2 -pipe
LDFLAGS ?=
CPPFLAGS ?=
EXTRA_CFLAGS = -Wall -Wextra $(shell pkgconf --cflags $(PKGS))
EXTRA_CPPFLAGS = -MMD -MP 
LDLIBS = $(shell pkgconf --libs $(PKGS))

PREFIX ?= /usr/local
SYSCONFDIR ?= /etc

# Add debug symbols and strip optimizations if DEBUG=1 is passed
ifeq ($(DEBUG), 1)
	CFLAGS := $(filter-out -O1 -O2 -O3 -Os,$(CFLAGS)) -g -O0
endif

.PHONY: all clean install

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(EXTRA_CPPFLAGS) $(CPPFLAGS) $(EXTRA_CFLAGS) $(CFLAGS) -c $< -o $@

install: $(BUILD_DIR)/$(TARGET)
	install -D -m 755 $(BUILD_DIR)/$(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	
	install -D -m 644 config.json $(DESTDIR)$(SYSCONFDIR)/cfans/config.json.example

	install -D -m 644 cfans.service $(DESTDIR)$(PREFIX)/lib/systemd/system/cfans.service
	install -D -m 644 51-cfans.rules $(DESTDIR)$(PREFIX)/lib/udev/rules.d/51-cfans.rules
	install -D -m 644 cfans.sysusers $(DESTDIR)$(PREFIX)/lib/sysusers.d/cfans.conf

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
