#ifndef SUBS_CURSES_SEARCH_H
#define SUBS_CURSES_SEARCH_H

#include "../buffer.h"

enum search_flags {
    SEARCH_INPUT  = 1u << 0,
    SEARCH_ACTIVE = 1u << 1,
};

struct search {
    struct buffer b;
    u8 flags;
};

void search_reset(struct search *s);
static bool search_is_active(const struct search *s);
static bool search_is_input_active(const struct search *s);
static bool search_is_empty(const struct search *s);
static void search_set_inactive(struct search *s);
static void search_end(struct search *s);
void search_add_char(struct search *s, char c);
void search_erase_char(struct search *s);

static inline bool search_is_active(const struct search *s) {
    return s->flags & SEARCH_ACTIVE;
}

static inline bool search_is_input_active(const struct search *s) {
    return s->flags & SEARCH_INPUT;
}

static inline bool search_is_empty(const struct search *s) {
    return !s->b.n;
}

static inline void search_set_inactive(struct search *s) {
    s->flags = (u8)((unsigned)s->flags & ~(unsigned)SEARCH_ACTIVE);
}

static inline void search_end(struct search *s) {
    s->flags = (u8)((unsigned)s->flags & ~(unsigned)SEARCH_INPUT);
}

#endif
