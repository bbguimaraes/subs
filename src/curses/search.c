#include "search.h"

void search_reset(struct search *s) {
    free(s->b.p);
    *s = (struct search){.flags = SEARCH_INPUT | SEARCH_ACTIVE};
}

void search_add_char(struct search *s, char c) {
    struct buffer *b = &s->b;
    if(b->n)
        --b->n;
    buffer_append_str(b, (char[]){(char)c, 0});
}

void search_erase_char(struct search *s) {
    struct buffer *b = &s->b;
    switch(b->n) {
    case 0:
        break;
    case 1:
        assert(false);
    case 2:
        buffer_destroy(b);
        break;
    default:
        ((char*)b->p)[--b->n - 1] = 0;
        break;
    }
}
