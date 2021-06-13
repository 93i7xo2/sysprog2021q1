#define _GNU_SOURCE
#include "ringbuffer.h"
#include <errno.h>
#include <linux/kernel.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGN(x, a) __ALIGN_KERNEL((x), (a))

#define err(s)                                                                 \
  do {                                                                         \
    printf(s "\n");                                                            \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

struct __ringbuffer {
  int fd;
  size_t size, mask;
  size_t read_offset, write_offset; // Last read/write
  _Atomic int readble, writable;
  uint8_t *buffer;
};

ringbuffer_t *rb_init(size_t len) {
  ringbuffer_t *ret = (ringbuffer_t *)malloc(sizeof(ringbuffer_t));
  if (!ret)
    err("Failed to allocate memory");

#define MAXBITS (sizeof(size_t) << 3)
  // Make the requested size be multiple of a page
  ret->size = ALIGN(len, getpagesize());
  ret->mask = (ret->size << 1) - 1;
  if (!ret->size)
    err("Cannot be size 0");
  else if ((((size_t)1) << (MAXBITS - 1)) & ret->size)
    err("Size too large");
#undef MAXBITS

  if ((ret->fd = memfd_create("shma", 0)) < 0)
    err("Failed to open file");
  if (ftruncate(ret->fd, ret->size) != 0)
    err("Failed to set buffer size");

  // Ask for virtual address
  // Refer to @sysprog/linux2020-quiz13
  if ((ret->buffer = mmap(NULL, ret->size << 1, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
    err("Could not allocate virtual memory");

  // 1st region
  if (mmap(ret->buffer, ret->size, PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_FIXED, ret->fd, 0) == MAP_FAILED)
    err("Could not map buffer into virtual memory");

  // 2nd region
  if (mmap(ret->buffer + ret->size, ret->size, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_FIXED, ret->fd, 0) == MAP_FAILED)
    err("Could not map buffer into virtual memory");

  ret->read_offset = 0;
  ret->write_offset = 0;
  ret->readble = 1;
  ret->writable = 1;
  return ret;
}

void rb_destroy(ringbuffer_t *rb) {
  if (munmap(rb->buffer + rb->size, rb->size) != 0)
    err("Could not unmap buffer");

  if (munmap(rb->buffer, rb->size) != 0)
    err("Could not unmap buffer");

  if (close(rb->fd) != 0)
    err("Could not close file");

  free(rb);
}

bool enqueue(ringbuffer_t *rb, void **src) {
  // is full
  int expected = 1;
  while (!atomic_compare_exchange_weak(&rb->readble, &expected, 0))
    expected = 1;
  while (!atomic_compare_exchange_weak(&rb->writable, &expected, 0))
    expected = 1;
  if (rb->read_offset == (rb->write_offset ^ rb->size)) {
    atomic_store(&rb->readble, 1);
    atomic_store(&rb->writable, 1);
    return NULL;
  }
  atomic_store(&rb->readble, 1);

  rb->write_offset += sizeof(void *);
  rb->write_offset &= rb->mask;
  memcpy(&rb->buffer[rb->write_offset], src, sizeof(void *));
  atomic_store(&rb->writable, 1);

  return true;
}

bool dequeue(ringbuffer_t *rb, void **dst) {
  // is empty
  int expected = 1;
  while (!atomic_compare_exchange_weak(&rb->readble, &expected, 0))
    expected = 1;
  while (!atomic_compare_exchange_weak(&rb->writable, &expected, 0))
    expected = 1;
  if (rb->read_offset == rb->write_offset) {
    atomic_store(&rb->readble, 1);
    atomic_store(&rb->writable, 1);
    return false;
  }
  atomic_store(&rb->writable, 1);

  rb->read_offset += sizeof(void *);
  rb->read_offset &= rb->mask;
  memcpy(dst, &rb->buffer[rb->read_offset], sizeof(void *));
  atomic_store(&rb->readble, 1);

  return true;
}

void enqueue_must(ringbuffer_t *rb, void *ptr) {
  while (!enqueue(rb, &ptr))
    ;
}

void *dequeue_must(ringbuffer_t *rb) {
  void *ret;
  while (!dequeue(rb, &ret))
    ;
  return ret;
}