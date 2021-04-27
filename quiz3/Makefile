all: test

.PHONY: clean

build:
	gcc -o xs -std=gnu11 xs.c
	g++ -o string xs.cpp

test: build
	./test.sh

clean:
	rm -rf xs string *.txt