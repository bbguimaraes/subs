#ifndef SUBS_SOURCE_VIDEOS_H
#define SUBS_SOURCE_VIDEOS_H

#include <stdbool.h>

#include <ncurses.h>
#include <sqlite3.h>

#include "../def.h"
#include "../buffer.h"

#include "list.h"

struct window;

enum videos_flags {
    VIDEOS_ACTIVE       = 1u << 0,
    VIDEOS_UNTAGGED     = 1u << 1,
    VIDEOS_WATCHED      = 1u << 2,
    VIDEOS_NOT_WATCHED  = 1u << 3,
    VIDEOS_SEARCH_INPUT = 1u << 4,
};

struct videos {
    struct subs_curses *s;
    int *items;
    struct buffer search;
    struct list list;
    int n, x, y, width, /*XXX*/height, tag, type, sub;
    u8 flags;
};

void videos_destroy(struct videos *v);
void videos_toggle_watched(struct videos *v);
void videos_toggle_not_watched(struct videos *v);
void videos_set_untagged(struct videos *v);
void videos_set_tag(struct videos *v, int t);
void videos_set_type(struct videos *v, int t);
void videos_set_sub(struct videos *v, int s);
bool videos_leave(struct window *w);
bool videos_enter(struct window *w);
bool videos_input(struct window *w, int c);
bool videos_resize(struct videos *v);
bool videos_reload(struct videos *v);

#endif
