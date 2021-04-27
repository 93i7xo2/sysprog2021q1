#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_STR_LEN_BITS (54)
#define MAX_STR_LEN ((1UL << MAX_STR_LEN_BITS) - 1)

#define LARGE_STRING_LEN 256

#define XS_32
#ifdef XS_32
#define SHORT_STRING_LEN 31
#else
#define SHORT_STRING_LEN 15
#endif

typedef union
{
    /* allow strings up to 15 bytes to stay on the stack
     * use the last byte as a null terminator and to store flags
     * much like fbstring:
     * https://github.com/facebook/folly/blob/master/folly/docs/FBString.md
     */
    char data[SHORT_STRING_LEN + 1];

    struct
    {
        uint8_t filler[SHORT_STRING_LEN],
        /* how many free bytes in this stack allocated string
             * same idea as fbstring
             */
#ifdef XS_32
            space_left : 5,
            /* if it is on heap, set to 1 */
            is_ptr : 1, is_large_string : 1, flag2 : 1;
#else
            space_left : 4,
            /* if it is on heap, set to 1 */
            is_ptr : 1, is_large_string : 1, flag2 : 1, flag3 : 1;
#endif
    };

    /* heap allocated */
    struct
    {
        char *ptr;
        /* supports strings up to 2^MAX_STR_LEN_BITS - 1 bytes */
        size_t size : MAX_STR_LEN_BITS,
        /* capacity is always a power of 2 (unsigned)-1 */
#ifdef XS_32
                      capacity : 7;
#else
                      capacity : 6;
        /* the last 4 bits are important flags */
#endif
    };
} xs;

static inline bool xs_is_ptr(const xs *x) { return x->is_ptr; }

static inline bool xs_is_large_string(const xs *x)
{
    return x->is_large_string;
}

static inline size_t xs_size(const xs *x)
{
    return xs_is_ptr(x) ? x->size : SHORT_STRING_LEN - x->space_left;
}

static inline char *xs_data(const xs *x)
{
    if (!xs_is_ptr(x))
        return (char *)x->data;

    if (xs_is_large_string(x))
        return (char *)(x->ptr + 4); // OFF
    return (char *)x->ptr;
}

static inline size_t xs_capacity(const xs *x)
{
    return xs_is_ptr(x) ? ((size_t)1 << x->capacity) - 1 : SHORT_STRING_LEN;
}

static inline void xs_set_refcnt(const xs *x, int val)
{
    *((int *)((size_t)x->ptr)) = val;
}

static inline void xs_inc_refcnt(const xs *x)
{
    if (xs_is_large_string(x))
        ++(*(int *)((size_t)x->ptr));
}

static inline int xs_dec_refcnt(const xs *x)
{
    if (!xs_is_large_string(x))
        return 0;
    return --(*(int *)((size_t)x->ptr));
}

static inline int xs_get_refcnt(const xs *x)
{
    if (!xs_is_large_string(x))
        return 0;
    return *(int *)((size_t)x->ptr);
}

#define xs_literal_empty() \
    (xs) { .space_left = SHORT_STRING_LEN }

/* lowerbound (floor log2) */
static inline int ilog2(uint32_t n) { return 32 - __builtin_clz(n) - 1; } // LLL

static void xs_allocate_data(xs *x, size_t len, bool reallocate)
{
    /* Medium string */
    if (len < LARGE_STRING_LEN)
    {
        x->ptr = reallocate ? realloc(x->ptr, (size_t)1 << x->capacity)
                            : malloc((size_t)1 << x->capacity);
        return;
    }

    /* Large string */
    x->is_large_string = 1;

    /* The extra 4 bytes are used to store the reference count */
    x->ptr = reallocate ? realloc(x->ptr, (size_t)(1 << x->capacity) + 4)
                        : malloc((size_t)(1 << x->capacity) + 4);

    xs_set_refcnt(x, 1);
}

xs *xs_new(xs *x, const void *p)
{
    *x = xs_literal_empty();
    size_t len = strlen(p) + 1;
    if (len > SHORT_STRING_LEN + 1)
    {
        x->capacity = ilog2(len) + 1;
        x->size = len - 1;
        x->is_ptr = true;
        xs_allocate_data(x, x->size, 0);
        memcpy(xs_data(x), p, len);
    }
    else
    {
        memcpy(x->data, p, len);
        x->space_left = SHORT_STRING_LEN - (len - 1);
    }
    return x;
}

/* Memory leaks happen if the string is too long but it is still useful for
 * short strings.
 */
#define xs_tmp(x)                                                   \
    ((void)((struct {                                               \
         _Static_assert(sizeof(x) <= MAX_STR_LEN, "it is too big"); \
         int dummy;                                                 \
     }){1}),                                                        \
     xs_new(&xs_literal_empty(), x))

/* grow up to specified size */
xs *xs_grow(xs *x, size_t len)
{
    char buf[SHORT_STRING_LEN + 1];

    if (len <= xs_capacity(x))
        return x;

    /* Backup first */
    if (!xs_is_ptr(x))
        memcpy(buf, x->data, SHORT_STRING_LEN + 1);

    x->capacity = ilog2(len) + 1;

    if (xs_is_ptr(x))
    {
        xs_allocate_data(x, len, 1);
    }
    else
    {
        x->is_ptr = true;
        xs_allocate_data(x, len, 0);
        memcpy(xs_data(x), buf, SHORT_STRING_LEN + 1);
    }
    return x;
}

static inline xs *xs_newempty(xs *x)
{
    *x = xs_literal_empty();
    return x;
}

static inline xs *xs_free(xs *x)
{
    if (xs_is_ptr(x) && xs_dec_refcnt(x) <= 0)
        free(x->ptr);
    return xs_newempty(x);
}

static bool xs_cow_lazy_copy(xs *x, char **data)
{
    if (xs_get_refcnt(x) <= 1)
        return false;

    /* Lazy copy */
    xs_dec_refcnt(x);
    xs_allocate_data(x, x->size, 0);

    if (data)
    {
        memcpy(xs_data(x), *data, x->size);

        /* Update the newly allocated pointer */
        *data = xs_data(x);
    }
    return true;
}

xs *xs_concat(xs *string, const xs *prefix, const xs *suffix)
{
    size_t pres = xs_size(prefix), sufs = xs_size(suffix),
           size = xs_size(string), capacity = xs_capacity(string);

    char *pre = xs_data(prefix), *suf = xs_data(suffix),
         *data = xs_data(string);

    xs_cow_lazy_copy(string, &data);

    if (size + pres + sufs <= capacity)
    {
        memmove(data + pres, data, size);
        memcpy(data, pre, pres);
        memcpy(data + pres + size, suf, sufs + 1);

        if (xs_is_ptr(string))
            string->size = size + pres + sufs;
        else
            string->space_left = SHORT_STRING_LEN - (size + pres + sufs);
    }
    else
    {
        xs tmps = xs_literal_empty();
        xs_grow(&tmps, size + pres + sufs);
        char *tmpdata = xs_data(&tmps);
        memcpy(tmpdata + pres, data, size);
        memcpy(tmpdata, pre, pres);
        memcpy(tmpdata + pres + size, suf, sufs + 1);
        xs_free(string);
        *string = tmps;
        string->size = size + pres + sufs;
    }
    return string;
}

xs *xs_trim(xs *x, const char *trimset)
{
    if (!trimset[0])
        return x;

    char *dataptr = xs_data(x), *orig = dataptr;

    if (xs_cow_lazy_copy(x, &dataptr))
        orig = dataptr;

    /* similar to strspn/strpbrk but it operates on binary data */
    uint8_t mask[32] = {0};

#define check_bit(byte) (mask[(uint8_t)byte / 8] & 1 << (uint8_t)byte % 8) // CCC
#define set_bit(byte) (mask[(uint8_t)byte / 8] |= 1 << (uint8_t)byte % 8)  // SSS
    size_t i, slen = xs_size(x), trimlen = strlen(trimset);

    for (i = 0; i < trimlen; i++)
        set_bit(trimset[i]);
    for (i = 0; i < slen; i++)
        if (!check_bit(dataptr[i]))
            break;
    for (; slen > 0; slen--)
        if (!check_bit(dataptr[slen - 1]))
            break;
    dataptr += i;
    slen -= i;

    /* reserved space as a buffer on the heap.
     * Do not reallocate immediately. Instead, reuse it as possible.
     * Do not shrink to in place if < 16 bytes.
     */
    memmove(orig, dataptr, slen);
    /* do not dirty memory unless it is needed */
    if (orig[slen])
        orig[slen] = 0;

    if (xs_is_ptr(x))
        x->size = slen;
    else
        x->space_left = SHORT_STRING_LEN - slen;
    return x;
#undef check_bit
#undef set_bit
}

xs *xs_copy(xs *x)
{
    xs *tmp = &xs_literal_empty();
    if (!xs_is_ptr(x))
    {
        memcpy(xs_data(tmp), x, SHORT_STRING_LEN + 1);
    }
    else
    {
        tmp->capacity = x->capacity;
        tmp->is_ptr = true;
        tmp->size = x->size;
        if (xs_is_large_string(x))
        {
            xs_inc_refcnt(x);
            tmp->is_large_string = true;
            tmp->ptr = x->ptr;
        }
        else
        {
            xs_allocate_data(tmp, x->size, 0);
            memcpy(xs_data(tmp), xs_data(x), x->size);
        }
    }
    return tmp;
}