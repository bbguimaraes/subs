#include "menu.h"

#include <stdlib.h>

#include <menu.h>

#include "../util.h"

#include "window.h"

#define M(m) ((MENU*)((m)->m))

void subs_menu_init(
    struct menu *m, struct window *w,
    int n, const char *title, const char **items, const char **desc)
{
    ITEM **const c_items = checked_calloc((size_t)n + 1, sizeof(*c_items));
    if(desc)
        for(int i = 0; i != n; ++i)
            c_items[i] = new_item(items[i], desc[i]);
    else
        for(int i = 0; i != n; ++i)
            c_items[i] = new_item(items[i], NULL);
    MENU *const cm = new_menu(c_items);
    const int height = window_height(w), width = window_width(w);
    struct window *const mw =
        window_new(w, height, width, window_y(w), window_x(w));
    window_print(mw, 0, 0, "%s", title);
    struct window *const sub = window_derive(mw, height - 1, width, 1, 0);
    set_menu_win(cm, window_handle(mw));
    set_menu_sub(cm, window_handle(sub));
    set_menu_format(cm, n, 0);
    window_refresh(mw);
    m->m = cm;
    m->w = mw;
    m->sub = sub;
}

void menu_destroy(struct menu *m) {
    MENU *const cm = M(m);
    unpost_menu(cm);
    ITEM **const items = menu_items(cm);
    const int n = item_count(cm);
    free_menu(cm);
    for(int i = 0; i != n; ++i)
        free_item(items[i]);
    window_destroy(m->sub);
    window_destroy(m->w);
}

int menu_current(struct menu *m) {
    return item_index(current_item(M(m)));
}

void menu_set_current(struct menu *m, int i) {
    MENU *const cm = M(m);
    set_current_item(cm, menu_items(cm)[i]);
}

void menu_display(struct menu *m) {
    post_menu(M(m));
}

void menu_refresh(struct menu *m) {
    wrefresh(menu_sub(M(m)));
}

bool menu_input(struct menu *m, int c) {
    MENU *const cm = M(m);
    switch(c) {
    case 'h': case KEY_LEFT:  menu_driver(cm, REQ_LEFT_ITEM);  break;
    case 'j': case KEY_DOWN:  menu_driver(cm, REQ_DOWN_ITEM);  break;
    case 'k': case KEY_UP:    menu_driver(cm, REQ_UP_ITEM);    break;
    case 'l': case KEY_RIGHT: menu_driver(cm, REQ_RIGHT_ITEM); break;
    case 'b': case KEY_PPAGE: menu_driver(cm, REQ_SCR_UPAGE);  break;
    case 'f': case KEY_NPAGE: menu_driver(cm, REQ_SCR_DPAGE);  break;
    case 'g': case KEY_HOME:  menu_driver(cm, REQ_FIRST_ITEM); break;
    case 'G': case KEY_END:   menu_driver(cm, REQ_LAST_ITEM);  break;
    default: return false;
    }
    wrefresh(menu_sub(cm));
    return true;
}
