#ifndef SUBS_UNIX_H
#define SUBS_UNIX_H

#include <stdbool.h>

#include <poll.h>
#include <signal.h>
#include <unistd.h>

enum input_result {
    INPUT_FD,
    INPUT_CLOSED,
    INPUT_ERR,
};

bool setup_pipe(int *r, int *w);
bool setup_bidirectional_pipe(int *r0, int *w0, int *r1, int *w1);
sigset_t make_signal_mask(int s, ...);
int setup_signalfd(sigset_t mask);
bool process_signalfd(int fd);
bool exec_with_pipes(
    const char *file, const char *const *argv,
    pid_t *pid, int *r, int *w);
bool wait_for_pid(pid_t pid);
bool get_terminal_size(int *x, int *y);
enum input_result poll_input(nfds_t n, const int fds[static n], int *fd);

#endif
