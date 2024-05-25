#ifndef SUBS_CURSES_CONST_H
#define SUBS_CURSES_CONST_H

#define SOURCE_BAR_IDX 0
#define SUBS_BAR_IDX 1
#define VIDEOS_IDX 2

enum {
    ESC  = 0x1b,
    CTRL = 0x1f,
    DEL  = 0x7f,
};

enum subs_curses_flags {
    RESIZED =     1u << 0,
    WATCHED =     1u << 1,
    NOT_WATCHED = 1u << 2,
};

enum subs_curses_key {
    KEY_ERROR   = 0,
    KEY_HANDLED = 1,
    KEY_IGNORED = 2,
};

#endif
