#ifndef SUBS_CURSES_H
#define SUBS_CURSES_H

#include <stdbool.h>

#include <curses.h>
#include <sqlite3.h>

#include "../def.h"

#include "const.h"

typedef struct lua_State lua_State;

struct videos;

struct subs_curses {
    sqlite3 *db;
    lua_State *L;
    struct window *windows;
    size_t n_windows, cur_window;
    u8 flags;
};

struct window {
    int x, y;
    void *data;
    bool (*leave)(struct window*);
    bool (*enter)(struct window*);
    void (*redraw)(struct window*);
    enum subs_curses_key (*input)(struct window*, int);
};

static unsigned int_digits(int i);

bool query_to_int(
    sqlite3 *db, const char *sql, size_t len, const int *param, int *p);
char *name_with_counts(
    int width, const char *prefix, const char *title, int n0, int n1);
bool change_window(struct subs_curses *s, size_t i);
void suspend_tui(void);
void resume_tui(void);

static inline unsigned int_digits(int i) {
    unsigned ret = 1;
    while(i /= 10)
        ++ret;
    return ret;
}

#endif
