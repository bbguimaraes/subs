#ifndef SUBS_CURSES_INPUT_H
#define SUBS_CURSES_INPUT_H

enum input_type {
    INPUT_TYPE_ERR,
    INPUT_TYPE_KEY,
    INPUT_TYPE_QUIT,
};

struct input_event {
    enum input_type type;
    union {
        int key;
    };
};

struct input_event input_process(void);

#endif
