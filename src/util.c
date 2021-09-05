#include "util.h"

#include <assert.h>
#include <stdarg.h>
#include "log.h"

char *sprintf_alloc(const char *restrict fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char *const ret = vsprintf_alloc(fmt, args);
    va_end(args);
    return ret;
}

char *vsprintf_alloc(const char *restrict fmt, va_list args0) {
    va_list args1;
    va_copy(args1, args0);
    const int n = vsnprintf(NULL, 0, fmt, args0);
    char *const ret = checked_malloc((size_t)(n + 1));
    if(ret) {
        const int nw = vsprintf(ret, fmt, args1);
        assert(n == nw);
    }
    va_end(args1);
    return ret;
}

char *join_path(char v[static SUBS_MAX_PATH], int n, ...) {
    va_list args;
    va_start(args, n);
    char *p = v, *const e = v + SUBS_MAX_PATH;
    for(int i = 0; i != n; ++i) {
        size_t ni = strlcpy(p, va_arg(args, const char*), (size_t)(e - p));
        if(e[-1])
            goto err;
        p += ni - 1;
    }
    assert(p <= e);
    assert(!e[-1]);
    va_end(args);
    return v;
err:
    va_end(args);
    log_err("%s: path too long: %s\n", __func__, v);
    return NULL;
}

bool file_exists(const char *name) {
    FILE *const f = fopen(name, "r");
    if(!f)
        return false;
    fclose(f);
    return true;
}
