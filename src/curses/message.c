#include "message.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include "../util.h"

#include "window/window.h"

static size_t n_messages(struct message *m) {
    size_t ret = 0;
    if(m->q)
        for(char *const *v = m->q; *v; ++v)
            ++ret;
    return ret;
}

static char *pop_message(struct message *m) {
    const size_t n = n_messages(m);
    char *const msg = *m->q;
    if(!checked_realloc(n * sizeof(*m->q), (void**)&m->q))
        return false;
    char **q = m->q;
    if(n) {
        memmove(q, q + 1, (n - 1) * sizeof(*q));
        q[n - 1] = NULL;
    }
    return msg;
}

static bool show(struct message *m, const char *msg) {
    const int width = m->width, height = m->height, x = m->x, y = m->y;
    struct window *const w = window_new_curses(height, width, y, x);
    if(!w)
        return false;
    enum { INNER_SPACE = 1, BORDER_SIZE = 1 };
    const int sh = height - 2 * BORDER_SIZE;
    const int sw = width - 2 * (BORDER_SIZE + INNER_SPACE);
    struct window *const sub = window_derive(
        w, sh, sw, BORDER_SIZE, BORDER_SIZE + INNER_SPACE);
    if(!sub)
        return window_destroy(w), false;
    window_box(w, 0, 0);
    char title[] = " message ";
    window_print(w, 0, width - (int)sizeof(title), title);
    window_print(sub, 0, 0, msg);
    window_refresh(w);
    m->w = w;
    m->sub = sub;
    return true;
}

void message_destroy(struct message *m) {
    if(m->sub)
        window_destroy(m->sub);
    if(m->w)
        window_destroy(m->w);
    free(m->msg);
    if(m->q)
        for(char **v = m->q; *v; ++v)
            free(*v);
    free(m->q);
}

void message_resize(struct message *m) {
    struct window *const w = m->w;
    if(!w)
        return;
    window_destroy(w);
    show(m, m->msg);
}

bool message_process(struct message *m) {
    if(m->msg || !m->q || !*m->q)
        return true;
    char *const msg = pop_message(m);
    if(!msg)
        return false;
    m->msg = msg;
    return show(m, msg);
}

char **message_push(struct message *m, char *msg) {
    const size_t n = n_messages(m);
    if(!checked_calloc_p(n + 1, sizeof(*m->q), (void**)&m->q))
        return NULL;
    m->q[n] = msg;
    return m->q + n;
}

void message_hide(struct message *m) {
    free(m->msg);
    m->msg = NULL;
    window_destroy(m->sub);
    window_destroy(m->w);
    m->sub = NULL;
    m->w = NULL;
}
