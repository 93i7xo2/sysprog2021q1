#include "xs.h"
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>

#define CLOCK_ID CLOCK_MONOTONIC_RAW
#define ONE_SEC 1e9

#define SIZE 131072

int main(int argc, char *argv[])
{
    uint8_t buf[21] = {0};
    size_t count;
    xs string[SIZE];
    int32_t *tmp[SIZE];

    struct timespec start = {0, 0};
    struct timespec end = {0, 0};

    int fd = open("/dev/urandom", O_RDONLY);
    for (int i = 0; i < SIZE; ++i)
    {
        count = read(fd, buf, sizeof(buf) - 1);
        string[i] = *xs_tmp((const void *)buf);
        tmp[i] = (int32_t *)malloc(sizeof(int32_t));
    }
    close(fd);

    srand(time(NULL));

    for (int i = 0; i < SIZE; ++i)
    {
        ++*tmp[i];
    }

    clock_gettime(CLOCK_ID, &start);
    for (int i = 0; i < 1000; ++i)
    {
        xs_data(&string[rand() % SIZE])[20] = 'A';
    }
    clock_gettime(CLOCK_ID, &end);
    long long utime = (double)(end.tv_sec - start.tv_sec) * ONE_SEC + (end.tv_nsec - start.tv_nsec);
    printf("%lld\n", utime);

    for (int i = 0; i < SIZE; ++i)
    {
        free(tmp[i]);
    }
    return 0;
}