#include "input.h"

#include <assert.h>

#include <curses.h>

#include "../unix.h"
#include "../util.h"

#define EVENT(...) (struct input_event){ .type = INPUT_TYPE_ ## __VA_ARGS__ }

struct input_event input_process(void) {
    const int fds[] = {STDIN_FILENO};
    int fd = -1;
    switch(poll_input(ARRAY_SIZE(fds), fds, &fd)) {
    case INPUT_FD:
        break;
    default:
        return EVENT(ERR);
    }
    if(fd == STDIN_FILENO) {
        const int key = getch();
        switch(key) {
        case ERR:
            return EVENT(ERR);
        default:
            return EVENT(KEY, .key = key);
        }
    }
    assert(!"invalid file descriptor");
}
