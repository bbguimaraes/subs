#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <string.h>

void log_errv(const char *fmt, va_list argp);

static FILE *LOG_OUT = NULL;
static log_fn *LOG_FN = log_errv;

FILE *log_set(FILE *f) {
    FILE *const ret = LOG_OUT;
    LOG_OUT = f;
    return ret;
}

log_fn *log_set_fn(log_fn *f) {
    log_fn *const ret = LOG_FN;
    LOG_FN = f;
    return ret;
}

void log_errv(const char *fmt, va_list argp) {
    if(PROG_NAME)
        fprintf(LOG_OUT, "%s: ", PROG_NAME);
    if(CMD_NAME)
        fprintf(LOG_OUT, "%s: ", CMD_NAME);
    vfprintf(LOG_OUT, fmt, argp);
}

void log_err(const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    LOG_FN(fmt, argp);
    va_end(argp);
}

void log_errno(const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    log_errv(fmt, argp);
    va_end(argp);
    log_err(": %s\n", strerror(errno));
    errno = 0;
}
