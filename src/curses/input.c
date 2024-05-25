#include "input.h"

#include <assert.h>

#include <curses.h>
#include <unistd.h>

#include "../log.h"
#include "../unix.h"
#include "../util.h"

#include "const.h"

bool input_init(struct input *i) {
    const int sig_fd = setup_signalfd(make_signal_mask(SIGWINCH, 0));
    if(sig_fd == -1)
        return false;
    int event_r, event_w;
    if(!setup_pipe(&event_r, &event_w))
        goto e0;
    i->sig_fd = sig_fd;
    i->event_r = event_r;
    i->event_w = event_w;
    return true;
e0:
    if(close(sig_fd))
        LOG_ERRNO("close", 0);
    return false;
}

bool input_destroy(struct input *i) {
    bool ret = true;
    if(close(i->sig_fd))
        LOG_ERRNO("close", 0), ret = false;
    if(close(i->event_r))
        LOG_ERRNO("close", 0), ret = false;
    if(close(i->event_w))
        LOG_ERRNO("close", 0), ret = false;
    return ret;
}

struct input_event input_process(const struct input *i) {
    const int sig_fd = i->sig_fd, event_fd = i->event_r;
    const int fds[] = {STDIN_FILENO, sig_fd, event_fd};
    int fd = -1;
    switch(poll_input(ARRAY_SIZE(fds), fds, &fd)) {
    case INPUT_FD:
        break;
    case INPUT_CLOSED:
        return EVENT(QUIT);
    default:
        return EVENT(ERR);
    }
    if(fd == STDIN_FILENO) {
        const int key = getch();
        switch(key) {
        case ERR:
            return EVENT(ERR);
        case 'c' & CTRL:
            return EVENT(QUIT);
        default:
            return EVENT(KEY, .key = key);
        }
    }
    if(fd == sig_fd)
        return process_signalfd(fd)
            ? EVENT(RESIZE)
            : EVENT(ERR);
    if(fd == event_fd) {
        struct input_event e;
        const ssize_t n = read(i->event_r, &e, sizeof(e));
        switch(n) {
        case -1:
            return LOG_ERRNO("read", 0), EVENT(ERR);
        case sizeof(e):
            return e;
        default:
            LOG_ERR("short read: %zd != %zu\n", n, sizeof(e));
            return EVENT(ERR);
        }
    }
    assert(!"invalid file descriptor");
}

bool input_send_event(struct input *i, struct input_event e) {
    const ssize_t n = write(i->event_w, &e, sizeof(e));
    switch(n) {
    case -1:
        return LOG_ERRNO("write", 0), false;
    case sizeof(e):
        return true;
    default:
        return LOG_ERR("short write: %zd != %zu\n", n, sizeof(e)), false;
    }
}
