#include "window.h"

#include <assert.h>
#include <stdlib.h>

#include <curses.h>

#include "../../util.h"

struct window_curses {
    struct window w;
    WINDOW *cw;
};

static struct window_curses window_curses_new(WINDOW *w);

static void *window_curses_handle(struct window *w) {
    return ((struct window_curses*)w)->cw;
}

static struct window *window_curses_derive(
    struct window *pw, int h, int w, int y, int x)
{
    struct window_curses *const ret = checked_malloc(sizeof(*ret));
    if(!ret)
        return NULL;
    WINDOW *const nw = derwin(window_curses_handle(pw), h, w, y, x);
    if(!nw)
        return log_err("derwin"), free(ret), NULL;
    *ret = window_curses_new(nw);
    return &ret->w;
}

static int window_curses_x(const struct window *w) {
    return getbegx((WINDOW*)window_curses_handle((struct window*)w));
}

static int window_curses_y(const struct window *w) {
    return getbegy((WINDOW*)window_curses_handle((struct window*)w));
}

static int window_curses_height(const struct window *w) {
    return getmaxy((WINDOW*)window_curses_handle((struct window*)w));
}

static int window_curses_width(const struct window *w) {
    return getmaxx((WINDOW*)window_curses_handle((struct window*)w));
}

static unsigned window_curses_character(const struct window *w) {
    return winch(window_curses_handle((struct window*)w));
}

static void window_curses_move(struct window *w, int y, int x) {
    wmove(window_curses_handle(w), y, x);
}

static void window_curses_change_attr(struct window *w, unsigned c) {
    wchgat(window_curses_handle(w), -1, c, 0, NULL);
}

static void window_curses_refresh(struct window *w) {
    wrefresh(window_curses_handle(w));
}

static void window_curses_redraw(struct window *w) {
    redrawwin((WINDOW*)window_curses_handle(w));
}

static void window_curses_clear(struct window *w) {
    wclear(window_curses_handle(w));
}

static void window_curses_clear_line(struct window *w) {
    wclrtoeol(window_curses_handle(w));
}

static void window_curses_box(struct window *w, unsigned v_ch, unsigned h_ch) {
    box(window_curses_handle(w), v_ch, h_ch);
}

static void window_curses_vprint(
    struct window *w, int y, int x, const char *fmt, va_list args)
{
    WINDOW *const cw = window_curses_handle(w);
    wmove(cw, y, x);
    vw_printw(cw, fmt, args);
}

static void window_curses_destroy(struct window *w) {
    delwin(window_curses_handle(w));
    free(w);
}

static struct window_curses window_curses_new(WINDOW *w) {
    assert(w);
    return (struct window_curses){
        .w = {
            .new = window_new_curses,
            .derive = window_curses_derive,
            .handle = window_curses_handle,
            .x = window_curses_x,
            .y = window_curses_y,
            .height = window_curses_height,
            .width = window_curses_width,
            .character = window_curses_character,
            .move = window_curses_move,
            .change_attr = window_curses_change_attr,
            .refresh = window_curses_refresh,
            .redraw = window_curses_redraw,
            .clear = window_curses_clear,
            .clear_line = window_curses_clear_line,
            .box = window_curses_box,
            .vprint = window_curses_vprint,
            .destroy = window_curses_destroy,
        },
        .cw = w,
    };
}

struct window *window_new_curses(int h, int w, int y, int x) {
    struct window_curses *const ret = checked_malloc(sizeof(*ret));
    if(!ret)
        return NULL;
    WINDOW *const nw = newwin(h, w, y, x);
    if(!nw)
        return log_err("newwin"), free(ret), NULL;
    *ret = window_curses_new(nw);
    return &ret->w;
}

void window_print(
    struct window *w, int y, int x, const char *restrict fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    window_vprint(w, y, x, fmt, args);
    va_end(args);
}
