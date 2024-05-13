#include "input.h"

#include <assert.h>

#include <curses.h>
#include <unistd.h>

#include "../log.h"
#include "../unix.h"
#include "../util.h"

#include "const.h"

#define EVENT(...) (struct input_event){ .type = INPUT_TYPE_ ## __VA_ARGS__ }

bool input_init(struct input *i) {
    const int sig_fd = setup_signalfd(make_signal_mask(SIGWINCH, 0));
    if(sig_fd == -1)
        return false;
    i->sig_fd = sig_fd;
    return true;
}

bool input_destroy(struct input *i) {
    bool ret = true;
    if(close(i->sig_fd))
        LOG_ERRNO("close", 0), ret = false;
    return ret;
}

struct input_event input_process(const struct input *i) {
    const int sig_fd = i->sig_fd;
    const int fds[] = {STDIN_FILENO, sig_fd};
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
    assert(!"invalid file descriptor");
}
