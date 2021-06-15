#include "ringbuffer.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

ringbuffer_t *rb;
_Atomic int64_t sum = 0;

void *reader(void *arg) {
  for (int i = 0; i < 4096; i++) {
    sum += (int64_t)dequeue_must(rb);
  }
  return NULL;
}

int main() {
  rb = rb_init(10);
  int thread_count = 4, run = thread_count * 4096;
  pthread_t readers[thread_count];

  for (int i = 0; i < thread_count; ++i) {
    pthread_create(&readers[i], NULL, reader, NULL);
  }
  for (uint64_t i = 0; i < run; i++) {
    enqueue_must(rb, (void *)i);
  }
  for (int i = 0; i < thread_count; ++i) {
    pthread_join(readers[i], NULL);
  }
  rb_destroy(rb);
  assert(sum == ((run - 1) * run / 2));
  return 0;
}