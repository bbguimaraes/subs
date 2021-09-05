#ifndef SUBS_BUFFER_H
#define SUBS_BUFFER_H

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define BUFFER_APPEND(b, x) (buffer_append((b), (x), sizeof(*(x))))

struct buffer {
    /* Head of the array. */
    void *p;
    /** Number of bytes currently in use. */
    size_t n;
    /** Number of bytes allocated, `>=n`. */
    size_t cap;
};

static struct buffer buffer_new(size_t size);
static void buffer_destroy(struct buffer *b);
static void buffer_reserve(struct buffer *b, size_t n);
static void buffer_resize(struct buffer *b, size_t n);
static void buffer_append(struct buffer *b, const void *s, size_t n);
static void buffer_append_str(struct buffer *b, const char *s);
void buffer_printf(struct buffer *b, const char *restrict fmt, ...);

static inline struct buffer buffer_new(size_t size) {
    return (struct buffer){.p = checked_malloc(size), .n = 0, .cap = size};
}

static inline void buffer_destroy(struct buffer *b) {
    free(b->p);
    *b = (struct buffer){0};
}

static inline void buffer_reserve(struct buffer *b, size_t n) {
    size_t cap = b->cap ? b->cap : 1;
    while(cap < n)
        cap *= 2;
    b->p = realloc(b->p, cap);
    b->cap = cap;
}

static inline void buffer_resize(struct buffer *b, size_t n) {
    buffer_reserve(b, n);
    b->n = n;
}

static inline void buffer_append(struct buffer *b, const void *p, size_t n) {
    const size_t bn = b->n, new_n = bn + n;
    if(new_n <= b->cap - bn) {
        memcpy((char*)b->p + bn, p, n);
        b->n = new_n;
        return;
    }
    buffer_reserve(b, new_n);
    memcpy((char*)b->p + bn, p, n);
    b->n = new_n;
}

static inline void buffer_append_str(struct buffer *b, const char *s) {
    size_t n = b->n;
    const size_t max = b->cap - n;
    void *const p = (char*)b->p + n;
    size_t nw = strlcpy(p, s, max);
    if(nw && !s[nw - 1]) {
        b->n += nw;
        return;
    }
    s += nw;
    n += nw;
    const size_t len = strlen(s) + 1;
    buffer_reserve(b, n + len);
    nw = strlcpy((char*)b->p + n, s, len);
    assert(nw == len);
    b->n = n + len;
}

#endif
