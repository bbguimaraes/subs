#include "subs.h"

#include <stddef.h>

#include <getopt.h>
#include <time.h>

#include "buffer.h"
#include "db.h"
#include "def.h"
#include "http.h"
#include "log.h"
#include "subs.h"

static void usage(void) {
    printf(
"Usage: %s [options] [command [args]]\n"
"\n"
"Options:\n"
"    -h, --help      This help text.\n"
"    -v              Increase previous log level, can appear multiple\n"
"                    times.\n"
"    --log-level N   Set log level to `N`.\n"
"    -f, --file DB   Use database file `DB`, default: $XDG_DATA_HOME/subs/db.\n"
"\n"
"Commands:\n"
"    ls              List subscriptions.\n"
"    videos          List videos.\n"
"    add NAME ID     Add a subscription.\n"
"    watched [-r|--remove] ID\n"
"                    Mark videos as watched (`-r` to unmark)\n"
"    update [OPTIONS]\n"
"                    Fetch new videos from subscriptions.\n",
        PROG_NAME);
}

static bool check_argc(const char *cmd, int argc, int n) {
    if(argc == n)
        return true;
    log_err("%s: invalid number of arguments (%d != %d)\n", cmd, argc, n);
    return false;
}

static bool parse_log_level(const char *p, u32 *l) {
    if(!p || !*p)
        return true;
    const char *e = p + strlen(p);
    unsigned long v = strtoul(p, (char**)&e, 0);
    if(*e || UINT32_MAX < v) {
        log_err("invalid log level: %s\n", p);
        return false;
    }
    *l = (u32)v;
    return true;
}

static void format_sub(sqlite3_stmt *stmt, FILE *f) {
    fprintf(
        f, "%d %s %s %s\n",
        sqlite3_column_int(stmt, 0),
        subs_type_name(sqlite3_column_int(stmt, 1)),
        sqlite3_column_text(stmt, 2),
        sqlite3_column_text(stmt, 3));
}

static void format_video(sqlite3_stmt *stmt, FILE *f) {
    fprintf(
        f,
        "%d"    // id
        " %s"   // watched
        " %s"   // sub type
        " %lld" // timestamp
        " %s"   // ext_id
        " %s"   // name
        " %s"   // title
        "\n",
        sqlite3_column_int(stmt, 0),
        sqlite3_column_int(stmt, 1) ? "w" : "0",
        subs_type_name(sqlite3_column_int(stmt, 2)),
        sqlite3_column_int64(stmt, 3),
        sqlite3_column_text(stmt, 4),
        sqlite3_column_text(stmt, 5),
        sqlite3_column_text(stmt, 6));
}

const char *subs_type_name(enum subs_type type) {
    static const char *v[SUBS_TYPE_MAX] = {
        [0] = "unknown",
        [SUBS_LBRY] = "lbry",
        [SUBS_YOUTUBE] = "youtube",
    };
    return v[type < SUBS_TYPE_MAX ? type : 0];
}

enum subs_type subs_parse_type(const char *s) {
#define C(u, n) if(strcmp(s, #n) == 0) return SUBS_ ## u;
    C(LBRY, lbry)
    C(YOUTUBE, youtube)
#undef C
    log_err("invalid subscription type: %s\n", s);
    return 0;
}

static bool init_from_env(struct subs *s);
static const char *find_db(const char *path, char v[static SUBS_MAX_PATH]);

bool subs_init(struct subs *s) {
    if(!init_from_env(s))
        return false;
    const char *const db_path = find_db(s->db_path, (char[SUBS_MAX_PATH]){0});
    if(!db_path)
        return false;
    sqlite3 *const db = db_init(db_path);
    if(!db)
        return false;
    s->db = db;
    if(db_path != s->db_path)
        strcpy(s->db_path, db_path);
    if(!s->url)
        s->url = "localhost:5279";
    return true;
}

static bool init_from_env(struct subs *s) {
    if(!parse_log_level(getenv("SUBS_LOG_LEVEL"), &s->log_level))
        return false;
    return true;
}

static const char *find_db(const char *path, char v[static SUBS_MAX_PATH]) {
    if(*path)
        return path;
#define B "/subs/db"
    const char *const base = B;
    const char *const home_base = "/.local/share" B;
#undef B
    const char *const xdg = getenv("XDG_DATA_HOME");
    if(xdg) {
        if(!join_path(v, 2, xdg, base))
            return false;
    } else {
        const char *const home = getenv("HOME");
        if(!home)
            goto end;
        if(!join_path(v, 2, home, home_base))
            return false;
    }
    if(file_exists(v))
        return v;
end:
    return ":memory:";
}

bool subs_init_from_argv(struct subs *s, int *argc_p, char ***argv_p) {
    enum { CMD_HELP = 1, LOG_LEVEL = 0 };
    const char short_opts[] = "+f:hv";
    const struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {"file", required_argument, 0, 'f'},
        {"log-level", required_argument, 0, LOG_LEVEL},
        {0},
    };
    bool ret = false;
    int argc = *argc_p;
    char **argv = *argv_p;
    int cmd = 0;
    u32 log_level = 0;
    for(;;) {
        int long_idx = 0;
        const int c = getopt_long(argc, argv, short_opts, long_opts, &long_idx);
        if(c == -1)
            break;
        switch(c) {
        case '?': break;
        case 'f': {
            const size_t n = strlen(optarg);
            if(SUBS_MAX_PATH <= n) {
                log_err(
                    "database path too long (%zu >= %zu): %s\n",
                    n, SUBS_MAX_PATH, optarg);
                goto end;
            }
            memcpy(s->db_path, optarg, n + 1);
            continue;
        }
        case 'h': cmd = CMD_HELP; continue;
        case 'v': ++log_level; continue;
        case LOG_LEVEL:
            if(!parse_log_level(optarg, &log_level))
                goto end;
            break;
        }
    }
    if(cmd == CMD_HELP)
        usage();
    else {
        db_sqlite_init();
        if(!subs_init(s))
            goto end;
    }
    s->log_level = log_level;
    *argc_p = argc - optind;
    *argv_p = argv + optind;
    ret = true;
end:
    optind = 1;
    return ret;
}

bool subs_destroy(struct subs *s) {
    const int ret = sqlite3_close(s->db);
    if(ret == SQLITE_BUSY) {
        log_err("%s: attempted to close busy database\n", __func__);
        return false;
    }
    assert(ret == SQLITE_OK);
    return true;
}

bool subs_list(const struct subs *s, FILE *f) {
    const char sql[] = "select id, type, yt_id, name from subs";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = write_stmt(stmt, f, format_sub);
    ret = (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
    return ret;
}

bool subs_list_videos(const struct subs *s, FILE *f) {
    const char sql[] =
        "select"
            " videos.id, watched, subs.type,"
            " timestamp, videos.yt_id, subs.yt_id,"
            " replace(title, '\n', '\\n'), sub"
        " from videos"
        " join subs where subs.id == videos.sub"
        " order by timestamp";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    const bool ret = write_stmt(stmt, f, format_video);
    return sqlite3_finalize(stmt) == SQLITE_OK && ret;
}

bool subs_add(
    const struct subs *s,
    enum subs_type type, const char *name, const char *id)
{
    const char sql[] = "insert into subs (type, yt_id, name) values (?, ?, ?)";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(!(
        sqlite3_bind_int(stmt, 1, type) == SQLITE_OK
        && sqlite3_bind_text(stmt, 2, id, -1, SQLITE_STATIC) == SQLITE_OK
        && sqlite3_bind_text(stmt, 3, name, -1, SQLITE_STATIC) == SQLITE_OK
        && step_stmt_once(stmt)
    ))
        goto end;
    if(s->log_level)
        fprintf(
            stderr, "created new subscription: %lld %s %s %s\n",
            (long long)sqlite3_last_insert_rowid(s->db),
            subs_type_name(type), name, id);
    ret = true;
end:
    ret &= sqlite3_finalize(stmt) == SQLITE_OK;
    return ret;
}

bool subs_add_video(
    const struct subs *s,
    i64 sub, i64 timestamp, const char *ext_id, const char *title)
{
    const char sql[] =
        "insert into videos (sub, timestamp, yt_id, title) values (?, ?, ?, ?)";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(!(
        sqlite3_bind_int64(stmt, 1, sub) == SQLITE_OK
        && sqlite3_bind_int64(stmt, 2, timestamp) == SQLITE_OK
        && sqlite3_bind_text(stmt, 3, ext_id, -1, SQLITE_STATIC) == SQLITE_OK
        && sqlite3_bind_text(stmt, 4, title, -1, SQLITE_STATIC) == SQLITE_OK
        && step_stmt_once(stmt)
    ))
        goto end;
    if(s->log_level)
        fprintf(
            stderr, "created new video: %lld %lld %lld %s %s\n",
            (long long)sqlite3_last_insert_rowid(s->db),
            (long long)sub, (long long)timestamp,
            ext_id, title);
    ret = true;
end:
    ret &= sqlite3_finalize(stmt) == SQLITE_OK;
    return ret;
}

bool subs_set_watched(const struct subs *s, i64 id, bool b) {
    const char sql[] = "update videos set watched = ? where id = ?";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    const bool ret =
        sqlite3_bind_int64(stmt, 1, b) == SQLITE_OK
        && sqlite3_bind_int64(stmt, 2, id) == SQLITE_OK
        && step_stmt_once(stmt);
    if(ret && s->log_level)
        fprintf(stderr, "watched video: %lld\n", (long long)id);
    return sqlite3_finalize(stmt) == SQLITE_OK && ret;
}

static bool cmd_watched(struct subs *s, char **argv) {
    bool b = true;
    if(strcmp(*argv, "-r") == 0 || strcmp(*argv, "--remove") == 0)
        b = false, ++argv;
    if(!*argv)
        return log_err("watched: at least one argument required\n"), false;
    do {
        const i64 id = parse_i64(*argv);
        if(id == -1 || !subs_set_watched(s, id, b))
            return false;
    } while(*++argv);
    return true;
}

static bool cmd_update(struct subs *s, int argc, char **argv) {
    enum { DEEP = 1 };
    const char short_opts[] = "h";
    const struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {"deep", no_argument, 0, DEEP},
        {0},
    };
    bool ret = false;
    u32 flags = 0;
    for(;;) {
        int long_idx = 0;
        const int c = getopt_long(argc, argv, short_opts, long_opts, &long_idx);
        if(c == -1)
            break;
        switch(c) {
        case '?': goto end;
        case DEEP: flags |= SUBS_UPDATE_DEEP; break;
        case 'h':
            printf(
"Usage: %s [options] update [options]\n"
"\n"
"Options:\n"
"    -h, --help      This help text.\n"
"    --deep          Fetch all pages when updating.  By default, updates stop\n"
"                    on the first page that does not contain new entries.\n",
                PROG_NAME);
            ret = true;
            goto end;
        }
    }
    struct http_client http = {0};
    http_client_init(&http, 0);
    ret = subs_update(s, &http, flags);
end:
    optind = 1;
    return ret;
}

bool subs_exec(struct subs *s, int argc, char **argv) {
    if(!s->db)
        return true;
    if(!argc)
        return subs_list(s, stdout);
    if(strcmp(*argv, "ls") == 0)
        return check_argc("ls", --argc, 0)
            && subs_list(s, stdout);
    if(strcmp(*argv, "videos") == 0)
        return check_argc("videos", --argc, 0)
            && subs_list_videos(s, stdout);
    if(strcmp(*argv, "add") == 0) {
        --argc, ++argv;
        enum subs_type t = 0;
        return check_argc("add", argc, 3)
            && (t = subs_parse_type(argv[0]))
            && subs_add(s, t, argv[1], argv[2]);
    }
    if(strcmp(*argv, "watched") == 0)
        return cmd_watched(s, ++argv);
    if(strcmp(*argv, "update") == 0)
        return cmd_update(s, argc, argv);
    log_err("invalid command: %s\n", *argv);
    return false;
}
