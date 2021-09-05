#ifndef SUBS_LOG_H
#define SUBS_LOG_H

#include <stdarg.h>
#include <stdio.h>

#define LOG_ERR(fmt, ...) \
    log_err("%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERRNO(fmt, ...) \
    log_errno("%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, __VA_ARGS__)

/** Program name, used as a prefix for log messages if non-`NULL`. */
extern const char *PROG_NAME;

/** Command name, used as a second prefix for log messages if non-`NULL`. */
extern const char *CMD_NAME;

/** Sets the output stream used by log functions and returns previous value. */
FILE *log_set(FILE *f);

typedef void (log_fn)(const char *fmt, va_list);
/** Sets an alternative function to process log messages. */
log_fn *log_set_fn(log_fn *f);

/**
 * Writes the printf-style message to the error output.
 * If non-null, \ref PROG_NAME and \ref CMD_NAME are prepended.
 */
void log_err(const char *fmt, ...);

/** \see log_err */
void log_errv(const char *fmt, va_list argp);

/**
 * Similar to \ref log_err, but also logs `strerror(errno)`.
 * `: %s\n` is appended, where `%s` is the result of `strerror(errno)`.  `errno`
 * is cleared.
 */
void log_errno(const char *fmt, ...);

#endif
