#ifndef SUBS_UNIX_H
#define SUBS_UNIX_H

#include <stdbool.h>

#include <unistd.h>

bool setup_pipe(int *r, int *w);
bool setup_bidirectional_pipe(int *r0, int *w0, int *r1, int *w1);
bool exec_with_pipes(
    const char *file, const char *const *argv,
    pid_t *pid, int *r, int *w);
bool wait_for_pid(pid_t pid);

#endif
