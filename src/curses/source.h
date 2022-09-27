#ifndef SUBS_CURSES_SOURCE_H
#define SUBS_CURSES_SOURCE_H

#include <stdbool.h>
#include <stddef.h>

#include <sqlite3.h>

#include "../def.h"

#include "list.h"

struct subs;
struct subs_curses;
struct window;

struct source_bar {
    struct subs_curses *s;
    struct subs_bar *subs_bar;
    struct videos *videos;
    struct list list;
    int *items;
    int x, y, width, /*XXX*/height, n_tags;
};

bool source_bar_update_count(struct source_bar *b);
bool source_bar_reload(struct source_bar *b);
void source_bar_destroy(struct source_bar *b);
bool source_bar_leave(struct window *w);
bool source_bar_enter(struct window *w);
bool source_bar_input(struct window *w, int c);

#endif
