#include <stdbool.h>
#include <stddef.h>

typedef struct __ringbuffer ringbuffer_t;

ringbuffer_t *rb_init(size_t len);
void rb_destroy(ringbuffer_t *rb);
bool enqueue(ringbuffer_t *, void **);
bool dequeue(ringbuffer_t *, void **);
void enqueue_must(ringbuffer_t *, void *);
void *dequeue_must(ringbuffer_t *);

/* Refer to rmind/ringbuf */
#define SPINLOCK_BACKOFF_MIN 4
#define SPINLOCK_BACKOFF_MAX 128
#define SPINLOCK_BACKOFF_HOOK __asm volatile("pause" ::: "memory")
#define SPINLOCK_BACKOFF(count)                                                \
  do {                                                                         \
    for (int __i = (count); __i != 0; __i--) {                                 \
      SPINLOCK_BACKOFF_HOOK;                                                   \
    }                                                                          \
    if ((count) < SPINLOCK_BACKOFF_MAX)                                        \
      (count) += (count);                                                      \
  } while (0)
  