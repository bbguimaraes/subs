#ifndef SUBS_CURSES_SOURCE_H
#define SUBS_CURSES_SOURCE_H

#include <stdbool.h>
#include <stddef.h>

#include <sqlite3.h>

#include "../def.h"

#include "const.h"
#include "window/list.h"
#include "search.h"

struct subs;
struct subs_curses;

struct source_bar {
    struct subs_curses *s;
    struct subs_bar *subs_bar;
    struct videos *videos;
    struct list list;
    struct search search;
    int *items;
    int x, y, width, height, n_tags;
};

void source_bar_update_title(struct source_bar *b);
bool source_bar_update_count(struct source_bar *b);
bool source_bar_reload(struct source_bar *b);
void source_bar_destroy(struct source_bar *b);
bool source_bar_leave(void *data);
bool source_bar_enter(void *data);
void source_bar_redraw(void *data);
enum subs_curses_key source_bar_input(void *data, int c);

#endif
