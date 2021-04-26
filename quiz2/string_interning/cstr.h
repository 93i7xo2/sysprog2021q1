#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

enum {
    CSTR_PERMANENT = 1,
    CSTR_INTERNING = 2,
    CSTR_ONSTACK = 4,
};

#define CSTR_INTERNING_SIZE (32)
#define CSTR_STACK_SIZE (128)

typedef struct __cstr_data {
    char *cstr;
    uint32_t hash_size;
    uint16_t type;
    uint16_t ref;
} * cstring;

typedef struct __cstr_buffer {
    cstring str;
} cstr_buffer[1];

#define CSTR_S(s) ((s)->str)

#define CSTR_BUFFER(var)                                                      \
    char var##_cstring[CSTR_STACK_SIZE] = {0};                                \
    struct __cstr_data var##_cstr_data = {var##_cstring, 0, CSTR_ONSTACK, 0}; \
    cstr_buffer var;                                                          \
    var->str = &var##_cstr_data;

#define CSTR_LITERAL(var, cstr)                                               \
    static cstring var = NULL;                                                \
    if (!var) {                                                               \
        cstring tmp = cstr_clone("" cstr, (sizeof(cstr) / sizeof(char)) - 1); \
        if (tmp->type == 0) {                                                 \
            tmp->type = CSTR_PERMANENT;                                       \
            tmp->ref = 0;                                                     \
        }                                                                     \
        if (!__sync_bool_compare_and_swap(&var, NULL, tmp)) {                 \
            if (tmp->type == CSTR_PERMANENT)                                  \
                free(tmp);                                                    \
        }                                                                     \
    }

#define CSTR_CLOSE(var)               \
    do {                              \
        if (!(var)->str->type)        \
            cstr_release((var)->str); \
    } while (0)

/* Public API */
cstring cstr_grab(cstring s);
cstring cstr_clone(const char *cstr, size_t sz);
cstring cstr_cat(cstr_buffer sb, const char *str);
int cstr_equal(cstring a, cstring b);
void cstr_release(cstring s);
size_t strings_allocated_bytes();