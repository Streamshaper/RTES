CC := gcc
SYNTHETIC_BURST_DEMO ?= 0
CFLAGS := -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -DSYNTHETIC_BURST_DEMO=$(SYNTHETIC_BURST_DEMO) -MMD -MP -I/usr/include
LDLIBS := -pthread -lwebsockets -lcjson

PI_CC ?= aarch64-linux-gnu-gcc
PI_CFLAGS := $(CFLAGS)
PI_LDLIBS := $(LDLIBS)

TARGET := main
SOURCES := src/main.c src/queue.c src/threads.c
OBJECTS := $(SOURCES:.c=.o)
DEPENDS := $(OBJECTS:.o=.d)
PI_BUILD_DIR := build/raspberry-pi
PI_TARGET := $(PI_BUILD_DIR)/main
PI_OBJECTS := $(SOURCES:%.c=$(PI_BUILD_DIR)/%.o)
PI_DEPENDS := $(PI_OBJECTS:.o=.d)

.PHONY: all demo raspberry-pi clean

all: $(TARGET)

demo:
	$(MAKE) clean
	$(MAKE) SYNTHETIC_BURST_DEMO=1 all

raspberry-pi: $(PI_TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(PI_TARGET): $(PI_OBJECTS)
	$(PI_CC) $(PI_OBJECTS) $(PI_LDLIBS) -o $@

$(PI_BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(PI_CC) $(PI_CFLAGS) -c $< -o $@

-include $(DEPENDS) $(PI_DEPENDS)

clean:
	rm -rf $(TARGET) $(OBJECTS) $(DEPENDS) build