#ifndef SUBS_CURSES_MESSAGE_H
#define SUBS_CURSES_MESSAGE_H

#include <stdbool.h>

struct window;

struct message {
    struct window *w, *sub;
    char *msg;
    char **q;
    int width, height, x, y;
};

void message_destroy(struct message *m);
void message_resize(struct message *m);
bool message_process(struct message *m);
char **message_push(struct message *m, char *s);
void message_hide(struct message *m);

#endif
