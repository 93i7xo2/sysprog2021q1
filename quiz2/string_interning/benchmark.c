#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "cstr.h"
#include "unsigned.h"

typedef struct __data {
    int val;
    cstring str;
} data;

// void* insert_cstr(void *__input){
//     char buffer[12] = {0};
//     data* input = (data*) __input;
//     unsigned_string(buffer, input->val);
//     input->str = cstr_clone(buffer, strlen(buffer));
// }


int main(int argc, char *argv[])
{
    size_t count = 5000000;
    // pthread_t* threads = (pthread_t *) malloc(sizeof(pthread_t) * count);
    data* ret = (data *) malloc(sizeof(data) * count);

    // for (uint32_t i=0;i<count;++i){
    //     ret[i].val = i;
    //     pthread_create(&threads[i], NULL, insert_cstr, (void*) &ret[i]);
    // }

    // for (uint32_t i=0;i<count;++i){
    //     pthread_join(threads[i], NULL);
    // }

    for (uint32_t i=0;i<count;++i){
        char buffer[12] = {0};
        data* input = (data*) &ret[i];
        unsigned_string(buffer, i);
        input->str = cstr_clone(buffer, strlen(buffer));
    }

    char buffer[12] = {0};
    size_t string_bytes = 0;

    for (uint32_t i=0;i<count;++i){
        cstring expected = ret[i].str;
        unsigned_string(buffer, i);
        string_bytes += strlen(buffer);

        assert(expected->type == CSTR_INTERNING);
        assert(strncmp(buffer, expected->cstr, strlen(buffer)+1 ) == 0);
    }
    
    string_bytes += count; // null character
    size_t allocated_bytes = strings_allocated_bytes();
    size_t overhead = allocated_bytes - string_bytes;
    double overhead_per_string = (double)overhead / (double)count;

    printf("Interned %lu unique strings\n", count);
    printf("Overhead per string: %.1f bytes\n", overhead_per_string);

    free(ret);
    return 0;
}