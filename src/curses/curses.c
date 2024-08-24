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
#include "lua.h"
#include "message.h"
#include "source.h"
#include "subs.h"
#include "videos.h"

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
    return !w->leave || w->leave(w);
}

static bool window_enter(struct subs_curses *s, struct window *w) {
    const ptrdiff_t i = w - s->windows;
    assert(0 <= i && (size_t)i < s->n_windows);
    s->cur_window = (size_t)i;
    return !w->enter || w->enter(w);
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

static bool process_key(
    struct subs_curses *sc, struct input *input, struct message *message,
    struct source_bar *source_bar, struct subs_bar *subs_bar,
    struct videos *videos, int c)
{
    const size_t cur = sc->cur_window, n = sc->n_windows;
    struct window *const windows = sc->windows;
    if(message->msg) {
        message_hide(message);
        for(size_t i = 0; i != n; ++i)
            windows[i].redraw(windows + i);
        return true;
    }
    const int count = sc->input_count;
    sc->input_count = -1;
    switch(windows[cur].input(&windows[cur], c, count == -1 ? 1 : count)) {
    case KEY_ERROR:
        return false;
    case KEY_HANDLED:
        return true;
    case KEY_IGNORED:
        break;
    }
    switch(c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        sc->input_count = (c - '0') + ((count == -1) ? 0 : 10 * count);
        return true;
    case 'l' & CTRL:
        for(size_t i = 0; i != n; ++i)
            windows[i].redraw(windows + i);
        doupdate();
        break;
    case 'z' & CTRL: kill(0, SIGTSTP); break;
    case 'R' & CTRL:
        return source_bar_reload(source_bar)
            && subs_bar_reload(subs_bar)
            && videos_reload(videos);
    case 'W':
        if((sc->flags = (u8)(sc->flags ^ NOT_WATCHED)) & NOT_WATCHED)
            sc->flags = (u8)(sc->flags & ~WATCHED);
        source_bar_update_title(source_bar);
        return subs_bar_reload(subs_bar)
            && videos_reload(videos);
    case ESC:
    case 'q':
        if(!input_send_event(input, EVENT(QUIT)))
            return false;
        break;
    case 'w':
        if((sc->flags = (u8)(sc->flags ^ WATCHED)) & WATCHED)
            sc->flags = (u8)(sc->flags & ~NOT_WATCHED);
        source_bar_update_title(source_bar);
        return subs_bar_reload(subs_bar)
            && videos_reload(videos);
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
    char **const p = message_push(s->priv, NULL);
    return p && (*p = strdup(msg));
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
    struct message message = {0};
    struct subs_curses sc = {
        .db = s->db,
        .L = s->L,
        .flags = RESIZED,
        .priv = &message,
    };
    struct videos videos = {
        .s = &sc,
        .db = subs_new_db_connection(s),
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
            .redraw = videos_bar_redraw,
            .input = videos_input,
        },
    };
    sc.windows = windows;
    sc.n_windows = ARRAY_SIZE(windows);
    init_lua(s->L, &sc, &videos);
    bool ret = false;
    if(!resize(&sc, &message, &source_bar, &subs_bar, &videos))
        goto end;
    struct input input = {0};
    if(!input_init(&input))
        goto end;
    videos.input = &input;
    struct task_thread task_thread = {
        .data = &input,
        .error_f = task_error,
    };
    if(!task_thread_init(&task_thread))
        goto end;
    videos.task_thread = &task_thread;
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
            if(!process_key(
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
