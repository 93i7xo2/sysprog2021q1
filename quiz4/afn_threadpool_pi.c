#define _GNU_SOURCE
#include "afn_threadpool.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PRECISION 1000 /* upper bound in BPP sum */
#ifndef ENABLE_AFN
#define ENABLE_AFN true
#endif

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
  tpool_future_t *futures[PRECISION + 1];

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Creation time: %.0f ns\n",
         (double)(end.tv_sec - start.tv_sec) * ONESECOND +
             (end.tv_nsec - start.tv_nsec));
  clock_gettime(CLOCK_MONOTONIC, &start);

  for (int i = 0; i <= PRECISION; i++) {
    bpp_args[i] = i;
    futures[i] = tp_queue(tp, bpp, (void *)&bpp_args[i]);
  }
  for (int i = 0; i <= PRECISION; i++) {
    if (!futures[i])
      continue;
    double *result = tpool_future_get(futures[i], time_limit);
    if (result) {
      bpp_sum += *result;
      free(result);
      tpool_future_destroy(futures[i]);
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Execution time: %.0f ns\n",
         (double)(end.tv_sec - start.tv_sec) * ONESECOND +
             (end.tv_nsec - start.tv_nsec));
  clock_gettime(CLOCK_MONOTONIC, &start);

  tp_join(tp);
  tp_destroy(tp);

  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Deconstruction time: %.0f ns\n",
         (double)(end.tv_sec - start.tv_sec) * ONESECOND +
             (end.tv_nsec - start.tv_nsec));
  printf("PI calculated with %d terms: %.15f\n", PRECISION + 1, bpp_sum);
  return 0;
}
