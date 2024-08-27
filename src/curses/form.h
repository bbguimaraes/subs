#ifndef SUBS_CURSES_FORM_H
#define SUBS_CURSES_FORM_H

#include <stdbool.h>
#include <stddef.h>

struct window;

enum form_field_type {
    FIELD_TYPE_ALNUM,
    FIELD_TYPE_ALPHA,
    FIELD_TYPE_ENUM,
    FIELD_TYPE_INTEGER,
    FIELD_TYPE_NUMERIC,
    FIELD_TYPE_REGEXP,
    FIELD_TYPE_IPV4,
    FIELD_TYPE_CHECKBOX,
    FIELD_TYPE_LABEL,
};

struct form {
    void *f;
};

struct form_field {
    enum form_field_type type;
    const char *text;
    int x, y, width, height;
};

bool subs_form_init(
    struct form *f, struct window *w,
    const char *title, size_t n_fields, const struct form_field *fields);
void form_destroy(struct form *f);
void form_refresh(struct form *f);
void form_redraw(struct form *f);
bool form_input(struct form *f, int c);

#endif
