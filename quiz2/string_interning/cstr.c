#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cstr.h"

#define INTERNING_POOL_SIZE 1 << 23

#define HASH_START_SIZE 16 /* must be power of 2 */

struct __cstr_node
{
    char buffer[CSTR_INTERNING_SIZE];
    struct __cstr_data str;
    struct __cstr_node *next;
};

struct __cstr_pool
{
    struct __cstr_node node[INTERNING_POOL_SIZE];
};

struct __cstr_interning
{
    int lock;
    int index;
    unsigned size;
    unsigned total;
    struct __cstr_node **hash;
    struct __cstr_pool *pool;
};

static struct __cstr_interning __cstr_ctx;

/* FIXME: use C11 atomics */
#define CSTR_LOCK()                                             \
    ({                                                          \
        while (__sync_lock_test_and_set(&(__cstr_ctx.lock), 1)) \
        {                                                       \
        }                                                       \
    })
#define CSTR_UNLOCK() ({ __sync_lock_release(&(__cstr_ctx.lock)); })

static void *xalloc(size_t n)
{
    void *m = malloc(n);
    if (!m)
        exit(-1);
    return m;
}

static inline void insert_node(struct __cstr_node **hash,
                               int sz,
                               struct __cstr_node *node)
{
    uint32_t h = node->str.hash_size;
    int index = h & (sz - 1);
    node->next = hash[index];
    hash[index] = node;
}

static void expand(struct __cstr_interning *si)
{
    unsigned new_size = si->size * 2;
    if (new_size < HASH_START_SIZE)
        new_size = HASH_START_SIZE;

    struct __cstr_node **new_hash =
        xalloc(sizeof(struct __cstr_node *) * new_size);
    memset(new_hash, 0, sizeof(struct __cstr_node *) * new_size);

    for (unsigned i = 0; i < si->size; ++i)
    {
        struct __cstr_node *node = si->hash[i];
        while (node)
        {
            struct __cstr_node *tmp = node->next;
            insert_node(new_hash, new_size, node);
            node = tmp;
        }
    }

    free(si->hash);
    si->hash = new_hash;
    si->size = new_size;
}

static cstring interning(struct __cstr_interning *si,
                         const char *cstr,
                         size_t sz,
                         uint32_t hash)
{
    if (!si->hash)
        return NULL;

    int index = (int)(hash & (si->size - 1));
    struct __cstr_node *n = si->hash[index];
    while (n)
    {
        if (n->str.hash_size == hash)
        {
            if (!strcmp(n->str.cstr, cstr))
                return &n->str;
        }
        n = n->next;
    }
    // 80% (4/5) threshold
    if (si->total * 5 >= si->size * 4)
        return NULL;
    if (!si->pool)
    {
        si->pool = xalloc(sizeof(struct __cstr_pool));
        si->index = 0;
    }
    n = &si->pool->node[si->index++];
    memcpy(n->buffer, cstr, sz);
    n->buffer[sz] = 0;

    cstring cs = &n->str;
    cs->cstr = n->buffer;
    cs->hash_size = hash;
    cs->type = CSTR_INTERNING;
    cs->ref = 0;

    n->next = si->hash[index];
    si->hash[index] = n;

    ++__cstr_ctx.total;

    return cs;
}

static cstring cstr_interning(const char *cstr, size_t sz, uint32_t hash)
{
    cstring ret;
    CSTR_LOCK();
    ret = interning(&__cstr_ctx, cstr, sz, hash);
    if (!ret)
    {
        expand(&__cstr_ctx);
        ret = interning(&__cstr_ctx, cstr, sz, hash);
    }
    CSTR_UNLOCK();
    return ret;
}

static inline uint32_t hash_blob(const char *buffer, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)buffer;
    size_t h = len;
    size_t step = (len >> 5) + 1;
    for (size_t i = len; i >= step; i -= step)
        h = h ^ ((h << 5) + (h >> 2) + ptr[i - 1]);
    return h == 0 ? 1 : h;
}

cstring cstr_clone(const char *cstr, size_t sz)
{
    if (sz < CSTR_INTERNING_SIZE)
        return cstr_interning(cstr, sz, hash_blob(cstr, sz));
    cstring p = xalloc(sizeof(struct __cstr_data) + sz + 1);
    if (!p)
        return NULL;
    void *ptr = (void *)(p + 1);
    p->cstr = ptr;
    p->type = 0;
    p->ref = 1;
    memcpy(ptr, cstr, sz);
    ((char *)ptr)[sz] = 0;
    p->hash_size = 0;
    return p;
}

cstring cstr_grab(cstring s)
{
    if (s->type & (CSTR_PERMANENT | CSTR_INTERNING))
        return s;
    if (s->type == CSTR_ONSTACK)
        return cstr_clone(s->cstr, s->hash_size);
    if (s->ref == 0)
        s->type = CSTR_PERMANENT;
    else
        __sync_add_and_fetch(&s->ref, 1);
    return s;
}

void cstr_release(cstring s)
{
    if (s->type || !s->ref)
        return;
    if (__sync_sub_and_fetch(&s->ref, 1) == 0)
        free(s);
}

static size_t cstr_hash(cstring s)
{
    if (s->type == CSTR_ONSTACK)
        return hash_blob(s->cstr, s->hash_size);
    if (s->hash_size == 0)
        s->hash_size = hash_blob(s->cstr, strlen(s->cstr));
    return s->hash_size;
}

int cstr_equal(cstring a, cstring b)
{
    if (a == b)
        return 1;
    if ((a->type == CSTR_INTERNING) && (b->type == CSTR_INTERNING))
        return 0;
    if ((a->type == CSTR_ONSTACK) && (b->type == CSTR_ONSTACK))
    {
        if (a->hash_size != b->hash_size)
            return 0;
        return memcmp(a->cstr, b->cstr, a->hash_size) == 0;
    }
    uint32_t hasha = cstr_hash(a);
    uint32_t hashb = cstr_hash(b);
    if (hasha != hashb)
        return 0;
    return !strcmp(a->cstr, b->cstr);
}

static cstring cstr_cat2(const char *a, const char *b)
{
    size_t sa = strlen(a), sb = strlen(b);
    if (sa + sb < CSTR_INTERNING_SIZE)
    {
        char tmp[CSTR_INTERNING_SIZE];
        memcpy(tmp, a, sa);
        memcpy(tmp + sa, b, sb);
        tmp[sa + sb] = 0;
        return cstr_interning(tmp, sa + sb, hash_blob(tmp, sa + sb));
    }
    cstring p = xalloc(sizeof(struct __cstr_data) + sa + sb + 1);
    if (!p)
        return NULL;

    char *ptr = (char *)(p + 1);
    p->cstr = ptr;
    p->type = 0;
    p->ref = 1;
    memcpy(ptr, a, sa);
    memcpy(ptr + sa, b, sb);
    ptr[sa + sb] = 0;
    p->hash_size = 0;
    return p;
}

cstring cstr_cat(cstr_buffer sb, const char *str)
{
    cstring s = sb->str;
    if (s->type == CSTR_ONSTACK)
    {
        int i = s->hash_size;
        while (i < CSTR_STACK_SIZE - 1)
        {
            s->cstr[i] = *str;
            if (*str == 0)
                return s;
            ++s->hash_size;
            ++str;
            ++i;
        }
        s->cstr[i] = 0;
    }
    cstring tmp = s;
    sb->str = cstr_cat2(tmp->cstr, str);
    cstr_release(tmp);
    return sb->str;
}

size_t strings_allocated_bytes()
{
    size_t s_pool = 0, s_table = 0, s_ctx = 0;
    CSTR_LOCK();
    if (__cstr_ctx.pool){
        s_pool = sizeof(*__cstr_ctx.pool);
        printf("pool: %ld bytes\n", s_pool);
    }
    s_table = sizeof(struct __cstr_node *) * __cstr_ctx.size;
    printf("hash table: %ld bytes\n", s_table);
    s_ctx = sizeof(__cstr_ctx);
    printf("ctx: %ld bytes\n", s_ctx);
    CSTR_UNLOCK();
    return s_pool + s_table + s_ctx;
}