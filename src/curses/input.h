#ifndef SUBS_CURSES_INPUT_H
#define SUBS_CURSES_INPUT_H

#include <stdbool.h>

enum input_type {
    INPUT_TYPE_ERR,
    INPUT_TYPE_KEY,
    INPUT_TYPE_RESIZE,
    INPUT_TYPE_QUIT,
};

struct input_event {
    enum input_type type;
    union {
        int key;
    };
};

struct input {
    int sig_fd, event_r, event_w;
};

bool input_init(struct input *i);
bool input_destroy(struct input *i);
struct input_event input_process(const struct input *i);
bool input_send_event(struct input *i, struct input_event e);

#endif
