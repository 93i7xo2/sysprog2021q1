#define _GNU_SOURCE
#include "afn_threadpool.h"
#include "ringbuffer.h"
#include <errno.h>
#include <likwid.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

enum __future_flags {
  __FUTURE_PENDING = 0,
  __FUTURE_RUNNING = 01,
  __FUTURE_FINISHED = 02,
  __FUTURE_TIMEOUT = 04,
  __FUTURE_CANCELLED = 010,
  __FUTURE_DESTROYED = 020,
};

struct __threadtask {
  void *(*func)(void *);
  void *arg;
  tpool_future_t *future;
};

struct __tpool_future {
  int flag;
  void *result;
  pthread_mutex_t mutex;
  pthread_cond_t cond_finished;
};

bool tpool_future_create(tpool_future_t **ptr) {
  *ptr = (tpool_future_t *)malloc(sizeof(tpool_future_t));
  if (*ptr) {
    (*ptr)->flag = __FUTURE_PENDING;
    (*ptr)->result = NULL;
    pthread_mutex_init(&(*ptr)->mutex, NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&(*ptr)->cond_finished, &attr);
    pthread_condattr_destroy(&attr);
    return true;
  }
  return false;
}

void tpool_future_destroy(tpool_future_t *future) {
  if (!future)
    return;
  pthread_mutex_lock(&future->mutex);
  if (future->flag & __FUTURE_FINISHED || future->flag & __FUTURE_CANCELLED) {
    pthread_mutex_unlock(&future->mutex);
    pthread_mutex_destroy(&future->mutex);
    pthread_cond_destroy(&future->cond_finished);
    free(future);
  } else {
    future->flag |= __FUTURE_DESTROYED;
    pthread_mutex_unlock(&future->mutex);
  }
}

tpool_future_t *tpool_apply(ringbuffer_t *rb, void *(*func)(void *),
                            void *arg) {
  threadtask_t *task = (threadtask_t *)malloc(sizeof(threadtask_t));
  tpool_future_t *future;
  if (task) {
    task->func = func;
    task->arg = arg;
    if (tpool_future_create(&future)) {
      task->future = future;
      enqueue_must(rb, (void *)task);
      return future;
    }
    free(task);
  }
  return NULL;
}

void *tpool_future_get(tpool_future_t *future, unsigned int milliseconds) {
  pthread_mutex_lock(&future->mutex);
  /* turn off the timeout bit set previously */
  future->flag &= ~__FUTURE_TIMEOUT;
  while ((future->flag & __FUTURE_FINISHED) == 0) {
    if (milliseconds) {
      struct timespec expire_time;
      clock_gettime(CLOCK_MONOTONIC, &expire_time);
      expire_time.tv_nsec += (milliseconds % 1000) * ONESECOND / 1000;
      if (expire_time.tv_nsec / ONESECOND) {
        expire_time.tv_nsec %= 1000000000;
        ++expire_time.tv_sec;
      }
      expire_time.tv_sec += milliseconds / 1000;

      int status = pthread_cond_timedwait(&future->cond_finished,
                                          &future->mutex, &expire_time);

      if (status == ETIMEDOUT) {
        future->flag |= __FUTURE_TIMEOUT;
        if (future->flag & __FUTURE_RUNNING)
          goto wait;
        future->flag |= __FUTURE_DESTROYED;
        return NULL;
      }
    } else {
    wait:
      pthread_cond_wait(&future->cond_finished, &future->mutex);
    }
  }
  pthread_mutex_unlock(&future->mutex);
  return future->result;
}

typedef struct __workeragent {
  ringbuffer_t *shared_queue, *private_queue;
  pthread_t worker;
  void *(*loop)(void *);
} workeragent_t;

static void *loop(void *);
static bool workeragent_init(workeragent_t **, ringbuffer_t *, int, bool);
static void workeragent_destroy(workeragent_t *);

static bool workeragent_init(workeragent_t **wa, ringbuffer_t *sharedqueue,
                             int affinity_id, bool enable_afn) {
  *wa = (workeragent_t *)malloc(sizeof(workeragent_t));
  (*wa)->shared_queue = sharedqueue;
  (*wa)->private_queue = rb_init(QUEUESIZE);
  (*wa)->loop = &loop;

  if (pthread_create(&(*wa)->worker, NULL, (*wa)->loop, (void *)*wa)) {
    workeragent_destroy(*wa);
    return false;
  }

  /* Set affinity mask */
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(affinity_id % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);

  if (enable_afn &&
      pthread_setaffinity_np((*wa)->worker, sizeof(cpuset), &cpuset)) {
    pthread_cancel((*wa)->worker);
    pthread_join((*wa)->worker, NULL);
    workeragent_destroy(*wa);
    return false;
  }

  return true;
}

static void workeragent_destroy(workeragent_t *wa) {
  rb_destroy(wa->private_queue);
  free(wa);
}

struct __threadpool {
  int size;
  _Atomic int r;             // used for round-robin
  ringbuffer_t *sharedqueue; // queue used for work stealing
  workeragent_t *workeragents[0];
};

bool tp_init(threadpool_t **tp, int size, bool enable_afn) {
  *tp = (threadpool_t *)malloc(sizeof(threadpool_t) +
                               sizeof(workeragent_t *) * size);
  // Load the LIKWID's topology module
  if (topology_init() < 0) {
    printf("Failed to initialize LIKWID's topology module\n");
    return false;
  }

  CpuTopology_t topo = get_cpuTopology();
  int numSockets = topo->numSockets,
      numCoresPerSocket = topo->numCoresPerSocket,
      numHWThreads = topo->numHWThreads;
  int cpulist[numSockets * numCoresPerSocket * numHWThreads], idx = 0;
  for (int socket = 0; socket < numSockets; ++socket) {
    for (int core = 0; core < numCoresPerSocket; ++core) {
      for (int i = 0; i < numHWThreads; ++i) {
        int threadId = topo->threadPool[i].threadId,
            coreId = topo->threadPool[i].coreId,
            packageId = topo->threadPool[i].packageId,
            apicId = topo->threadPool[i].apicId;
        if (packageId == socket && coreId == core) {
          cpulist[idx + threadId * (numCoresPerSocket * numSockets)] = apicId;
        }
      }
      idx++;
    }
  }
  topology_finalize();

  (*tp)->size = size;
  (*tp)->r = 0;
  (*tp)->sharedqueue = rb_init(QUEUESIZE);
  for (int i = 0; i < (*tp)->size; ++i) {
    if (!workeragent_init(&(*tp)->workeragents[i], (*tp)->sharedqueue,
                          cpulist[i], enable_afn)) {
      for (int j = 0; j < i; ++j) {
        pthread_cancel((*tp)->workeragents[i]->worker);
      }
      for (int j = 0; j < i; ++j) {
        pthread_join((*tp)->workeragents[i]->worker, NULL);
      }
      for (int j = 0; j < i; ++j) {
        workeragent_destroy((*tp)->workeragents[i]);
      }
      rb_destroy((*tp)->sharedqueue);
      return false;
    }
  }
  return true;
}

void tp_destroy(threadpool_t *tp) {
  for (int i = 0; i < tp->size; ++i) {
    workeragent_destroy(tp->workeragents[i]);
  }
  rb_destroy(tp->sharedqueue);
  free(tp);
}

void tp_join(threadpool_t *tp) {
  for (int i = 0; i < tp->size; ++i) {
    while (!tpool_apply(tp->workeragents[i]->private_queue, NULL, NULL))
      ;
  }
  for (int i = 0; i < tp->size; ++i)
    pthread_join(tp->workeragents[i]->worker, NULL);
}

tpool_future_t *tp_queue(threadpool_t *tp, void *fn, void *arg) {
  tpool_future_t *future = tpool_apply(tp->sharedqueue, fn, arg);
  return future;
}

tpool_future_t *tp_queue_afn(threadpool_t *tp, int affinity_id, void *fn,
                             void *arg) {
  workeragent_t *wa = tp->workeragents[affinity_id % tp->size];
  tpool_future_t *future = tpool_apply(wa->private_queue, fn, arg);
  return future;
}

static void *loop(void *arg) {
  workeragent_t *wa = (workeragent_t *)arg;
  ringbuffer_t *p_queue = wa->private_queue, *s_queue = wa->shared_queue;
  threadtask_t *task;
  int count = 0;
  // To-do
  // int old_state;

  while (1) {
    while (
        !(dequeue(p_queue, (void **)&task) || dequeue(s_queue, (void **)&task)))
      ;

    /* Terminate pthread */
    if (!task->func) {
      pthread_mutex_destroy(&task->future->mutex);
      pthread_cond_destroy(&task->future->cond_finished);
      free(task->future);
      free(task);
      break;
    }

    pthread_mutex_lock(&task->future->mutex);
    if (task->future->flag & __FUTURE_CANCELLED) {
      pthread_mutex_unlock(&task->future->mutex);
      free(task);
      continue;
    } else {
      task->future->flag |= __FUTURE_RUNNING;
      pthread_mutex_unlock(&task->future->mutex);
    }

    // To-do
    // pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
    // pthread_testcancel();
    void *result = task->func(task->arg);
    // pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);

    pthread_mutex_lock(&task->future->mutex);
    if (task->future->flag & __FUTURE_DESTROYED) {
      pthread_mutex_unlock(&task->future->mutex);
      pthread_mutex_destroy(&task->future->mutex);
      pthread_cond_destroy(&task->future->cond_finished);
      free(task->future);
    } else {
      task->future->flag |= __FUTURE_FINISHED;
      task->future->result = result;
      pthread_cond_broadcast(&task->future->cond_finished);
      pthread_mutex_unlock(&task->future->mutex);
    }
    free(task);

    /* Avoid starvation */
    if ((count++) % 32 == 0) {
      ringbuffer_t *tmp = p_queue;
      p_queue = s_queue;
      s_queue = tmp;
    }
  }

  return NULL; // in place of `pthread_exit(NULL)`
}