#include "form.h"

#include <form.h>

#include "../util.h"

#include "const.h"

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
    FIELD *const field = current_field(cf);
    if(field_type(field) == TYPE_CHECKBOX)
        switch(c) {
        default: break;
        case ' ':
            set_field_buffer(field, 0, (const char[]){
                *field_buffer(field, 0) == ' ' ? 'x' : ' ', 0});
            goto end;
    }
    enum { CTRL = 0x1f };
    switch(c) {
    case CTRL & 'a': form_driver(cf, REQ_BEG_FIELD); break;
    case CTRL & 'e': form_driver(cf, REQ_END_FIELD); break;
    case CTRL & 'k': form_driver(cf, REQ_CLR_EOF); break;
    case KEY_BACKSPACE:
        form_driver(cf, REQ_PREV_CHAR);
        /* fallthrough */
    case KEY_DC:
        form_driver(cf, REQ_DEL_CHAR);
        form_driver(cf, REQ_VALIDATION);
        break;
    case KEY_LEFT: form_driver(cf, REQ_PREV_CHAR); break;
    case KEY_RIGHT: form_driver(cf, REQ_NEXT_CHAR); break;
    case '\t':
    case KEY_DOWN: form_driver(cf, REQ_NEXT_FIELD); break;
    case KEY_BTAB:
    case KEY_UP: form_driver(cf, REQ_PREV_FIELD); break;
    default: form_driver(cf, c); break;
    }
end:
    wrefresh(form_win(cf));
    return true;
}

size_t form_field_count(struct form *f) {
    return (size_t)field_count(P(f)->f);
}

const char *form_buffer(struct form *f, size_t i) {
    return field_buffer(form_fields(P(f)->f)[i], 0);
}
