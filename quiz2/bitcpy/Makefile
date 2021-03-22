all: 

build: clean
	gcc -g -O0 -o main main.c

perf: build
	sudo perf record -F max ./main
	sudo perf report -M intel

run: build
	./main >output

plot:
	gnuplot plot.gp

clean:
	rm -rf ./perf.* ./main ./output