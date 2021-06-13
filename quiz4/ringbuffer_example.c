#include "ringbuffer.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

ringbuffer_t *rb;

void *writer(void *arg) {
  for (uint64_t i = 0; i < 4096; i++) {
    enqueue_must(rb, (void *)i);
  }
  return NULL;
}

void *reader(void *arg) {
  for (int i = 0; i < 4096; i++) {
    dequeue_must(rb);
  }
  return NULL;
}

int main() {
  rb = rb_init(10);
  int thread_count = 4;
  pthread_t writers[thread_count];
  pthread_t readers[thread_count];
  for (int i = 0; i < thread_count; ++i) {
    pthread_create(&writers[i], NULL, writer, NULL);
    pthread_create(&readers[i], NULL, reader, NULL);
  }
  for (int i = 0; i < thread_count; ++i) {
    pthread_join(writers[i], NULL);
    pthread_join(readers[i], NULL);
  }
  rb_destroy(rb);
  return 0;
}