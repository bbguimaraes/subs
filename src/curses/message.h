#ifndef SUBS_CURSES_MESSAGE_H
#define SUBS_CURSES_MESSAGE_H

#include <stdbool.h>

struct window;

struct message {
    int width, height, x, y;
};

#endif
