#ifndef SUBS_CURSES_WINDOW_WINDOW_H
#define SUBS_CURSES_WINDOW_WINDOW_H

#include <stdarg.h>

struct window {
    struct window *(*new)(int, int, int, int);
    struct window *(*derive)(struct window*, int, int, int, int);
    void *(*handle)(struct window*);
    int (*x)(const struct window*);
    int (*y)(const struct window*);
    int (*height)(const struct window*);
    int (*width)(const struct window*);
    unsigned (*character)(const struct window*);
    void (*move)(struct window*, int y, int x);
    void (*change_attr)(struct window*, unsigned);
    void (*refresh)(struct window*);
    void (*redraw)(struct window*);
    void (*clear)(struct window*);
    void (*clear_line)(struct window*);
    void (*box)(struct window*, unsigned, unsigned);
    void (*vprint)(
        struct window*, int, int, const char *restrict, va_list args);
    void (*destroy)(struct window*);
};

struct window *window_new_curses(int h, int w, int y, int x);
struct window *window_derive_curses(int h, int w, int y, int x);
void window_print(
    struct window *w, int y, int x, const char *restrict fmt, ...);

static inline struct window *window_new(
    const struct window *pw, int h, int w, int y, int x)
{
    return pw->new(h, w, y, x);
}

static inline void *window_handle(struct window *w) { return w->handle(w); }
static inline int window_x(const struct window *w) { return w->x(w); }
static inline int window_y(const struct window *w) { return w->y(w); }
static inline int window_height(const struct window *w) { return w->height(w); }
static inline int window_width(const struct window *w) { return w->width(w); }
static inline void window_refresh(struct window *w) { (w->refresh)(w); }
static inline void window_redraw(struct window *w) { (w->redraw)(w); }
static inline void window_clear(struct window *w) { (w->clear)(w); }
static inline void window_clear_line(struct window *w) { w->clear_line(w); }
static inline void window_destroy(struct window *w) { w->destroy(w); }

static inline struct window *window_derive(
    struct window *pw, int h, int w, int y, int x)
{
    return (pw->derive)(pw, h, w, y, x);
}

static inline unsigned window_character(const struct window *w) {
    return w->character(w);
}

static inline void window_move(struct window *w, int y, int x) {
    (w->move)(w, y, x);
}

static inline void window_change_attr(struct window *w, unsigned c) {
    w->change_attr(w, c);
}

static inline void window_box(struct window *w, unsigned v_ch, unsigned h_ch) {
    (w->box)(w, v_ch, h_ch);
}

static inline void window_vprint(
    struct window *w, int y, int x, const char *restrict fmt, va_list args)
{
    w->vprint(w, y, x, fmt, args);
}

#endif
