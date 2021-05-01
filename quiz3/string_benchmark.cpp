#include <cstring>
#include <string>
#include <iostream>
#include <ctime>
#include <unistd.h>

#define CLOCK_ID CLOCK_MONOTONIC_RAW
#define ONE_SEC 1e9

#define SIZE 131072

// https://stackoverflow.com/questions/822323/how-to-generate-a-random-int-in-c
std::string gen_random(const int len)
{

    std::string tmp_s;
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    srand((unsigned)time(NULL) * getpid());

    tmp_s.reserve(len);

    for (int i = 0; i < len; ++i)
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];

    return tmp_s;
}

int main(int argc, char *argv[])
{
    size_t count;
    std::string str[SIZE];
    int32_t *tmp[SIZE] = {0};

    struct timespec start = {0, 0};
    struct timespec end = {0, 0};

    for (int i = 0; i < SIZE; ++i)
    {
        str[i] = gen_random(20);
        tmp[i] = new int(0);
    }

    for (int i = 0; i < SIZE; ++i)
    {
        ++*tmp[i];
    }

    clock_gettime(CLOCK_ID, &start);
    for (int i = 0; i < 1000; ++i)
    {
        str[rand() % SIZE][20] = 'A';
    }
    clock_gettime(CLOCK_ID, &end);
    long long utime = (double)(end.tv_sec - start.tv_sec) * ONE_SEC + (end.tv_nsec - start.tv_nsec);
    std::cout << utime << std::endl;

    for (int i = 0; i < SIZE; ++i)
    {
        delete tmp[i];
    }
    return 0;
}