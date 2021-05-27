#include "threadpool.c"
#include <math.h>
#include <stdio.h>

#define PRECISION 100 /* upper bound in BPP sum */
#define ONE_SEC 1000000000.0

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

int main() {
  int bpp_args[PRECISION + 1];
  double bpp_sum = 0;
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  tpool_t pool = tpool_create(4);
  tpool_future_t futures[PRECISION + 1];

  for (int i = 0; i <= PRECISION; i++) {
    bpp_args[i] = i;
    futures[i] = tpool_apply(pool, bpp, (void *)&bpp_args[i]);
  }

  for (int i = 0; i <= PRECISION; i++) {
    double *result = tpool_future_get(futures[i], 0 /* blocking wait */);
    bpp_sum += *result;
    tpool_future_destroy(futures[i]);
    free(result);
  }

  tpool_join(pool);

  clock_gettime(CLOCK_MONOTONIC, &end);

  printf("Elapsed time: %.0f ns\n",
         (double)(end.tv_sec - start.tv_sec) * ONE_SEC +
             (end.tv_nsec - start.tv_nsec));
  printf("PI calculated with %d terms: %.15f\n", PRECISION + 1, bpp_sum);
  return 0;
}
