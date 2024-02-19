#include "update.h"

#include <math.h>

#include <sqlite3.h>

#include "buffer.h"
#include "log.h"
#include "subs.h"
#include "unix.h"

enum result { DONE = 1, ERR };

#define LOGGER \
    "class logger(object):\n" \
    "    warning = lambda _, *x: print(*x, file=sys.stderr)\n" \
    "    error = lambda _, *x: print(*x, file=sys.stderr)\n" \
    "    debug = lambda *_: None\n"

static const char *CHANNEL_ENTRIES =
    "import sys\n"
    "import yt_dlp as youtube_dl\n"
    LOGGER
    "PAGE_SIZE = 20\n"
    "for line in sys.stdin:\n"
    "    id, page = line.rstrip().split()\n"
    "    page = int(page)\n"
    "    ytdl = youtube_dl.YoutubeDL({"
            "'logger': logger(),"
            "'extract_flat': True,"
            "'playliststart': 1 + PAGE_SIZE * page,"
            "'playlistend': PAGE_SIZE * (page + 1),"
    "    })\n"
    "    try:\n"
    "        info = ytdl.extract_info("
                "f'https://www.youtube.com/channel/{id}/videos',"
                " download=False)\n"
    "    except youtube_dl.utils.DownloadError:\n"
    "        print(f'failed to get information for {id}:', file=sys.stderr)\n"
    "        raise\n"
    "    out = []\n"
    "    for x in info['entries']:\n"
    "        id, title = x['id'], x['title']\n"
    "        title = title.replace('\\n', '\\\\n')\n"
    "        out.append(' '.join((id, title)))\n"
    "    out = '\\n'.join(out) + '\\n'\n"
    "    sys.stdout.write(out)\n";

static const char *VIDEO_INFO =
    "import datetime\n"
    "import sys\n"
    "import yt_dlp as youtube_dl\n"
    LOGGER
    "ytdl = youtube_dl.YoutubeDL({'logger': logger()})\n"
    "for id in sys.stdin:\n"
    "    try:\n"
    "        info = ytdl.extract_info("
                "f'https://www.youtube.com/watch?v={id}',"
                "download=False)\n"
    "    except youtube_dl.utils.DownloadError as ex:\n"
    "        s = str(ex)\n"
    "        if 'Premieres in ' in s or 'Sign in to confirm your age' in s:\n"
    "            sys.stdout.write('0 0')\n"
    "            continue\n"
    "        print(f'failed to get information for {id}:', file=sys.stderr)\n"
    "        raise\n"
    "    d = datetime.datetime.strptime(info['upload_date'], '%Y%m%d')\n"
    "    sys.stdout.write(\n"
    "        '{:d} {:d}'.format(\n"
    "            int(d.timestamp()),\n"
    "            int(info['duration'])))\n";

#undef LOGGER

bool update_youtube_init(struct update_youtube *u) {
    *u = (struct update_youtube){
        .channel_pid = -1,
        .channel_r   = -1,
        .channel_w   = -1,
        .info_pid    = -1,
        .info_r      = -1,
        .info_w      = -1,
    };
    return exec_with_pipes(
            "python", (const char*[]){"python", "-uc", CHANNEL_ENTRIES, NULL},
            &u->channel_pid, &u->channel_r, &u->channel_w)
        && exec_with_pipes(
            "python", (const char*[]){"python", "-uc", VIDEO_INFO, NULL},
            &u->info_pid, &u->info_r, &u->info_w);
}

bool update_youtube_destroy(struct update_youtube *p) {
    bool ret = true;
    if(p->channel_r != -1 && close(p->channel_r) == -1)
        LOG_ERRNO("close", 0), ret = false;
    if(p->channel_w != -1 && close(p->channel_w) == -1)
        LOG_ERRNO("close", 0), ret = false;
    if(p->info_r != -1 && close(p->info_r) == -1)
        LOG_ERRNO("close", 0), ret = false;
    if(p->info_w != -1 && close(p->info_w) == -1)
        LOG_ERRNO("close", 0), ret = false;
    if(p->channel_pid != -1)
        ret = wait_for_pid(p->channel_pid) && ret;
    if(p->info_pid != -1)
        ret = wait_for_pid(p->info_pid) && ret;
    return ret;
}

static enum result process(
    sqlite3 *db, const struct update_youtube *u, const struct buffer *input,
    u32 flags, bool verbose, struct buffer *b, int id, int *n);

bool update_youtube(
    const struct subs *s, struct update_youtube *u, struct buffer *b, u32 flags,
    int id, const char *ext_id)
{
    sqlite3 *const db = s->db;
    const bool verbose = s->log_level;
    struct buffer tmp = {0};
    bool ret = false;
    for(size_t page = 0;; ++page) {
        if(verbose)
            fprintf(stderr, "page %zu\n", page);
        b->n = 0;
        buffer_printf(b, "%s %zu\n", ext_id, page);
        --b->n;
        if(write(u->channel_w, b->p, b->n) != (ssize_t)b->n) {
            LOG_ERRNO("write", 0);
            goto end;
        }
        buffer_reserve(b, 4096);
        const ssize_t nr = read(u->channel_r, b->p, b->cap);
        if(nr == -1) {
            LOG_ERRNO("read", 0);
            goto end;
        }
        b->n = (size_t)nr;
        int n_updated = 0;
        switch(process(db, u, b, flags, verbose, &tmp, id, &n_updated)) {
        case DONE: ret = true; goto end;
        case ERR: goto end;
        }
        if(verbose)
            fprintf(stderr, "added %d new video(s)\n", n_updated);
    }
end:
    free(tmp.p);
    return ret;
}

static bool process_line(
    sqlite3 *db, const struct update_youtube *u, struct buffer *b,
    bool verbose, int sub_id, const char *ext_id, size_t ext_id_len,
    const char *title, size_t title_len, bool *done, int *n);

static enum result process(
    sqlite3 *db, const struct update_youtube *u, const struct buffer *input,
    u32 flags, bool verbose, struct buffer *b, int id, int *n_p)
{
    const char *p = input->p;
    size_t n = input->n;
    if(strncmp("\n", p, n) == 0)
        return DONE;
    bool done = true;
    while(n && *p != '\n') {
        const char *const ext_id = p;
        const char *const space = memchr(ext_id, ' ', n);
        if(!space)
            goto invalid;
        const size_t ext_id_len = (size_t)(space - ext_id);
        const char *const title = space + 1;
        const char *const new_line = memchr(title, '\n', n - ext_id_len - 1);
        if(!new_line)
            goto invalid;
        const size_t title_len = (size_t)(new_line - title);
        if(!process_line(
            db, u, b, verbose,
            id, ext_id, ext_id_len, title, title_len, &done, n_p
        ))
            return ERR;
        n -= (size_t)(new_line - ext_id + 1);
        p = new_line + 1;
    }
    if(done) {
        if(flags & SUBS_UPDATE_DEEP)
            return 0;
        if(verbose)
            fputs(
                "page has no new videos"
                " (use a deep update to unconditionally fetch all pages)\n",
                stderr);
        return DONE;
    }
    return 0;
invalid:
    LOG_ERR("invalid output line: '%.*s'\n", (int)n, p);
    return ERR;
}

static bool exists_query(
    sqlite3 *db, const char *sql, int len, const char *arg, int arg_len,
    bool *p);
static bool get_info(
    const struct update_youtube *u,
    const char *id, size_t id_len, struct buffer *b,
    i64 *timestamp, i64 *duration_seconds);
static bool insert(
    sqlite3 *db, bool verbose, int id, const char *ext_id, size_t ext_id_len,
    const char *title, size_t title_len, i64 timestamp, i64 duration_seconds,
    int *n);

static bool process_line(
    sqlite3 *db, const struct update_youtube *u, struct buffer *b,
    bool verbose, int id, const char *ext_id, size_t ext_id_len,
    const char *title, size_t title_len, bool *done, int *n)
{
    const char sql[] = "select 1 from videos where ext_id == ?";
    bool exists;
    if(!exists_query(
        db, sql, sizeof(sql) - 1, ext_id, (int)ext_id_len, &exists
    ))
        return false;
    if(exists)
        return true;
    *done = false;
    i64 timestamp, duration_seconds;
    if(!get_info(u, ext_id, ext_id_len, b, &timestamp, &duration_seconds))
        return false;
    if(!timestamp || !duration_seconds)
        return true;
    return insert(
        db, verbose, id, ext_id, ext_id_len, title, title_len, timestamp,
        duration_seconds, n);
}

static bool exists_query(
    sqlite3 *db, const char *sql, int len, const char *arg, int arg_len,
    bool *p)
{
    sqlite3_stmt *stmt;
    sqlite3_prepare_v3(db, sql, (int)len, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(sqlite3_bind_text(stmt, 1, arg, arg_len, SQLITE_STATIC) != SQLITE_OK)
        goto end;
    for(;;)
        switch(sqlite3_step(stmt)) {
        case SQLITE_BUSY: continue;
        case SQLITE_ROW: ret = *p = true; goto end;
        case SQLITE_DONE: ret = true; *p = false; goto end;
        default: goto end;
        }
end:
    ret = (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
    return ret;
}

static bool get_info(
    const struct update_youtube *u,
    const char *id, size_t id_len, struct buffer *b,
    i64 *timestamp, i64 *duration_seconds)
{
    b->n = 0;
    buffer_reserve(b, id_len + 1);
    buffer_append(b, id, id_len);
    buffer_append(b, "\n", 1);
    ssize_t n = (ssize_t)b->n;
    if(write(u->info_w, b->p, (size_t)n) != n)
        return LOG_ERRNO("write", 0), false;
    b->n = 0;
    buffer_reserve(b, 64);
    n = read(u->info_r, b->p, b->cap - 1);
    if(n <= 0)
        return false;
    ((char*)b->p)[n] = 0;
    const i64 t = parse_i64((const char*)b->p);
    if(t == -1)
        goto err;
    const char *const space = strchr((const char*)b->p, ' ');
    if(!space)
        goto err;
    const i64 d = parse_i64(space + 1);
    if(d == -1)
        goto err;
    *timestamp = t;
    *duration_seconds = d;
    return true;
err:
    LOG_ERR("invalid yt-dlp output:\n", 0);
    fprintf(stderr, "---\n%s---\n", (const char*)b->p);
    return false;
}

static bool insert(
    sqlite3 *db, bool verbose, int id, const char *ext_id, size_t ext_id_len,
    const char *title, size_t title_len, i64 timestamp, i64 duration_seconds,
    int *n)
{
    const char sql[] =
        "insert into videos (sub, ext_id, timestamp, duration_seconds, title)"
        " values (?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v3(db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(!(
        sqlite3_bind_int(stmt, 1, id) == SQLITE_OK
        && sqlite3_bind_text(stmt, 2, ext_id, (int)ext_id_len, SQLITE_STATIC)
            == SQLITE_OK
        && sqlite3_bind_int64(stmt, 3, timestamp) == SQLITE_OK
        && sqlite3_bind_int64(stmt, 4, duration_seconds) == SQLITE_OK
        && sqlite3_bind_text(stmt, 5, title, (int)title_len, SQLITE_STATIC)
            == SQLITE_OK
    ))
        goto err;
    for(;;)
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW:
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: goto done;
        default: goto err;
        }
done:
    if(verbose)
        fprintf(
            stderr, "created new video: %" PRId64 " %d %.*s %" PRId64 " %"
                PRId64 " %.*s\n",
            (i64)sqlite3_last_insert_rowid(db), id, (int)ext_id_len, ext_id,
            timestamp, duration_seconds, (int)title_len, title);
    ++(*n);
    ret = true;
err:
    ret = (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
    return ret;
}
