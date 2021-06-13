#include <stdbool.h>
#include <stddef.h>

typedef struct __ringbuffer ringbuffer_t;

ringbuffer_t *rb_init(size_t len);
void rb_destroy(ringbuffer_t *rb);
bool enqueue(ringbuffer_t *, void **);
bool dequeue(ringbuffer_t *, void **);
void enqueue_must(ringbuffer_t *, void *);
void *dequeue_must(ringbuffer_t *);