#ifndef SUBS_CURSES_MENU_H
#define SUBS_CURSES_MENU_H

#include <stdbool.h>

struct window;

struct menu {
    void *m;
    struct window *w, *sub;
};

void subs_menu_init(
    struct menu *m, struct window *w,
    int n, const char *title, const char **items, const char **desc);
void menu_destroy(struct menu *m);
int menu_current(struct menu *m);
void menu_set_current(struct menu *m, int i);
void menu_display(struct menu *m);
void menu_refresh(struct menu *m);
bool menu_input(struct menu *m, int c);

#endif
