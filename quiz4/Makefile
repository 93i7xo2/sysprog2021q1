CFLAGS = -Wall -O0 -g -pthread -lm -llikwid -std=gnu11
VALGRIND_OPTS = --leak-check=full \
--show-leak-kinds=all \
--suppressions=valgrind_supression.config \
--error-exitcode=1 -s

.PHONY: clean

all: check

%.o: %.c
	$(CC) -o $@ -c -MMD -MF .$@.d $< $(CFLAGS)

ringbuffer_example: ringbuffer.o ringbuffer_example.o
	$(CC) -o $@ $^ $(CFLAGS)

# thread-safe queue + thread pool
threadpool_pi: threadpool.o threadpool_pi.c
	$(CC) -o $@ $^ $(CFLAGS)

# lock-free queue + affinity-based thread pool
afn_threadpool_pi: ringbuffer.o afn_threadpool.o afn_threadpool_pi.c
	$(CC) -o $@ $^ $(CFLAGS)

# lock-free queue + thread pool
afn_threadpool_pi_v2: ringbuffer.o afn_threadpool.o afn_threadpool_pi.c
	$(CC) -o $@ -D ENABLE_AFN=false $^ $(CFLAGS)

PRINTF = env printf
PASS_COLOR = \e[32;01m
NO_COLOR = \e[0m
pass = $(PRINTF) "$(PASS_COLOR)$1 Passed [-]$(NO_COLOR)\n"

check: ringbuffer_example threadpool_pi afn_threadpool_pi afn_threadpool_pi_v2
	@for i in $^; do \
	valgrind $(VALGRIND_OPTS) ./$$i 1>/dev/null || exit 1; \
	done && $(call pass)

benchmark: threadpool_pi afn_threadpool_pi afn_threadpool_pi_v2
	@./benchmark.sh && $(call pass)

plot:
	gnuplot plot.gp

perf:
	@sudo perf record -g --call-graph dwarf ./threadpool_pi
	
prepare:
	sudo apt install python3-pip likwid valgrind gnuplot
	pip3 install pandas scipy

clean:
	@rm -f ringbuffer_example threadpool_pi afn_threadpool_pi afn_threadpool_pi_v2 *.o .*.d *.txt *.png