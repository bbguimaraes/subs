#include "input.h"

#include <assert.h>

#include <curses.h>
#include <unistd.h>

#include "../unix.h"
#include "../util.h"

#define EVENT(...) (struct input_event){ .type = INPUT_TYPE_ ## __VA_ARGS__ }

enum {
    ESC = 0x1b,
};

bool init_input(struct input *i) {
    const int fd = setup_signalfd(make_signal_mask(SIGWINCH, 0));
    if(fd == -1)
        return false;
    i->sig_fd = fd;
    return true;
}

struct input_event process_input(const struct input *i) {
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
        case 'q':
        case ESC:
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
