#ifndef SUBS_UTIL_H
#define SUBS_UTIL_H

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "const.h"
#include "def.h"
#include "log.h"

#define MIN(x, y) ((y) < (x) ? (y) : (x))
#define MAX(x, y) ((y) > (x) ? (y) : (x))
#define CLAMP(x, min, max) (MIN((max), MAX((min), (x))))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))
#define ARRAY_END(x) ((x) + ARRAY_SIZE((x)))
#define IN_ARRAY(x, p, n) ((p) <= (x) && (x) < ((p) + (n)))
#define FOR_EACH_ARRAY(type, var, array) \
    for(type *var = array; var != (array) + ARRAY_SIZE(array); ++var)
#define FOR_EACH(type, var, init) for(type *var = (init); *var; ++var)

static void *checked_malloc(size_t n);
static void *checked_calloc(size_t n, size_t s);
static bool checked_calloc_p(size_t n, size_t s, void **p);
static bool checked_realloc(size_t s, void **p);

static size_t strlen_utf8(const char *s);
static size_t strlcpy(char *restrict dst, const char *restrict src, size_t n);
static i64 parse_i64(const char *s);
char *sprintf_alloc(const char *restrict fmt, ...);
char *vsprintf_alloc(const char *restrict fmt, va_list args);
char *join_path(char v[static SUBS_MAX_PATH], int n, ...);
bool file_exists(const char *name);

static inline void *checked_malloc(size_t n) {
    void *const ret = malloc(n);
    if(!ret)
        LOG_ERRNO("malloc", 0);
    return ret;
}

static inline void *checked_calloc(size_t n, size_t s) {
    void *ret = NULL;
    checked_calloc_p(n, s, &ret);
    return ret;
}

static inline bool checked_calloc_p(size_t n, size_t s, void **p) {
    void *const ret = calloc(n, s);
    if(!ret)
        return LOG_ERRNO("calloc", 0), false;
    *p = ret;
    return true;
}

static inline bool checked_realloc(size_t s, void **p) {
    void *const ret = realloc(*p, s);
    if(!ret)
        return LOG_ERRNO("realloc", 0), false;
    *p = ret;
    return true;
}

static inline size_t strlen_utf8(const char *s) {
    size_t ret = 0;
    for(; *s; ++s)
        ret += (*s & 0xc0) != 0x80;
    return ret;
}

static inline size_t strlcpy(
    char *restrict dst, const char *restrict src, size_t n)
{
    const char *const p = dst;
    while(n && (*dst++ = *src++))
        --n;
    return (size_t)(dst - p);
}

static inline i64 parse_int_common(u64 max, const char *name, const char *s) {
    const char *e;
    errno = 0;
    const u64 ret = strtoull(s, (char**)&e, 10);
    if(errno) {
        errno = 0;
        fprintf(stderr, "strtoull: %s\n", strerror(errno));
        return -1;
    }
    if(e == s) {
        fprintf(stderr, "failed to parse %s: %s\n", name, s);
        return -1;
    }
    if(ret > max) {
        fprintf(stderr, "%s value too large: %" PRIu64 "\n", name, ret);
        return -1;
    }
    return (i64)ret;
}

static inline int parse_int(const char *s) {
    return (int)parse_int_common(INT_MAX, "int", s);
}

static inline i64 parse_i64(const char *s) {
    return parse_int_common(INT64_MAX, "i64", s);
}

#endif
