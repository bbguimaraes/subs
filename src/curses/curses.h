#ifndef SUBS_CURSES_H
#define SUBS_CURSES_H

#include <stdbool.h>

#include <curses.h>
#include <sqlite3.h>

#include "../def.h"

#include "const.h"

typedef struct lua_State lua_State;

struct task_thread;
struct videos;

struct subs_curses {
    sqlite3 *db;
    lua_State *L;
    struct task_thread *task_thread;
    struct window *windows;
    size_t n_windows, cur_window;
    u8 flags;
    void *priv;
};

struct window {
    void *data;
    bool (*leave)(void*);
    bool (*enter)(void*);
    void (*redraw)(void*);
    enum subs_curses_key (*input)(void*, int);
};

static unsigned int_digits(int i);

bool query_to_int(
    sqlite3 *db, const char *sql, size_t len, const int *param, int *p);
char *name_with_counts(
    int width, const char *prefix, const char *title, int n0, int n1);
bool change_window(struct subs_curses *s, size_t i);
bool add_message(struct subs_curses *s, const char *msg);
bool toggle_watched(struct subs_curses *s);
bool toggle_not_watched(struct subs_curses *s);
void suspend_tui(void);
void resume_tui(void);

static inline unsigned int_digits(int i) {
    unsigned ret = 1;
    while(i /= 10)
        ++ret;
    return ret;
}

#endif
