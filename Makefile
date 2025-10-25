CC = gcc
PKGS = inih

LDLIBS = $(shell pkgconf --libs $(PKGS))
CFLAGS = -Wall -Wextra

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

# Install the binary to /usr/local/bin with correct permissions
install: $(TARGET)
	@echo "Installing to /usr/local/bin..."
	sudo mkdir -p /usr/local/bin
	sudo cp $(TARGET) /usr/local/bin/
	sudo chown root:root /usr/local/bin/$(notdir $(TARGET))
	sudo chmod u+s /usr/local/bin/$(notdir $(TARGET))
	@echo "Installation complete."

# Remove the installed binary
uninstall:
	@echo "Uninstalling from /usr/local/bin..."
	sudo rm -f /usr/local/bin/$(notdir $(TARGET))
	@echo "Uninstallation complete."

clean:
	@echo "Cleaning up object and binary files..."
	rm -rf $(OBJ_DIR) $(BIN_DIR)

-include $(DEPS)
