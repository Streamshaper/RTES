CC := gcc
SYNTHETIC_BURST_DEMO ?= 0
# Standard build flags for safety, modern C, and dependency tracking.
CFLAGS := -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -DSYNTHETIC_BURST_DEMO=$(SYNTHETIC_BURST_DEMO) -MMD -MP -I/usr/include
LDLIBS := -pthread -lwebsockets -lcjson

TARGET := main
SOURCES := src/main.c src/queue.c src/threads.c
OBJECTS := $(SOURCES:.c=.o)
DEPENDS := $(OBJECTS:.o=.d)

.PHONY: all demo clean help

all: $(TARGET)

# Displays usage information and available options.
help:
	@echo "Available targets:"
	@echo "  all      : Build the main executable (default)"
	@echo "  demo     : Clean and rebuild the project with the synthetic burst load tester enabled"
	@echo "  clean    : Remove all build artifacts (objects, dependencies, and executable)"
	@echo "  help     : Display this help message"
	@echo ""
	@echo "Available options:"
	@echo "  SYNTHETIC_BURST_DEMO=1 : Compile with synthetic burst load testing (default is 0)"

# Rebuilds the project with the synthetic burst load tester enabled.
demo:
	$(MAKE) clean
	$(MAKE) SYNTHETIC_BURST_DEMO=1 all

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPENDS)

clean:
	rm -rf $(TARGET) $(OBJECTS) $(DEPENDS)