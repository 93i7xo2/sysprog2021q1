/* affinity-based thread pool */
// #include "ringbuffer.h"
#include <pthread.h>
#include <stdbool.h>

#define QUEUESIZE 4096
#define ONESECOND 1000000000UL

typedef struct __threadpool threadpool_t;
typedef struct __tpool_future tpool_future_t;
typedef struct __threadtask threadtask_t;

bool tp_init(threadpool_t **tp, int size, bool enable_afn);
void tp_destroy(threadpool_t *tp);
void tp_join(threadpool_t *tp);

/* push new task into queue and return future to user*/
tpool_future_t *tp_queue(threadpool_t *tp, void *fn, void *arg);
tpool_future_t *tp_queue_afn(threadpool_t *tp, int affinity_id, void *fn,
                             void *arg);

void *tpool_future_get(tpool_future_t *future, unsigned int milliseconds);
void tpool_future_destroy(tpool_future_t *future);