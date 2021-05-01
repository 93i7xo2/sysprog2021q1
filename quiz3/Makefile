CFLAGS=-std=c11 -D_GNU_SOURCE

.PHONY: clean

xs_benchmark: xs_benchmark.c xs.h
	$(CC) -o $@ $< $(CFLAGS)

string_benchmark: string_benchmark.cpp
	$(CXX) -o $@ $<

test: xs_benchmark string_benchmark
	./test.sh

clean:
	rm -f string_benchmark xs_benchmark