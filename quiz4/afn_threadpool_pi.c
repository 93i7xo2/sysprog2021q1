#define _GNU_SOURCE
#include "afn_threadpool.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define MAX_EVENTES 1000
#define PRECISION 1000 /* upper bound in BPP sum */
#ifndef ENABLE_AFN
#define ENABLE_AFN true
#endif

enum __task_flags {
  __TASK_PENDING = 0,
  __TASK_FINISHED = 1,
  __TASK_TIMEOUT = 2
};

/* refer to @sysprog/linux-timerfd */
int timer_update(int timer_fd, int ms) {
  struct itimerspec its = {
      .it_interval = {.tv_sec = ms / 1000,
                      .tv_nsec = (ms % 1000) * 1000 * 1000},
      .it_value.tv_sec = ms / 1000,
      .it_value.tv_nsec = (ms % 1000) * 1000 * 1000,
  };
  if (timerfd_settime(timer_fd, 0, &its, NULL) < 0)
    return -1;
  return 0;
}

/* Use Bailey–Borwein–Plouffe formula to approximate PI */
static void *bpp(void *arg) {
  int k = *(int *)arg;
  double sum = (4.0 / (8 * k + 1)) - (2.0 / (8 * k + 4)) - (1.0 / (8 * k + 5)) -
               (1.0 / (8 * k + 6));
  double *product = malloc(sizeof(double));
  if (product)
    *product = 1 / pow(16, k) * sum;
  return (void *)product;
}

int main(int argc, char **argv) {

  if (argc > 3) {
    printf("Usage: ./a_threadpool_example [THREAD_COUNT [TIME_LIMIT]]\n");
    return -1;
  }
  unsigned int time_limit =
      argc > 2 ? abs(atoi(argv[2])) : 0; /* 0 = blocking wait */
  int bpp_args[PRECISION + 1];
  double bpp_sum = 0;
  int nthreads = argc > 1 ? abs(atoi(argv[1])) : sysconf(_SC_NPROCESSORS_ONLN);
  printf("[%c] Affinity-based thread pool\n", ENABLE_AFN ? 'o' : 'x');
  printf("Assigned %d tasks between %d threads\n", PRECISION + 1, nthreads);

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  threadpool_t *tp;
  if (!tp_init(&tp, nthreads, ENABLE_AFN))
    exit(EXIT_FAILURE);

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Creation time: %.0f ns\n",
         (double)(end.tv_sec - start.tv_sec) * ONESECOND +
             (end.tv_nsec - start.tv_nsec));
  clock_gettime(CLOCK_MONOTONIC, &start);

  struct epoll_event events[MAX_EVENTES];
  struct future {
    int id;
    void *result;
    int status;
  } futures[PRECISION + 1];
  int epfd = tp_epfd_get(tp);

  for (int i = 0; i < PRECISION + 1; i++) {
    bpp_args[i] = i;
    futures[i].status = __TASK_PENDING;
    futures[i].result = NULL;
    futures[i].id = tp_queue(tp, bpp, (void *)&bpp_args[i], &futures[i].result);
    if (futures[i].id < 0) {
      handle_error("Failed to create task.");
    }
  }

  /* create timer */
  int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

  /* register timer fd on epoll instance */
  if (time_limit > 0) {
    struct epoll_event ev = {
        .events = EPOLLIN,
        .data.fd = timer_fd,
    };
    epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev);
    if (timer_update(timer_fd, time_limit) < 0)
      handle_error("timer_update");
  }

  /* receive event */
  int received, expected;
  received = expected = 0;
  while (received + expected < PRECISION + 1) {
    int fire_events = epoll_wait(epfd, events, MAX_EVENTES, time_limit);
    for (int i = 0; i < fire_events; ++i) {
      uint64_t counter;
      ssize_t size = read(events[i].data.fd, &counter, sizeof(uint64_t));
      if (size != sizeof(uint64_t))
        handle_error("read error");

      if (events[i].data.fd == timer_fd) {
        /* check if the task is finished */
        if (futures[expected].status & __TASK_PENDING) {
          futures[expected].status |= __TASK_TIMEOUT;
          tp_task_cancel(tp, futures[expected].id);
          printf("Cancel task-%d\n", futures[expected].id);
        }
        expected++;
      } else {
        /* store caculated data */
        int idx = counter - 1;
        if (~(futures[idx].status & __TASK_TIMEOUT)) {
          futures[idx].status |= __TASK_FINISHED;
          bpp_sum += *((double *)futures[idx].result);
          received++;
        }
        close(events[i].data.fd);
      }
    }
  }
  close(timer_fd);

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Execution time: %.0f ns\n",
         (double)(end.tv_sec - start.tv_sec) * ONESECOND +
             (end.tv_nsec - start.tv_nsec));
  clock_gettime(CLOCK_MONOTONIC, &start);

  tp_join(tp);
  tp_destroy(tp);
  for (int i = 0; i < PRECISION + 1; ++i) {
    if (futures[i].result)
      free(futures[i].result);
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Deconstruction time: %.0f ns\n",
         (double)(end.tv_sec - start.tv_sec) * ONESECOND +
             (end.tv_nsec - start.tv_nsec));
  printf("PI calculated with %d terms: %.15f\n", PRECISION + 1, bpp_sum);
  printf("Skipped %d tasks\n", expected);
  return 0;
}
