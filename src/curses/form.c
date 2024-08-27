#include "form.h"

#include <form.h>

#include "../util.h"

#include "window/window.h"

#define P(f) ((struct private*)f->f)

static FIELDTYPE *const TYPE_CHECKBOX = (FIELDTYPE*)&(char){0};

struct private {
    FORM *f;
    struct window *w, *sub;
};

bool subs_form_init(
    struct form *f, struct window *w,
    const char *title, size_t n_fields, const struct form_field *fields)
{
    struct private *const p = checked_malloc(sizeof(*p));
    if(!p)
        return false;
    FIELD **const v = checked_calloc(n_fields + 1, sizeof(*v));
    if(!v)
        goto e0;
    for(size_t i = 0; i != n_fields; ++i) {
        struct form_field f = fields[i];
        FIELD *const cf = new_field(f.height, f.width, f.y, f.x, 0, 0);
        set_field_buffer(cf, 0, f.text);
        if(f.type != FIELD_TYPE_LABEL && f.type != FIELD_TYPE_CHECKBOX)
            set_field_back(cf, A_UNDERLINE);
        set_field_type(cf, ((FIELDTYPE*[]) {
            [FIELD_TYPE_ALNUM]    = TYPE_ALNUM,
            [FIELD_TYPE_ALPHA]    = TYPE_ALPHA,
            [FIELD_TYPE_ENUM]     = TYPE_ENUM,
            [FIELD_TYPE_INTEGER]  = TYPE_INTEGER,
            [FIELD_TYPE_NUMERIC]  = TYPE_NUMERIC,
            [FIELD_TYPE_REGEXP]   = TYPE_REGEXP,
            [FIELD_TYPE_IPV4]     = TYPE_IPV4,
            [FIELD_TYPE_LABEL]    = NULL,
            [FIELD_TYPE_CHECKBOX] = TYPE_CHECKBOX,
        })[f.type]);
        switch(f.type) {
        case FIELD_TYPE_LABEL:
            field_opts_off(cf, O_ACTIVE | O_EDIT);
            break;
        default:
            break;
        }
        v[i] = cf;
    }
    FORM *const cf = new_form(v);
    if(!cf) {
        LOG_ERRNO("new_form", 0);
        goto e1;
    }
    const int width = window_width(w);
    const int height = window_height(w);
    struct window *const fw =
        window_new(w, height, width, window_y(w), window_x(w));
    window_print(fw, 0, 0, "%s", title);
    struct window *const sub = window_derive(fw, height - 1, width, 1, 0);
    set_form_win(cf, window_handle(fw));
    set_form_sub(cf, window_handle(sub));
    p->f = cf;
    p->w = fw;
    p->sub = sub;
    f->f = p;
    return true;
e1:
    free(v);
e0:
    free(p);
    return false;
}

void form_destroy(struct form *f) {
    struct private *const p = P(f);
    FORM *const cf = p->f;
    FIELD **const fields = form_fields(cf);
    unpost_form(cf);
    free_form(cf);
    FOR_EACH(FIELD*, x, fields)
        free_field(*x);
    free(fields);
    window_destroy(p->w);
    window_destroy(p->sub);
    free(p);
    f->f = NULL;
}

void form_refresh(struct form *f) {
    wrefresh(form_win(P(f)->f));
}

void form_redraw(struct form *f) {
    post_form(P(f)->f);
}

bool form_input(struct form *f, int c) {
    FORM *const cf = P(f)->f;
    form_driver(cf, c);
    wrefresh(form_win(cf));
    return true;
}
