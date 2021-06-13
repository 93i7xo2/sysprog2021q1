#define _GNU_SOURCE
#include "threadpool.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define PRECISION 1000 /* upper bound in BPP sum */
#define ONE_SEC 1000000000.0

#ifdef DEBUG
#define DEBUG_PRINT(x) printf x
#else
#define DEBUG_PRINT(x)                                                         \
  do {                                                                         \
  } while (0)
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
    printf("Usage: ./pi [THREAD_COUNT [TIME_LIMIT]]\n");
    return -1;
  }

  size_t thcount =
      argc > 1 ? abs(atoi(argv[1])) : sysconf(_SC_NPROCESSORS_ONLN);
  unsigned int time_limit =
      argc > 2 ? abs(atoi(argv[2])) : 0; /* 0 = blocking wait */
  int bpp_args[PRECISION + 1];
  double bpp_sum = 0;
  struct timespec start, end;
  printf("Thread count: %ld\nTime limit: %d ms\n", thcount, time_limit);

  clock_gettime(CLOCK_MONOTONIC, &start);

  tpool_t pool = tpool_create(thcount);
  tpool_future_t futures[PRECISION + 1];

  for (int i = 0; i <= PRECISION; i++) {
    bpp_args[i] = i;
    futures[i] = tpool_apply(pool, bpp, (void *)&bpp_args[i]);
  }

  for (int i = 0; i <= PRECISION; i++) {
    if (!futures[i])
      continue;
    double *result = tpool_future_get(futures[i], time_limit, pool->jobqueue);
    if (result) {
      bpp_sum += *result;
      free(result);
      tpool_future_destroy(futures[i]);
      DEBUG_PRINT(("Future[%d] completed!\n", i));
    } else
      DEBUG_PRINT(("Cannot get future[%d], timeout after %d milliseconds.\n", i,
                   time_limit));
  }

  tpool_join(pool);

  clock_gettime(CLOCK_MONOTONIC, &end);

  printf("Elapsed time: %.0f ns\n",
         (double)(end.tv_sec - start.tv_sec) * ONE_SEC +
             (end.tv_nsec - start.tv_nsec));
  printf("PI calculated with %d terms: %.15f\n", PRECISION + 1, bpp_sum);
  return 0;
}
