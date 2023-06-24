#include "curses.h"

#include <assert.h>
#include <locale.h>
#include <signal.h>

#include <signal.h>

#include <form.h>
#include <menu.h>

#include "../log.h"
#include "../subs.h"
#include "../unix.h"
#include "../util.h"

#include "input.h"
#include "lua.h"
#include "source.h"
#include "subs.h"
#include "videos.h"

enum {
    ESC  = 0x1b,
    CTRL = 0x1f,
    DEL  = 0x7f,
};

static char log[4096];
static size_t log_pos;
static log_fn *log_prev;

static void curses_log_fn(const char *fmt, va_list args) {
    const int n = vsnprintf(log + log_pos, sizeof(log) - log_pos, fmt, args);
    if(n < 0) {
        fprintf(stderr, "%s: vsnprintf: %s\n", __func__, strerror(errno));
        return;
    }
    log_pos += (size_t)n;
    if(log_pos == sizeof(log) && log[log_pos - 1])
        fprintf(stderr, "%s: log buffer full\n", __func__);
}

static void process_log(void) {
    const size_t p = log_pos;
    if(!p)
        return;
    attron(COLOR_PAIR(1));
    mvprintw(0, 0, "%.*s", (int)p, log);
    attron(COLOR_PAIR(0));
    refresh();
    log_pos = 0;
}

static void init(void) {
    initscr();
    start_color();
    use_default_colors();
    init_pair(1, COLOR_RED, -1);
    raw();
    noecho();
    keypad(stdscr, TRUE);
}

static void cleanup(void) {
    curs_set(1);
    echo();
    noraw();
    endwin();
}

static void window_fill(WINDOW *w, chtype c) {
    int width, height;
    getmaxyx(w, height, width);
    for(int y = 0; y != height; ++y) {
        wmove(w, y, 0);
        for(int x = 0; x != width; ++x)
            waddch(w, c);
    }
}

static bool window_leave(struct subs_curses *s, struct window *w) {
    const ptrdiff_t i = w - s->windows;
    assert(0 <= i && (size_t)i < s->n_windows);
    (void)i;
    return !w->leave || w->leave(w);
}

static bool window_enter(struct subs_curses *s, struct window *w) {
    const ptrdiff_t i = w - s->windows;
    assert(0 <= i && (size_t)i < s->n_windows);
    s->cur_window = (size_t)i;
    return !w->enter || w->enter(w);
}

static bool resize(
    struct subs_curses *sc, struct source_bar *source_bar,
    struct subs_bar *subs_bar, struct videos *videos)
{
    if(!(sc->flags & RESIZED))
        return true;
    sc->flags = (u8)(sc->flags & ~RESIZED);
    int x, y;
    if(!get_terminal_size(&x, &y))
        return false;
    resizeterm(y, x);
    clear();
    refresh();
    return source_bar_update_count(source_bar)
        && calc_pos_lua(sc->L, source_bar, subs_bar, videos)
        && source_bar_reload(source_bar)
        && subs_bar_reload(subs_bar)
        && videos_resize(videos);
}

static bool menu_nav(WINDOW *w, MENU *m, int c) {
    switch(c) {
    case 'h': case KEY_LEFT:  menu_driver(m, REQ_LEFT_ITEM);  break;
    case 'j': case KEY_DOWN:  menu_driver(m, REQ_DOWN_ITEM);  break;
    case 'k': case KEY_UP:    menu_driver(m, REQ_UP_ITEM);    break;
    case 'l': case KEY_RIGHT: menu_driver(m, REQ_RIGHT_ITEM); break;
    case 'b': case KEY_PPAGE: menu_driver(m, REQ_SCR_UPAGE);  break;
    case 'f': case KEY_NPAGE: menu_driver(m, REQ_SCR_DPAGE);  break;
    case 'g': case KEY_HOME:  menu_driver(m, REQ_FIRST_ITEM); break;
    case 'G': case KEY_END:   menu_driver(m, REQ_LAST_ITEM);  break;
    default: return false;
    }
    wrefresh(w);
    return true;
}

static bool form_nav(WINDOW *w, FORM *f, int c) {
    switch(c) {
    case KEY_LEFT:
        form_driver(f, REQ_PREV_CHAR);
        break;
    case KEY_RIGHT:
        form_driver(f, REQ_NEXT_CHAR);
        break;
    case DEL:
        form_driver(f, REQ_PREV_CHAR);
        /* fallthrough */
    case KEY_DC:
        form_driver(f, REQ_DEL_CHAR);
        break;
    case '\t':
    case KEY_DOWN:
        form_driver(f, REQ_NEXT_FIELD);
        form_driver(f, REQ_END_LINE);
        break;
    case KEY_UP:
        form_driver(f, REQ_PREV_FIELD);
        form_driver(f, REQ_END_LINE);
        break;
    default:
        return false;
    }
    wrefresh(w);
    return true;
}

static void cmd_form(WINDOW *w) {
    FIELD *fields[] = {
        new_field(1, 10, 0, 8, 0, 0),
        new_field(1, 10, 2, 8, 0, 0),
        NULL,
    };
    FOR_EACH(FIELD*, x, fields) {
        set_field_back(*x, A_UNDERLINE);
        field_opts_off(*x, O_AUTOSKIP);
    }
    set_field_type(fields[0], TYPE_ALPHA, 0);
    FORM *const f = new_form(fields);
    int width, height;
    getmaxyx(w, height, width);
    set_form_win(f, w);
    set_form_sub(f, derwin(w, height - 2, width - 2, 1, 1));
    post_form(f);
    wrefresh(w);
    mvwprintw(w, 3, 1, "field1: ");
    mvwprintw(w, 1, 1, "field0: ");
    wrefresh(w);
    int c;
    while((c = getch()), c != ERR && c != '\n') {
        if(form_nav(w, f, c))
            continue;
        form_driver(f, c);
        wrefresh(w);
    }
    unpost_form(f);
    free_form(f);
    FOR_EACH(FIELD*, x, fields)
        free_field(*x);
    window_fill(w, ' ');
    box(w, 0, 0);
    curs_set(1);
    wrefresh(w);
}

static void cmd_menu(WINDOW *w) {
    const int n = 100;
    ITEM **const items = calloc((size_t)n, sizeof(*items));
    char name[100][3] = {0};
    for(int i = 0; i != n; ++i) {
        name[i][0] = '0' + (char)(i / 10);
        name[i][1] = '0' + (char)(i % 10);
        items[i] = new_item(name[i], name[i]);
        if(i & 1)
            item_opts_off(items[i], O_SELECTABLE);
    }
    MENU *const m = new_menu(items);
    int width, height, x = getcurx(w), y = getcury(w);
    getmaxyx(w, height, width);
    set_menu_win(m, w);
    set_menu_sub(m, derwin(w, height - 2, width - 2, 1, 1));
    set_menu_format(m, 0, 4);
    set_menu_opts(m, O_SHOWDESC | O_SHOWMATCH | O_NONCYCLIC);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    set_menu_grey(m, COLOR_PAIR(2));
    post_menu(m);
    wrefresh(w);
    int c;
    while((c = getch()), c != ERR && c != 'q' && c != '\n') {
        if(menu_nav(w, m, c))
            continue;
        switch(c) {
        case ' ': menu_driver(m, REQ_TOGGLE_ITEM); break;
        }
        wrefresh(w);
    }
    unpost_menu(m);
    wmove(w, y, x);
    if(c == '\n') {
        wprintw(w, "selected:");
        for(int i = 0; i != n; ++i)
            if(item_value(items[i]) == TRUE) {
                wprintw(w, " ");
                wprintw(w, name[i]);
            }
    }
    wrefresh(w);
    free_menu(m);
    for(int i = 0; i != n; ++i)
        free_item(items[i]);
}

static bool process_key(
    struct subs_curses *sc, struct source_bar *source_bar,
    struct subs_bar *subs_bar, struct videos *videos, int c)
{
    const size_t cur = sc->cur_window, n = sc->n_windows;
    struct window *const windows = sc->windows;
    switch(c) {
    case 'l' & CTRL:
        for(size_t i = 0; i != n; ++i)
            redrawwin(windows[i].w);
        doupdate();
        break;
    case 'z' & CTRL: kill(0, SIGTSTP); break;
//    case 'F': cmd_form(windows[VIDEOS_IDX].w); break;
//    case 'M': cmd_menu(windows[VIDEOS_IDX].w); break;
    case 'R':
        return source_bar_reload(source_bar)
            && subs_bar_reload(subs_bar)
            && videos_reload(videos);
//    case 'V': suspend_tui(); system("vim"); resume_tui(); break;
    case 'W':
        subs_bar_toggle_not_watched(subs_bar);
        videos_toggle_not_watched(videos);
        return subs_bar_reload(subs_bar)
            && videos_reload(videos);
    case 'w':
        subs_bar_toggle_watched(subs_bar);
        videos_toggle_watched(videos);
        return subs_bar_reload(subs_bar)
            && videos_reload(videos);
    case KEY_BTAB:
        return change_window(sc, (cur + n - 1) % n);
    case '\t':
        return change_window(sc, (cur + 1) % n);
    default:
        return windows[cur].input(&windows[cur], c);
    }
    return true;
}

bool query_to_int(
    sqlite3 *db, const char *sql, size_t len, const int *param, int *p)
{
    int ret = 0;
    bool ok = false;
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, (int)len, 0, &stmt, NULL);
    if(!stmt)
        goto end;
    if(param && sqlite3_bind_int(stmt, 1, *param) != SQLITE_OK)
        goto end;
    for(;;)
        switch(sqlite3_step(stmt)) {
        case SQLITE_BUSY:
            continue;
        case SQLITE_DONE:
            fprintf(stderr, "query returned no results");
            ok = true;
        default:
            goto end;
        case SQLITE_ROW:
            ret = sqlite3_column_int(stmt, 0);
            ok = true;
            goto end;
        }
end:
    if(!(sqlite3_finalize(stmt) == SQLITE_OK && ok))
        return false;
    *p = ret;
    return true;
}

char *name_with_counts(
    int width, const char *prefix, const char *title, int n0, int n1
) {
    const size_t prefix_len = strlen(prefix);
    const size_t title_len = strlen(title);
    const size_t title_len8 = strlen_utf8(title);
    const size_t n0_len = int_digits(n0);
    const size_t n1_len = int_digits(n1);
    const size_t counts_len =
        (sizeof(" ") - 1) + n0_len + (sizeof("/") - 1) + n1_len;
    const size_t len = MAX(
        prefix_len + title_len,
        (size_t)width + (title_len - title_len8));
    char *const ret = malloc(len + 1);
    snprintf(
        ret, len + 1, "%s%s%*s",
        prefix, title, (int)(len - counts_len), "");
    if(counts_len <= len)
        snprintf(ret + (len - counts_len), counts_len + 1, " %d/%d", n0, n1);
    return ret;
}

bool change_window(struct subs_curses *s, size_t i) {
    struct window *const v = s->windows;
    return window_leave(s, &v[s->cur_window])
        && window_enter(s, &v[i]);
}

void suspend_tui(void) {
    def_prog_mode();
    endwin();
}

void resume_tui(void) {
    reset_prog_mode();
    doupdate();
}

bool subs_start_tui(const struct subs *s) {
    if(!setlocale(LC_ALL, ""))
        return log_errno("setlocale"), false;
    log_prev = log_set_fn(curses_log_fn);
    init();
    struct subs_curses sc = {.db = s->db, .L = s->L, .flags = RESIZED};
    struct videos videos = {.s = &sc};
    struct subs_bar subs_bar = {.s = &sc, .videos = &videos};
    struct source_bar source_bar = {
        .s = &sc,
        .subs_bar = &subs_bar,
        .videos = &videos,
    };
    struct window windows[] = {
        [SOURCE_BAR_IDX] = {
            .data = &source_bar,
            .leave = source_bar_leave,
            .enter = source_bar_enter,
            .input = source_bar_input,
        },
        [SUBS_BAR_IDX] = {
            .data = &subs_bar,
            .leave = subs_bar_leave,
            .enter = subs_bar_enter,
            .input = subs_bar_input,
        },
        [VIDEOS_IDX] = {
            .data = &videos,
            .leave = videos_leave,
            .enter = videos_enter,
            .input = videos_input,
        },
    };
    sc.windows = windows;
    sc.n_windows = ARRAY_SIZE(windows);
    init_lua(s->L, &videos);
    bool ret = false;
    if(!resize(&sc, &source_bar, &subs_bar, &videos))
        goto end;
    window_enter(&sc, &windows[SOURCE_BAR_IDX]);
    struct input input = {0};
    if(!init_input(&input))
        goto end;
    for(;;) {
        const struct input_event e = process_input(&input);
        switch(e.type) {
        case INPUT_TYPE_QUIT:
            ret = true; /* fallthrough */
        case INPUT_TYPE_ERR:
            goto end;
        case INPUT_TYPE_RESIZE:
            sc.flags = (u8)(sc.flags | RESIZED);
            break;
        case INPUT_TYPE_KEY:
            if(!process_key(&sc, &source_bar, &subs_bar, &videos, e.key))
                goto end;
            break;
        }
        if(!resize(&sc, &source_bar, &subs_bar, &videos))
            goto end;
        process_log();
    }
end:
    videos_destroy(&videos);
    subs_bar_destroy(&subs_bar);
    source_bar_destroy(&source_bar);
    cleanup();
    log_set_fn(log_prev);
    if(log_pos)
        LOG_ERR("%.*s", (int)log_pos, log);
    return ret;
}
