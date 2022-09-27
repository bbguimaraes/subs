#ifndef SUBS_CURSES_WINDOW_LIST_H
#define SUBS_CURSES_WINDOW_LIST_H

#include <stdbool.h>

#include "../const.h"

struct window;

enum {
    LIST_BORDER_SIZE = 1,
};

/**
 * A list containing one text item per line.
 *
 * \ref w is the root window, positioned at coordinates \ref x and \ref y on the
 * screen, with dimensions \ref width and \ref height, determined by the
 * parameters given to \ref list_init.
 *
 * \ref sub is a derived window from \ref w which excludes the border and inner
 * spacing, i.e. it contains only the text for the items in \ref lines.  \ref
 * offset is the first item displayed at the top of \ref sub, which always
 * displays items <tt>lines[offset:offset+height-2*b]</tt>, where `b` is the
 * size of the top/bottom border.
 */
struct list {
    /** Root window, including borders and spaces. */
    struct window *w;
    /** Sub-window of \ref w, excluding borders and spaces. */
    struct window *sub;
    /** Items displayed in the list. */
    char **lines;
    /** Length of \ref lines. */
    int n;
    /** Current selected item in \ref lines. */
    int i;
    /** Beginning of the region of \ref lines currently displayed. */
    int offset;
    /** X position of window \ref w on the screen. */
    int x;
    /** Y position of window \ref w on the screen. */
    int y;
    /** Width of window \ref w. */
    int width;
    /** Height of window \ref w. */
    int height;
    /** Attribute used to highlight the selected item. */
    unsigned selected_attr;
};

bool list_init(
    struct list *l, struct window *(*window_new)(int, int, int, int),
    int n, char **lines, int x, int y, int width, int height);
void list_destroy(struct list *l);
void list_refresh(struct list *l);
void list_redraw(struct list *l);
void list_box(struct list *l);
void list_resize(
    struct list *l, struct window *(*window_new)(int, int, int, int),
    int x, int y, int width, int height);
void list_move(struct list *l, int i);
enum subs_curses_key list_input(struct list *l, int c);
void list_set_active(struct list *l, bool a);
void list_set_name(struct list *l, const char *restrict fmt, ...);
void list_write_title(struct list *l, int x, const char *restrict fmt, ...);

#endif
