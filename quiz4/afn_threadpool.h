/* affinity-based thread pool */
// #include "ringbuffer.h"
#include <pthread.h>
#include <stdbool.h>

#define QUEUESIZE 4096
#define ONESECOND 1000000000UL

typedef struct __threadpool threadpool_t;
typedef struct __threadtask threadtask_t;

bool tp_init(threadpool_t **tp, int size, bool enable_afn);
void tp_destroy(threadpool_t *tp);
void tp_join(threadpool_t *tp);

/* push new task into queue and return task id if succeed */
int tp_queue(threadpool_t *tp, void *fn, void *arg, void **dstptr);
int tp_queue_afn(threadpool_t *tp, int affinity_id, void *fn, void *arg,
                 void **dstptr);

int tp_epfd_get(threadpool_t *tp);
void tp_task_cancel(threadpool_t *tp, int task_id);

#define handle_error(x)                                                        \
  do {                                                                         \
    printf("%s\n", x);                                                         \
    exit(EXIT_FAILURE);                                                        \
  } while (0)
