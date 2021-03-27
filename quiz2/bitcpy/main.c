#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "bitcpy.h"

#define CLOCK_ID CLOCK_MONOTONIC_RAW
#define ONE_SEC 1000000000.0
#define BUFFER_SIZE 8 << 10 // 8 kB
#define OFFSET sizeof(uint64_t)
#define COUNT_MAX ((BUFFER_SIZE>>1) << 3) // 4k*8 bits
#define ROUND 100

static uint8_t output[BUFFER_SIZE], input[BUFFER_SIZE], tmp[BUFFER_SIZE];

static inline void dump_8bits(uint8_t _data)
{
    for (int i = 0; i < 8; ++i)
        printf("%d", (_data & (0x80 >> i)) ? 1 : 0);
}

static inline void dump_binary(uint8_t *_buffer, size_t _length)
{
    for (int i = 0; i < _length; ++i)
        dump_8bits(*_buffer++);
}

int main(int _argc, char **_argv)
{
    struct timespec start = {0, 0};
    struct timespec end = {0, 0};

    memset(&input[0], 0xFF, sizeof(input));

    for (int i = 1; i <= COUNT_MAX; ++i)
    {
        printf("%d ", i);

        int j = rand() % 64, k = rand() % 64;

        // Baseline
        clock_gettime(CLOCK_ID, &start);
        for (int c = 0; c < ROUND; ++c)
        {
            memset(&output[0], 0x00, sizeof(output));
            bitcpy(&output[0], k, &input[0], j, i);
        }
        clock_gettime(CLOCK_ID, &end);
        printf("%lf ", (double)(end.tv_sec - start.tv_sec) +
                           (end.tv_nsec - start.tv_nsec) / ONE_SEC);

        memcpy(tmp, output, BUFFER_SIZE);

        // Reading or writing 64-bit at a time
        clock_gettime(CLOCK_ID, &start);
        for (int c = 0; c < ROUND; ++c)
        {
            memset(&output[0], 0x00, sizeof(output));
            bitcpy64(&output[0], k, &input[0], j, i);
        }
        clock_gettime(CLOCK_ID, &end);
        printf("%lf\n", (double)(end.tv_sec - start.tv_sec) +
                           (end.tv_nsec - start.tv_nsec) / ONE_SEC);

        assert(memcmp(tmp, output, BUFFER_SIZE) == 0);
    }

    return 0;
}