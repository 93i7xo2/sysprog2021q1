#define _GNU_SOURCE
#include "afn_threadpool.h"
#include "ringbuffer.h"
#include <errno.h>
#include <likwid.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define MAX_EVENTES 100
#define MAX_TASKS 8

/* refer to @sysprog/linux-timerfd */
static void add_event(int epoll_fd, int fd, int state) {
  struct epoll_event ev = {
      .events = state,
      .data.fd = fd,
  };
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  return;
}

struct __threadtask {
  void *(*func)(void *);
  void *arg;
  void **dstptr;
  int id;
};

static int tpool_apply(ringbuffer_t *rb, void *(*func)(void *), void *arg,
                       void **dstptr) {
  static int id;
  threadtask_t *task = (threadtask_t *)malloc(sizeof(threadtask_t));
  if (task) {
    task->func = func;
    task->arg = arg;
    task->dstptr = dstptr;
    task->id = ++id;
    enqueue_must(rb, (void *)task);
    return task->id;
  }
  return -1;
}

typedef struct __workeragent {
  ringbuffer_t *shared_queue, *private_queue;
  pthread_t worker;
  void *(*loop)(void *);
  int shared_epfd, private_epfd;
} workeragent_t;

static void *loop(void *);
static bool workeragent_init(workeragent_t **, ringbuffer_t *, int, bool, int);
static void workeragent_destroy(workeragent_t *);

static bool workeragent_init(workeragent_t **wa, ringbuffer_t *sharedqueue,
                             int affinity_id, bool enable_afn,
                             int shared_epfd) {
  (*wa) = (workeragent_t *)malloc(sizeof(workeragent_t));
  (*wa)->shared_queue = sharedqueue;
  (*wa)->private_queue = rb_init(QUEUESIZE);
  (*wa)->loop = &loop;
  (*wa)->shared_epfd = shared_epfd;
  (*wa)->private_epfd = epoll_create(1);
  if ((*wa)->private_epfd == -1)
    return false;

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
  close(wa->private_epfd);
  rb_destroy(wa->private_queue);
  free(wa);
}

struct __threadpool {
  int size;
  int shared_epfd;
  _Atomic int r;             // used for round-robin
  ringbuffer_t *sharedqueue; // queue used for work stealing
  workeragent_t *workeragents[0];
};

bool tp_init(threadpool_t **tp, int size, bool enable_afn) {
  *tp = (threadpool_t *)malloc(sizeof(threadpool_t) +
                               sizeof(workeragent_t *) * size);
  // Create epoll instance
  (*tp)->shared_epfd = epoll_create(1);
  if ((*tp)->shared_epfd == -1)
    return false;

  // Load the LIKWID's topology module
  if (topology_init() < 0) {
    printf("Failed to initialize LIKWID's topology module\n");
    return false;
  }

  CpuTopology_t topo = get_cpuTopology();
  int numSockets = topo->numSockets,
      numCoresPerSocket = topo->numCoresPerSocket,
      numHWThreads = topo->numHWThreads, cpulist[topo->numHWThreads], idx = 0;
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
                          cpulist[i], enable_afn, (*tp)->shared_epfd)) {
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
  close(tp->shared_epfd);
  free(tp);
}

void tp_join(threadpool_t *tp) {
  for (int i = 0; i < tp->size; ++i) {
    while (!tpool_apply(tp->workeragents[i]->private_queue, NULL, NULL, NULL))
      ;
  }
  for (int i = 0; i < tp->size; ++i)
    pthread_join(tp->workeragents[i]->worker, NULL);
}

int tp_queue(threadpool_t *tp, void *fn, void *arg, void **dstptr) {
  return tpool_apply(tp->sharedqueue, fn, arg, dstptr);
}

int tp_queue_afn(threadpool_t *tp, int affinity_id, void *fn, void *arg,
                 void **dstptr) {
  workeragent_t *wa = tp->workeragents[affinity_id % tp->size];
  return tpool_apply(wa->private_queue, fn, arg, dstptr);
}

int tp_epfd_get(threadpool_t *tp) { return tp->shared_epfd; }

void tp_task_cancel(threadpool_t *tp, int task_id) {
  for (int i = 0; i < tp->size; ++i) {
    int efd = eventfd(task_id, EFD_CLOEXEC | EFD_NONBLOCK);
    add_event(tp->workeragents[i]->private_epfd, efd, EPOLLIN);
    printf("Cancel task-%d from [worker-%d]\n", task_id, i);
  }
}

static void *loop(void *arg) {
  workeragent_t *wa = (workeragent_t *)arg;
  ringbuffer_t *p_queue = wa->private_queue, *s_queue = wa->shared_queue;
  threadtask_t *task;
  int shared_epfd = wa->shared_epfd, private_epfd = wa->private_epfd, count = 0;
  struct epoll_event events[MAX_EVENTES];
  int cancelled_tasks[MAX_TASKS] = {-1}, ccount = -1;

  while (1) {
    while (
        !(dequeue(p_queue, (void **)&task) || dequeue(s_queue, (void **)&task)))
      ;

    /* Terminate pthread */
    if (!task->func) {
      free(task);
      break;
    }

    /* Receive cancellation request */
    int id = task->id, fs = epoll_wait(private_epfd, events, MAX_EVENTES, 0);
    for (int i = 0; i < fs; ++i) {
      uint64_t task_id;
      if (eventfd_read(events[i].data.fd, &task_id) != -1) {
        cancelled_tasks[(++ccount) % MAX_TASKS] = task_id;
        close(events[i].data.fd);
      }
    }
    for (int i = 0; i < MAX_TASKS; ++i) {
      if (cancelled_tasks[i] == id)
        goto _done;
    }

    /* Execute task */
    *task->dstptr = task->func(task->arg);

    /* Register to the poller */
    int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    add_event(shared_epfd, efd, EPOLLIN);

    /* Send event */
    eventfd_write(efd, (uint64_t)id);

  _done:
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