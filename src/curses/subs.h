#ifndef SUBS_CURSES_SUBS_H
#define SUBS_CURSES_SUBS_H

#include <ncurses.h>

#include "../def.h"

#include "const.h"
#include "menu.h"
#include "search.h"

#include "window/list.h"

struct subs_curses;

enum subs_bar_order { SUBS_NAME, SUBS_ID, SUBS_WATCHED, SUBS_UNWATCHED };

struct subs_bar {
    struct subs_curses *s;
    struct videos *videos;
    struct list list;
    struct search search;
    struct menu menu;
    int x, y, width, height, tag, type;
    u8 flags, order;
};

void subs_bar_toggle_watched(struct subs_bar *b);
void subs_bar_toggle_not_watched(struct subs_bar *b);
void subs_bar_set_untagged(struct subs_bar *b);
void subs_bar_set_tag(struct subs_bar *b, int t);
void subs_bar_set_type(struct subs_bar *b, int t);
bool subs_bar_set_order(struct subs_bar *b, u8 o);
bool subs_bar_next(struct subs_bar *b);
bool subs_bar_prev(struct subs_bar *b);
bool subs_bar_reload(struct subs_bar *b);
void subs_bar_destroy(struct subs_bar *b);
bool subs_bar_leave(void *data);
bool subs_bar_enter(void *data);
void subs_bar_redraw(void *data);
enum subs_curses_key subs_bar_input(void *data, int c, int count);

#endif
