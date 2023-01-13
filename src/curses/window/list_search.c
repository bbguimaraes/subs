#include "list_search.h"

#include "../../buffer.h"

#include "../search.h"

#include "list.h"
#include "search.h"

bool list_search_next(const struct search *s, struct list *l) {
    const int n = l->n;
    if(!n)
        return false;
    const bool inv = *(const char*)s->b.p == '!';
    const char *const text = (const char*)s->b.p + inv;
    const char *const *const lines = (const char *const*)l->lines;
    for(int i = l->i + 1; i != n; ++i)
        if((bool)strstr(lines[i], text) != inv) {
            list_move(l, i);
            return true;
        }
    return false;
}

enum subs_curses_key list_search_input(
    struct search *s, struct list *l, int c)
{
    switch(c) {
    case ERR:
        return false;
    case '\n':
        if(!search_is_empty(s))
            list_search_next(s, l);
        else
            search_set_inactive(s);
        search_end(s);
        return KEY_HANDLED;
    case KEY_BACKSPACE:
        if(!search_is_empty(s))
            search_erase_char(s);
        return KEY_HANDLED;
    default:
        if(!(c & ~CTRL))
            return KEY_IGNORED;
        search_add_char(s, (char)c);
        return KEY_HANDLED;
    }
}
