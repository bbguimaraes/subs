#ifndef SUBS_CURSES_SUBS_H
#define SUBS_CURSES_SUBS_H

#include <ncurses.h>

#include "../def.h"

#include "list.h"

struct subs_curses;
struct window;

enum subs_bar_flags {
    SUBS_WATCHED =     1u << 0,
    SUBS_NOT_WATCHED = 1u << 1,
    SUBS_UNTAGGED =    1u << 2,
};

struct subs_bar {
    struct subs_curses *s;
    struct videos *videos;
    struct list list;
    int *items;
    int x, y, width, /*XXX*/height, tag, type;
    u8 flags;
};

void subs_bar_toggle_watched(struct subs_bar *b);
void subs_bar_toggle_not_watched(struct subs_bar *b);
void subs_bar_set_untagged(struct subs_bar *b);
void subs_bar_set_tag(struct subs_bar *b, int t);
void subs_bar_set_type(struct subs_bar *b, int t);
bool subs_bar_reload(struct subs_bar *b);
void subs_bar_destroy(struct subs_bar *b);
bool subs_bar_leave(struct window *w);
bool subs_bar_enter(struct window *w);
bool subs_bar_input(struct window *w, int c);

#endif
