CFLAGS = -Wall -O0 -g -lpthread -lm -std=c99 -D_GNU_SOURCE
TARGET = pi

DEPS := pi.c

.PHONY: $(TARGET)

all: $(TARGET)

$(TARGET): $(DEPS)
	$(CC) -o $@ $^ $(CFLAGS)

perf: $(TARGET)
	sudo perf record -g --call-graph dwarf ./${TARGET}

clean:
	rm -f $(TARGET)
