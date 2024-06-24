#include "common.h"

#include "curses/window/list.h"
#include "curses/window/window.h"

const char *PROG_NAME = NULL;
const char *CMD_NAME = NULL;

struct test_window {
    struct window base;
    struct window *parent;
    int h, w, y, x;
};

struct window *test_window_new(int y, int x, int h, int w);

struct window *test_window_derive(
    struct window *pw, int y, int x, int h, int w)
{
    struct window *const ret = test_window_new(y, x, h, w);
    ((struct test_window*)ret)->parent = pw;
    return ret;
}

int test_window_height(const struct window *w) {
    return ((const struct test_window*)w)->h;
}

void test_window_box(struct window *w, unsigned v_ch, unsigned h_ch) {
    (void)w, (void)v_ch, (void)h_ch;
}

void test_window_destroy(struct window *w) {
    free(w);
}

struct window *test_window_new(int h, int w, int y, int x) {
    struct test_window *const ret = checked_malloc(sizeof(*ret));
    if(!ret)
        return NULL;
    *ret = (struct test_window){
        .base = {
            .derive = test_window_derive,
            .destroy = test_window_destroy,
            .height = test_window_height,
            .box = test_window_box,
        },
        .h = h,
        .w = w,
        .y = y,
        .x = x,
    };
    return &ret->base;
}

bool list_window_new(void) {
    struct list l = {0};
    list_init(&l, test_window_new, 0, NULL, NULL, 1, 2, 3, 4);
    const struct test_window *const w = (const struct test_window*)l.w;
    bool ret = true;
    ret = ret
        && ASSERT_EQ(w->x, 1)
        && ASSERT_EQ(w->y, 2)
        && ASSERT_EQ(w->w, 3)
        && ASSERT_EQ(w->h, 4);
    if(!ret)
        goto end;
end:
    list_destroy(&l);
    return ret;
}

int main(void) {
    log_set(stderr);
    bool ret = true;
    ret = RUN(list_window_new) && ret;
    return !ret;
}
