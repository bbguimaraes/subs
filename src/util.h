#ifndef SUBS_UTIL_H
#define SUBS_UTIL_H

#include <errno.h>
#include <inttypes.h>
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
#define FOR_EACH_ARRAY(type, var, array) \
    for(type *var = array; var != (array) + ARRAY_SIZE(array); ++var)
#define FOR_EACH(type, var, init) for(type *var = (init); *var; ++var)

static void *checked_malloc(size_t n);

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

static inline i64 parse_i64(const char *s) {
    const char *e;
    errno = 0;
    const u64 ret = strtoull(s, (char**)&e, 10);
    if(errno) {
        errno = 0;
        fprintf(stderr, "strtoull: %s\n", strerror(errno));
        return -1;
    }
    if(e == s) {
        fprintf(stderr, "failed to parse i64: %s\n", s);
        return -1;
    }
    if(ret > INT64_MAX) {
        fprintf(stderr, "i64 value too large: %" PRIu64 "\n", ret);
        return -1;
    }
    return (i64)ret;
}

#endif