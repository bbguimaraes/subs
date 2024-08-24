#ifndef SUBS_CURSES_WINDOW_LIST_SEARCH_H
#define SUBS_CURSES_WINDOW_LIST_SEARCH_H

#include <stdbool.h>

#include "../curses.h"

struct list;
struct search;

bool list_search_next(const struct search *s, struct list *l, int count);
enum subs_curses_key list_search_input(
    struct search *s, struct list *l, int c, int count);

#endif
