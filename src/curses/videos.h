#ifndef SUBS_CURSES_VIDEOS_H
#define SUBS_CURSES_VIDEOS_H

#include <stdbool.h>

#include <ncurses.h>
#include <sqlite3.h>

#include "../def.h"
#include "../buffer.h"

#include "const.h"
#include "list.h"
#include "search.h"

struct window;

enum videos_flags {
    VIDEOS_ACTIVE        = 1u << 0,
    VIDEOS_UNTAGGED      = 1u << 1,
};

struct videos {
    struct subs_curses *s;
    int *items;
    struct list list;
    struct search search;
    int n, x, y, width, height, tag, type, sub;
    u8 flags;
};

void videos_destroy(struct videos *v);
void videos_set_untagged(struct videos *v);
void videos_set_tag(struct videos *v, int t);
void videos_set_type(struct videos *v, int t);
void videos_set_sub(struct videos *v, int s);
bool videos_leave(struct window *w);
bool videos_enter(struct window *w);
void videos_bar_redraw(struct window *w);
enum subs_curses_key videos_input(struct window *w, int c);
bool videos_resize(struct videos *v);
bool videos_reload(struct videos *v);

#endif
