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
#define RB_OFF_MASK (0x00000000ffffffffUL)
#define RB_COUNTER_MASK (0xffffffff00000000UL)

#define err(s)                                                                 \
  do {                                                                         \
    printf(s "\n");                                                            \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

struct __ringbuffer {
  int fd;
  uint64_t size, mask;
  _Atomic int64_t count;
  _Atomic uint64_t read_offset, write_offset; // Last read/write
  uint8_t *buffer;
};

ringbuffer_t *rb_init(size_t len) {
  ringbuffer_t *ret = (ringbuffer_t *)malloc(sizeof(ringbuffer_t));
  if (!ret)
    err("Failed to allocate memory");

#define MAXBITS (sizeof(uint32_t) << 3)
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
  ret->count = 0;
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

/* producer */
bool enqueue(ringbuffer_t *rb, void **src) {
  uint64_t _read, _write;

  _read = atomic_load(&rb->read_offset) & RB_OFF_MASK;
  _write = atomic_load(&rb->write_offset) & RB_OFF_MASK;
  if (_read == (_write ^ rb->size))
    return false;

  memcpy(&rb->buffer[_write], src, sizeof(void *));
  _write = (_write + sizeof(void *)) & rb->mask;
  _write |= ((uint64_t)*src << 32);
  atomic_store_explicit(&rb->write_offset, _write, memory_order_release);
  atomic_fetch_add_explicit(&rb->count, 1, memory_order_release);
  return true;
}

/* consumer */
bool dequeue(ringbuffer_t *rb, void **dst) {
  int64_t count, new_count;

  do {
    count = atomic_load(&rb->count);
    new_count = count - 1;
    if (__builtin_expect((new_count < 0), 1))
      return false;
  } while (!atomic_compare_exchange_weak(&rb->count, &count, new_count));

  uint64_t _read, new_read;
  do {
    _read = atomic_load(&rb->read_offset);
    new_read = (((_read & RB_OFF_MASK) + sizeof(void *)) & rb->mask);
    memcpy(dst, &rb->buffer[_read & RB_OFF_MASK], sizeof(void *));
  } while (!atomic_compare_exchange_weak(&rb->read_offset, &_read, new_read));

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