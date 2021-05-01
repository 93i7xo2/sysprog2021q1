#include "xs.h"

int main(int argc, char *argv[])
{
    xs short_string = *xs_tmp("\n 0123456789 \n");
    xs short_string_dup = *xs_copy(&short_string);
    printf("[%p]\n", xs_data(&short_string));
    printf("[%p]\n", xs_data(&short_string_dup));

    xs large_string = *xs_tmp("");
    for (int i = 0; i < 16; ++i)
        xs_concat(&large_string, xs_tmp(""), xs_tmp(" 0123456789abcde"));
    xs large_string_dup = *xs_copy(&large_string);
    printf("[%p]\n", xs_data(&large_string));
    printf("[%p]\n", xs_data(&large_string_dup));
    return 0;
}