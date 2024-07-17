#include "common.h"

#include "buffer.h"

const char *PROG_NAME = NULL;
const char *CMD_NAME = NULL;

bool buffer_reserve_empty(void) {
    struct buffer b = {0};
    buffer_reserve(&b, 0);
    const bool ret =
        ASSERT_NE(b.p, NULL)
        && ASSERT_EQ(b.n, 0)
        && ASSERT_EQ(b.cap, 1);
    free(b.p);
    return ret;
}

bool buffer_append_empty(void) {
    const char src[] = "01234567";
    const size_t n = sizeof(src) - 1;
    struct buffer b = {0};
    buffer_append(&b, src, n);
    const bool ret =
        ASSERT_STR_EQ_N(b.p, src, n)
        && ASSERT_EQ(b.n, n)
        && ASSERT_EQ(b.cap, 8);
    free(b.p);
    return ret;
}

bool buffer_append_half(void) {
    const char src[] = "01234567";
    const size_t n = sizeof(src) - 1;
    struct buffer b = {0};
    buffer_reserve(&b, 4);
    buffer_append(&b, src, n);
    const bool ret =
        ASSERT_STR_EQ_N(b.p, src, n)
        && ASSERT_EQ(b.n, n)
        && ASSERT_EQ(b.cap, 8);
    free(b.p);
    return ret;
}

bool buffer_append_full(void) {
    const char src0[] = "01234567";
    const char src1[] = "89abcdef";
    const size_t n0 = sizeof(src0) - 1;
    const size_t n1 = sizeof(src0) - 1;
    const char expected[] = "0123456789abcdef";
    const size_t n = sizeof(expected) - 1;
    struct buffer b = {0};
    buffer_append(&b, src0, n0);
    buffer_append(&b, src1, n1);
    const bool ret =
        ASSERT_STR_EQ_N(b.p, expected, n)
        && ASSERT_EQ(b.n, n)
        && ASSERT_EQ(b.cap, n);
    free(b.p);
    return ret;
}

bool buffer_append_str_test(void) {
    const char src0[] = "0123456";
    const char src1[] = "789abcd";
    const char expected[] = "0123456789abcd";
    const size_t n0 = sizeof(src0);
    const size_t n = sizeof(expected);
    struct buffer b = {0};
    buffer_append_str(&b, src0);
    bool ret = true;
    ret =
        ASSERT_STR_EQ_N(b.p, src0, n0)
        && ASSERT_EQ(b.n, n0)
        && ASSERT_EQ(b.cap, n0);
    if(!ret)
        goto end;
    --b.n;
    buffer_append_str(&b, src1);
    ret =
        ASSERT_STR_EQ_N(b.p, expected, n)
        && ASSERT_EQ(b.n, n)
        && ASSERT_EQ(b.cap, 16);
    if(!ret)
        goto end;
    ret = true;
end:
    free(b.p);
    return ret;
}

bool buffer_str_append_str_test(void) {
    const char src0[] = "0123456";
    const char src1[] = "789abcd";
    const char expected[] = "0123456789abcd";
    const size_t n0 = sizeof(src0);
    const size_t n = sizeof(expected);
    struct buffer b = {0};
    buffer_append_str(&b, src0);
    bool ret = true;
    ret =
        ASSERT_STR_EQ_N(b.p, src0, n0)
        && ASSERT_EQ(b.n, n0)
        && ASSERT_EQ(b.cap, n0);
    if(!ret)
        goto end;
    buffer_str_append_str(&b, src1);
    ret =
        ASSERT_STR_EQ_N(b.p, expected, n)
        && ASSERT_EQ(b.n, n)
        && ASSERT_EQ(b.cap, 16);
    if(!ret)
        goto end;
    ret = true;
end:
    free(b.p);
    return ret;
}

int main(void) {
    log_set(stderr);
    bool ret = true;
    ret = RUN(buffer_reserve_empty) && ret;
    ret = RUN(buffer_append_empty) && ret;
    ret = RUN(buffer_append_half) && ret;
    ret = RUN(buffer_append_full) && ret;
    ret = RUN(buffer_append_str_test) && ret;
    ret = RUN(buffer_str_append_str_test) && ret;
    return !ret;
}
