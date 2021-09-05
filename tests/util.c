#include "common.h"

#include "util.h"

const char *PROG_NAME = NULL;
const char *CMD_NAME = NULL;

bool strlcpy_empty(void) {
    char buffer[8] = {0};
    const char src[] = "";
    const size_t n = strlcpy(buffer, src, 0);
    return ASSERT_STR_EQ(buffer, src) && ASSERT_EQ(n, 0);
}

bool strlcpy_not_full(void) {
    char buffer[8] = {0};
    const char src[] = "012";
    const size_t n = strlcpy(buffer, src, sizeof(buffer));
    return ASSERT_STR_EQ(buffer, src) && ASSERT_EQ(n, sizeof(src));
}

bool strlcpy_full(void) {
    char buffer[8] = {0};
    const char src[] = "0123456";
    const size_t n = strlcpy(buffer, src, sizeof(buffer));
    return ASSERT_STR_EQ(buffer, src) && ASSERT_EQ(n, sizeof(buffer));
}

bool strlcpy_exceed(void) {
    char buffer[8] = {0};
    const char src[] = "01234567";
    const size_t n = strlcpy(buffer, src, sizeof(buffer));
    return ASSERT_STR_EQ_N(buffer, src, sizeof(buffer))
        && ASSERT_EQ(n, sizeof(buffer));
}

int main(void) {
    log_set(stderr);
    bool ret = true;
    ret = RUN(strlcpy_empty) && ret;
    ret = RUN(strlcpy_not_full) && ret;
    ret = RUN(strlcpy_full) && ret;
    ret = RUN(strlcpy_exceed) && ret;
    return !ret;
}
