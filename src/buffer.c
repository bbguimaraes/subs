#include "buffer.h"

void buffer_printf(struct buffer *b, const char *restrict fmt, ...) {
    va_list args0, args1;
    va_start(args0, fmt);
    va_copy(args1, args0);
    const int n = vsnprintf(NULL, 0, fmt, args0);
    va_end(args0);
    assert(0 <= n);
    buffer_reserve(b, b->n + (size_t)n + 1);
    const int nw = vsprintf(b->p, fmt, args1);
    assert(n == nw);
    va_end(args1);
    b->n += (size_t)n + 1;
}
