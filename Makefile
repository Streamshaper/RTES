CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -MMD -MP
LDLIBS := -pthread

TARGET := main
SOURCES := main.c queue.c threads.c
OBJECTS := $(SOURCES:.c=.o)
DEPENDS := $(OBJECTS:.o=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPENDS)

clean:
	rm -f $(TARGET) $(OBJECTS) $(DEPENDS)