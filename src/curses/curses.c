#include "curses.h"

#include <assert.h>
#include <locale.h>
#include <signal.h>

#include "../log.h"
#include "../subs.h"
#include "../task.h"
#include "../unix.h"
#include "../util.h"

#include "input.h"
#include "message.h"
#include "source.h"
#include "subs.h"
#include "videos.h"
#include "lua/lua.h"

#define P(s) ((struct private*)(s)->priv)

static char log[4096];
static size_t log_pos;
static log_fn *log_prev;

struct private {
    struct message *message;
};

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
    curs_set(0);
    keypad(stdscr, TRUE);
}

static void cleanup(void) {
    curs_set(1);
    echo();
    noraw();
    endwin();
}

static bool window_leave(struct subs_curses *s, struct window *w) {
    const ptrdiff_t i = w - s->windows;
    assert(0 <= i && (size_t)i < s->n_windows);
    (void)i;
    return !w->leave || w->leave(w->data);
}

static bool window_enter(struct subs_curses *s, struct window *w) {
    assert(IN_ARRAY(w, s->windows, s->n_windows));
    s->cur_window = (size_t)(w - s->windows);
    return w->enter && w->enter(w->data);
}

static bool resize(
    struct subs_curses *sc, struct message *message,
    struct source_bar *source_bar, struct subs_bar *subs_bar,
    struct videos *videos)
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
        && calc_pos_lua(sc->L, message, source_bar, subs_bar, videos)
        && source_bar_reload(source_bar)
        && subs_bar_reload(subs_bar)
        && videos_resize(videos);
}

static bool process_messages(struct subs_curses *sc, struct message *message);
static enum subs_curses_key window_input(struct subs_curses *sc, int c);
static enum subs_curses_key process_key(
    struct subs_curses *sc, struct input *input,
    struct source_bar *source_bar, struct subs_bar *subs_bar,
    struct videos *videos, int c, int count);

static bool process_input(
    struct subs_curses *sc, struct input *input, struct message *message,
    struct source_bar *source_bar, struct subs_bar *subs_bar,
    struct videos *videos, int c)
{
    if(process_messages(sc, message))
        return true;
    const int count = sc->input_count;
    sc->input_count = -1;
    switch(window_input(sc, c)) {
    case KEY_ERROR: return false;
    case KEY_HANDLED: return true;
    case KEY_IGNORED: break;
    }
    return process_key(sc, input, source_bar, subs_bar, videos, c, count);
}

bool process_messages(struct subs_curses *sc, struct message *message) {
    if(!message->msg)
        return false;
    struct window *const windows = sc->windows;
    const size_t n = sc->n_windows;
    message_hide(message);
    for(size_t i = 0; i != n; ++i)
        windows[i].redraw(windows[i].data);
    return true;
}

enum subs_curses_key window_input(struct subs_curses *sc, int c) {
    struct window *const w = sc->windows + sc->cur_window;
    if(!w->input)
        return KEY_IGNORED;
    return w->input(w->data, c);
}

enum subs_curses_key process_key(
    struct subs_curses *sc, struct input *input,
    struct source_bar *source_bar, struct subs_bar *subs_bar,
    struct videos *videos, int c, int count)
{
    struct window *const windows = sc->windows;
    const size_t cur = sc->cur_window, n = sc->n_windows;
    switch(c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        sc->input_count = (c - '0') + ((count == -1) ? 0 : 10 * count);
        return true;
    case 'l' & CTRL:
        for(size_t i = 0; i != n; ++i)
            if(windows[i].redraw)
                windows[i].redraw(windows[i].data);
        doupdate();
        break;
    case 'z' & CTRL: kill(0, SIGTSTP); break;
    case 'J': return subs_bar_next(subs_bar);
    case 'K': return subs_bar_prev(subs_bar);
    case 'R' & CTRL:
        return source_bar_reload(source_bar)
            && subs_bar_reload(subs_bar)
            && videos_reload(videos);
    case 'W':
        return toggle_not_watched(sc);
    case ESC:
    case 'q':
        if(!input_send_event(input, EVENT(QUIT)))
            return false;
        break;
    case 'w':
        return toggle_watched(sc);
    case KEY_BTAB:
        return change_window(sc, (cur + n - 1) % n);
    case '\t':
        return change_window(sc, (cur + 1) % n);
    }
    return true;
}

static bool task_error(void *p) {
    return input_send_event(p, (struct input_event) {
        .type = INPUT_TYPE_ERR,
    });
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
    char *const ret = checked_malloc(len + 1);
    if(!ret)
        return NULL;
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

bool add_message(struct subs_curses *s, const char *msg) {
    char **const p = message_push(P(s)->message, NULL);
    return p && (*p = strdup(msg));
}

// XXX delay reload
bool toggle_watched(struct subs_curses *s) {
    if((s->flags = (u8)(s->flags ^ WATCHED)) & WATCHED)
        s->flags = (u8)(s->flags & ~NOT_WATCHED);
    source_bar_update_title(s->windows[SOURCE_BAR_IDX].data);
    return subs_bar_reload(s->windows[SUBS_BAR_IDX].data)
        && videos_reload(s->windows[VIDEOS_IDX].data);
}

// XXX delay reload
bool toggle_not_watched(struct subs_curses *s)
{
    if((s->flags = (u8)(s->flags ^ NOT_WATCHED)) & NOT_WATCHED)
        s->flags = (u8)(s->flags & ~WATCHED);
    source_bar_update_title(s->windows[SOURCE_BAR_IDX].data);
    return subs_bar_reload(s->windows[SUBS_BAR_IDX].data)
        && videos_reload(s->windows[VIDEOS_IDX].data);
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
    bool ret = false;
    struct input input = {0};
    if(!input_init(&input))
        goto end;
    struct task_thread task_thread = {
        .data = &input,
        .error_f = task_error,
    };
    if(!task_thread_init(&task_thread))
        goto end;
    if(!setlocale(LC_ALL, ""))
        return log_errno("setlocale"), false;
    log_prev = log_set_fn(curses_log_fn);
    init();
    struct message message = {0};
    struct private priv = {
        .message = &message,
    };
    struct subs_curses sc = {
        .db = s->db,
        .L = s->L,
        .flags = RESIZED,
        .task_thread = &task_thread,
        .priv = &priv,
    };
    struct videos videos = {
        .s = &sc,
        .db = subs_new_db_connection(s),
        .input = &input,
        .task_thread = &task_thread,
    };
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
            .redraw = source_bar_redraw,
            .input = source_bar_input,
        },
        [SUBS_BAR_IDX] = {
            .data = &subs_bar,
            .leave = subs_bar_leave,
            .enter = subs_bar_enter,
            .redraw = subs_bar_redraw,
            .input = subs_bar_input,
        },
        [VIDEOS_IDX] = {
            .data = &videos,
            .leave = videos_leave,
            .enter = videos_enter,
            .redraw = videos_redraw,
            .input = videos_input,
        },
    };
    sc.windows = windows;
    sc.n_windows = ARRAY_SIZE(windows);
    init_lua(s->L, &sc, &videos);
    if(!resize(&sc, &message, &source_bar, &subs_bar, &videos))
        goto end;
    window_enter(&sc, &windows[sc.cur_window]);
    for(;;) {
        const struct input_event e = input_process(&input);
        switch(e.type) {
        case INPUT_TYPE_QUIT:
            ret = true; /* fallthrough */
        case INPUT_TYPE_ERR:
            goto end;
        case INPUT_TYPE_RESIZE:
            sc.flags = (u8)(sc.flags | RESIZED);
            break;
        case INPUT_TYPE_KEY:
            if(!process_input(
                &sc, &input, &message, &source_bar, &subs_bar, &videos, e.key
            ))
                goto end;
            break;
        case INPUT_TYPE_TASK:
            if(!e.task.f(e.task.p))
                goto end;
            break;
        }
        if(!resize(&sc, &message, &source_bar, &subs_bar, &videos))
            goto end;
        if(!message_process(&message))
            goto end;
        process_log();
    }
end:
    message_destroy(&message);
    videos_destroy(&videos);
    subs_bar_destroy(&subs_bar);
    source_bar_destroy(&source_bar);
    ret = task_thread_destroy(&task_thread) && ret;
    ret = input_destroy(&input) && ret;
    cleanup();
    log_set_fn(log_prev);
    if(log_pos)
        LOG_ERR("%.*s", (int)log_pos, log);
    return ret;
}
