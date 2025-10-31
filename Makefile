CC = gcc
PKGS = inih libsystemd

LDLIBS = $(shell pkgconf --libs $(PKGS))
CFLAGS = -Wall -Wextra -MMD -MP

ifeq ($(DEBUG), 1)
	CFLAGS += -g
endif

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

TARGET = $(BIN_DIR)/cfans

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

.PHONY: clean all install uninstall

# Install the binary to /usr/local/bin.
# Note: This should be done by a package manager.
# The user running make install must have write permissions to the destination.
install: $(TARGET)
	@echo "Installing $(TARGET) to /usr/local/bin..."
	@echo "You may need to run this with sudo."
	mkdir -p /usr/local/bin
	cp $(TARGET) /usr/local/bin/
	chown root:root /usr/local/bin/$(notdir $(TARGET))
	@echo "Installation complete."
	@echo "WARNING: The setuid bit is no longer set. You must configure permissions (e.g., with udev rules) for this program to access hardware."

# Uninstall the binary from /usr/local/bin.
uninstall:
	@echo "Uninstalling from /usr/local/bin..."
	@echo "You may need to run this with sudo."
	rm -f /usr/local/bin/$(notdir $(TARGET))
	@echo "Uninstallation complete."

clean:
	@echo "Cleaning up object and binary files..."
	rm -rf $(OBJ_DIR) $(BIN_DIR)

-include $(DEPS)
