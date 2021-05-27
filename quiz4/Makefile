CFLAGS = -Wall -O0 -g -lpthread -lm
TARGET = pi

DEPS := pi.c

all: $(TARGET)

$(TARGET): $(DEPS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(TARGET)
