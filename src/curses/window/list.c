#include "list.h"

#include <curses.h>

#include "../../log.h"
#include "../../util.h"

#include "window.h"

enum {
    INNER_SPACE = 1,
    BORDER_SIZE = LIST_BORDER_SIZE,
    SELECTED_ATTR = A_REVERSE,
    NOT_SELECTED_ATTR = A_UNDERLINE,
};

static int max_offset(const struct list *l) {
    const int n = l->n;
    const int h = window_height(l->sub);
    return n <= h ? 0 : n - h;
}

static int max_idx_for_offset(const struct list *l) {
    return l->offset + window_height(l->sub) - 1;
}

static int offset_for_bottom_idx(const struct list *l, int i) {
    return i + 1 - window_height(l->sub);
}

static int limit_idx(const struct list *l, int i) {
    return CLAMP(i, 0, l->n - 1);
}

static void resize(
    struct list *l, struct window *(*window_new)(int, int, int, int),
    int x, int y, int w, int h)
{
    bool create = true;
    if(l->w) {
        create = l->x != x || l->y != y || l->width != w || l->height != h;
        if(create)
            window_destroy(l->w);
        else
            window_clear(l->w);
    }
    if(create) {
        const int sh = h - 2 * BORDER_SIZE;
        const int sw = w - 2 * (BORDER_SIZE + INNER_SPACE);
        if(!window_new)
            window_new = window_new_curses;
        l->w = window_new(h, w, y, x);
        l->sub = window_derive(
            l->w, sh, sw, BORDER_SIZE, BORDER_SIZE + INNER_SPACE);
        l->x = x;
        l->y = y;
        l->width = w;
        l->height = h;
    }
    const int i = l->n ? limit_idx(l, l->i) : 0;
    const int o = MIN(l->offset, max_offset(l));
    l->offset = (i < max_idx_for_offset(l)) ? o : offset_for_bottom_idx(l, i);
    l->i = i;
}

static void set_line_attr(struct window *w, int y, attr_t a) {
    window_move(w, y, 0);
    window_change_attr(w, (window_character(w) & A_ATTRIBUTES) | a);
}

static void clear_line_attr(struct window *w, int y, attr_t a) {
    window_move(w, y, 0);
    window_change_attr(w, (window_character(w) & A_ATTRIBUTES) & ~a);
}

static void redraw(const struct list *l) {
    struct window *const w = l->sub;
    const char *const *lines = (const char**)l->lines;
    const int offset = l->offset;
    const int src_height = l->n;
    const int dst_height = window_height(w);
    const int height = MIN(src_height, dst_height);
    for(int i = 0; i != height; ++i) {
        window_print(w, i, 0, "%s", lines[offset + i]);
        window_clear_line(w);
    }
    set_line_attr(w, l->i - offset, l->selected_attr);
    window_refresh(w);
}

static void move_idx(struct list *l, int i) {
    struct window *const w = l->sub;
    clear_line_attr(w, l->i - l->offset, l->selected_attr);
    set_line_attr(w, i - l->offset, l->selected_attr);
    window_refresh(w);
    l->i = i;
}

static void offset(struct list *l, int d) {
    const int n = l->n;
    if(!n)
        return;
    const int max = max_offset(l);
    const int src = l->offset;
    int dst = src + d;
    dst = MAX(dst, 0);
    dst = MIN(dst, max);
    if(src == dst)
        move_idx(l, limit_idx(l, l->i + d));
    else {
        l->offset = dst;
        l->i += d;
        redraw(l);
    }
}

bool list_init(
    struct list *l, struct window *(*window_new)(int, int, int, int),
    int n, i64 *ids, char **lines, int x, int y, int width, int height)
{
    if(height < 2)
        return LOG_ERR("insufficient height (%d)\n", height), false;
    free(l->ids);
    free(l->lines);
    // TODO preserve current item as much as possible
    l->ids = ids;
    l->lines = lines;
    l->n = n;
    if(!l->selected_attr)
        l->selected_attr = NOT_SELECTED_ATTR;
    resize(l, window_new, x, y, width, height);
    if(n)
        redraw(l);
    window_box(l->w, 0, 0);
    return true;
}

void list_destroy(struct list *l) {
    free(l->ids);
    free(l->lines);
    l->ids = NULL;
    l->lines = NULL;
    if(l->w) {
        window_destroy(l->w), l->w = NULL;
        window_destroy(l->sub), l->sub = NULL;
    }
}

void list_refresh(struct list *l) {
    window_refresh(l->w);
}

void list_redraw(struct list *l) {
    window_redraw(l->w);
}

void list_box(struct list *l) {
    window_box(l->w, 0, 0);
}

void list_resize(
    struct list *l, struct window *(*window_new)(int, int, int, int),
    int x, int y, int width, int height)
{
    resize(l, window_new, x, y, width, height);
    if(l->n)
        redraw(l);
    window_box(l->w, 0, 0);
}

void list_move(struct list *l, int i) {
    if(!l->n || (i = limit_idx(l, i)) == l->i)
        return;
    const int h = window_height(l->sub);
    int o = l->offset;
    if(o <= i && i < o + h)
        move_idx(l, i);
    else {
        l->offset = (i < o) ? i : offset_for_bottom_idx(l, i);
        l->i = i;
        redraw(l);
    }
}

enum subs_curses_key list_input(struct list *l, int c) {
    enum { CTRL = 0x1f };
    const int h = window_height(l->sub);
    const int i = l->i;
    switch(c) {
    case CTRL & 'e': offset(l, 1); return true;
    case CTRL & 'y': offset(l, -1); return true;
    case 'G': case KEY_END: list_move(l, l->n - 1); return true;
    case 'H': list_move(l, l->offset); return true;
    case 'L': list_move(l, l->offset + l->height - 3); return true;
    case 'M': list_move(l, l->offset + (l->height - 2) / 2 - 1); return true;
    case 'b': case KEY_PPAGE: offset(l, -h); return true;
    case 'f': case KEY_NPAGE: offset(l, h); return true;
    case 'j': case KEY_DOWN: list_move(l, i + 1); return true;
    case 'k': case KEY_UP: list_move(l, i - 1); return true;
    case 'g': case KEY_HOME: list_move(l, 0); return true;
    default: return KEY_IGNORED;
    }
    return false;
}

void list_set_active(struct list *l, bool a) {
    struct window *const w = l->sub;
    const int i = l->i;
    clear_line_attr(w, i, l->selected_attr);
    l->selected_attr = a ? SELECTED_ATTR : NOT_SELECTED_ATTR;
    set_line_attr(w, i, l->selected_attr);
    window_refresh(w);
}

void list_set_name(struct list *l, const char *restrict fmt, ...) {
    char **const lines = l->lines;
    const int i = l->i;
    free(lines[i]);
    va_list args;
    va_start(args, fmt);
    lines[i] = vsprintf_alloc(fmt, args);
    va_end(args);
    redraw(l);
}

void list_write_title(struct list *l, int x, const char *restrict fmt, ...) {
    struct window *const w = l->w;
    if(x < 0)
        x += window_width(w);
    if(x < 0)
        return;
    va_list args;
    va_start(args, fmt);
    window_vprint(w, 0, x, fmt, args);
    va_end(args);
}
