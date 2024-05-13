#include "unix.h"

#include <assert.h>
#include <errno.h>

#include <alloca.h>
#include <sys/wait.h>

#include "log.h"

bool setup_pipe(int *r, int *w) {
    union { int a[2]; struct { int r, w; };} p;
    if(pipe(p.a) == -1)
        return LOG_ERRNO("pipe", 0), false;
    *r = p.r, *w = p.w;
    return true;
}

bool setup_bidirectional_pipe(int *r0, int *w0, int *r1, int *w1) {
    int tmp_r0, tmp_w0;
    if(!setup_pipe(&tmp_r0, &tmp_w0))
        return false;
    int tmp_r1, tmp_w1;
    if(!setup_pipe(&tmp_r1, &tmp_w1)) {
        if(close(tmp_r0) == -1)
            LOG_ERRNO("close", 0);
        if(close(tmp_w0) == -1)
            LOG_ERRNO("close", 0);
        return false;
    }
    *r0 = tmp_r0, *w0 = tmp_w0;
    *r1 = tmp_r1, *w1 = tmp_w1;
    return true;
}

bool exec_with_pipes(
    const char *file, const char *const *argv,
    pid_t *pid, int *r, int *w)
{
    int parent_r, parent_w, child_r, child_w;
    if(!setup_bidirectional_pipe(&parent_r, &parent_w, &child_r, &child_w))
        return false;
    *r = child_r;
    *w = parent_w;
    const pid_t p = fork();
    if(p == -1)
        return LOG_ERRNO("fork", 0), false;
    if(p) {
        *pid = p;
        bool ret = true;
        ret = close(parent_r) != -1 && ret;
        ret = close(child_w) != -1 && ret;
        return ret;
    } else {
        if(close(parent_w) == -1)
            LOG_ERRNO("close", 0), _exit(1);
        if(close(child_r) == -1)
            LOG_ERRNO("close", 0), _exit(1);
        if(dup2(parent_r, 0) == -1)
            LOG_ERRNO("dup2", 0), _exit(1);
        if(dup2(child_w, 1) == -1)
            LOG_ERRNO("dup2", 0), _exit(1);
        execvp(file, (char *const*)argv);
        LOG_ERRNO("execlp", 0);
        _exit(1);
    }
}

bool wait_for_pid(pid_t pid) {
    int status;
    if(waitpid(pid, &status, 0) == -1) {
        LOG_ERRNO("wait", 0);
        return false;
    }
    if(WIFSIGNALED(status)) {
        LOG_ERR("child killed by signal: %d\n", WTERMSIG(status));
        return false;
    }
    if(WIFEXITED(status)) {
        if((status = WEXITSTATUS(status))) {
            LOG_ERR("child exited: %d\n", status);
            return false;
        }
        return true;
    }
    LOG_ERR("waitpid: unknown status: 0x%x\n", status);
    return false;
}

enum input_result poll_input(nfds_t n, const int fds[static n], int *fd) {
    struct pollfd *const v = alloca(n * sizeof(*v));
    for(nfds_t i = 0; i != n; ++i)
        v[i] = (struct pollfd){.events = POLLIN, .fd = fds[i]};
    enum input_result ret = -1;
retry: ;
    int n_events = poll(v, n, -1);
    if(n_events == -1) {
        if(errno == EINTR)
            goto retry;
        return LOG_ERRNO("poll", 0), INPUT_ERR;
    }
    for(size_t i = 0; n_events; ++i) {
        assert(i < n);
        const short revents = v[i].revents;
        if(!revents)
            continue;
        --n_events;
        v[i].revents = 0;
        if(revents & POLLERR) {
            LOG_ERR("poll: POLLERR: %d\n", v[i].fd);
            ret = INPUT_ERR;
        } else if(revents & POLLIN)
            *fd = v[i].fd, ret = INPUT_FD;
        else if(revents & POLLHUP) {
            if(ret != INPUT_ERR)
                *fd = v[i].fd, ret = INPUT_CLOSED;
        }
    }
    assert((int)ret != -1);
    return ret;
}
