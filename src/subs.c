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
"    db SQL          Execute database query\n"
"    ls [OPTIONS]    List subscriptions.\n"
"    videos          List videos.\n"
"    add TYPE NAME ID\n"
"                    Add a subscription.\n"
"    rm ID           Remove a subscription.\n"
"    tag add NAME    Create a tag.\n"
"    tag subs|videos TAG_ID ID...\n"
"                    Add tag TAG_ID to subscriptions/videos.\n"
"    watched [-r|--remove] ID\n"
"                    Mark videos as watched (`-r` to unmark)\n"
"    update [OPTIONS]\n"
"                    Fetch new videos from subscriptions.\n"
"    tui             Start curses terminal interface.\n",
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
        " %lld" // duration
        " %s"   // ext_id
        " %s"   // name
        " %s"   // title
        "\n",
        sqlite3_column_int(stmt, 0),
        sqlite3_column_int(stmt, 1) ? "w" : "0",
        subs_type_name(sqlite3_column_int(stmt, 2)),
        sqlite3_column_int64(stmt, 3),
        sqlite3_column_int64(stmt, 4),
        sqlite3_column_text(stmt, 5),
        sqlite3_column_text(stmt, 6),
        sqlite3_column_text(stmt, 7));
}

static void format_tag(sqlite3_stmt *stmt, FILE *f) {
    fprintf(
        f, "%d %s\n",
        sqlite3_column_int(stmt, 0),
        sqlite3_column_text(stmt, 1));
}

static bool exec_simple_query(sqlite3 *db, const char *sql, int len, i64 arg) {
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, len, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(!(
        sqlite3_bind_int64(stmt, 1, arg) == SQLITE_OK
        && step_stmt_once(stmt)
    ))
        goto end;
    ret = true;
end:
    ret &= sqlite3_finalize(stmt) == SQLITE_OK;
    return ret;
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

sqlite3 *subs_new_db_connection(const struct subs *s) {
    sqlite3 *ret = NULL;
    if(sqlite3_open(s->db_path, &ret) != SQLITE_OK)
        return NULL;
    return ret;
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
    lua_State *const L = subs_lua_init(s);
    if(!L)
        goto e0;
    s->db = db;
    s->L = L;
    if(db_path != s->db_path)
        strcpy(s->db_path, db_path);
    if(!s->url)
        s->url = "localhost:5279";
    return true;
e0:
    if(sqlite3_close(db) != SQLITE_OK)
        LOG_ERR("failed to close sqlite database\n", 0);
    return false;
}

static bool init_from_env(struct subs *s) {
    if(!parse_log_level(getenv("SUBS_LOG_LEVEL"), &s->log_level))
        return false;
    return true;
}

static const char *find_db(const char *path, char v[static SUBS_MAX_PATH]) {
    if(*path)
        return path;
    const char *const base = "/subs/db";
    const char *const home_base = "/.local/share/subs/db";
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
    return "file:subs?mode=memory&cache=shared";
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
        if(!db_sqlite_init())
            goto end;
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
    if(s->L)
        lua_close(s->L);
    const int ret = sqlite3_close(s->db);
    if(ret == SQLITE_BUSY) {
        log_err("%s: attempted to close busy database\n", __func__);
        return false;
    }
    assert(ret == SQLITE_OK);
    return true;
}

bool subs_list(const struct subs *s, i64 tag, FILE *f) {
    struct buffer b = {0};
    buffer_append_str(&b,
        "select subs.id, subs.type, subs.ext_id, subs.name from subs");
    if(tag)
        --b.n, buffer_append_str(&b,
            " join subs_tags on subs.id == subs_tags.sub"
            " where subs_tags.tag == ?");
    --b.n, buffer_append_str(&b, " order by subs.id");
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, b.p, (int)b.n, 0, &stmt, NULL);
    bool ret = false;
    if(!stmt)
        goto end;
    if(tag && sqlite3_bind_int64(stmt, 1, tag) != SQLITE_OK)
        goto end;
    if(!write_stmt(stmt, f, format_sub))
        goto end;
    ret = true;
end:
    free(b.p);
    ret = (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
    return ret;
}

bool subs_list_videos(const struct subs *s, i64 tag, FILE *f) {
    struct buffer b = {0};
    buffer_append_str(&b,
        "select"
            " videos.id, watched, subs.type,"
            " timestamp, duration_seconds,"
            " videos.ext_id, subs.ext_id,"
            " replace(title, '\n', '\\n'), sub"
        " from videos"
        " join subs on subs.id == videos.sub");
    if(tag)
        --b.n, buffer_append_str(&b,
            ", videos_tags on videos.id == videos_tags.video"
            " where videos_tags.tag == ?");
    --b.n, buffer_append_str(&b, " order by timestamp");
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, b.p, (int)b.n, 0, &stmt, NULL);
    bool ret = false;
    if(!stmt)
        goto end;
    if(tag && sqlite3_bind_int64(stmt, 1, tag) != SQLITE_OK)
        goto end;
    if(!write_stmt(stmt, f, format_video))
        goto end;
    ret = true;
end:
    free(b.p);
    ret = sqlite3_finalize(stmt) == SQLITE_OK && ret;
    return ret;
}

bool subs_list_tags(const struct subs *s, FILE *f) {
    const char sql[] = "select id, name from tags";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = write_stmt(stmt, f, format_tag);
    ret = (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
    return ret;
}

bool subs_add(
    const struct subs *s,
    enum subs_type type, const char *name, const char *id)
{
    const char sql[] = "insert into subs (type, ext_id, name) values (?, ?, ?)";
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

bool subs_rm(const struct subs *s, i64 id) {
    const char sql_videos_tags[] =
        "delete from videos_tags"
        " where video in (select id from videos where sub == ?)";
    const char sql_tags[] = "delete from subs_tags where sub == ?";
    const char sql_videos[] = "delete from videos where sub == ?";
    const char sql[] = "delete from subs where id == ?";
#define Q(x) x, sizeof(x) - 1
    return exec_simple_query(s->db, Q(sql_videos_tags), id)
        && exec_simple_query(s->db, Q(sql_tags), id)
        && exec_simple_query(s->db, Q(sql_videos), id)
        && exec_simple_query(s->db, Q(sql), id);
#undef Q
}

bool subs_add_video(
    const struct subs *s,
    i64 sub, i64 timestamp, i64 duration_seconds,
    const char *ext_id, const char *title)
{
    const char sql[] =
        "insert into videos (sub, timestamp, duration_seconds, ext_id, title)"
        " values (?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(!(
        sqlite3_bind_int64(stmt, 1, sub) == SQLITE_OK
        && sqlite3_bind_int64(stmt, 2, timestamp) == SQLITE_OK
        && sqlite3_bind_int64(stmt, 3, duration_seconds) == SQLITE_OK
        && sqlite3_bind_text(stmt, 4, ext_id, -1, SQLITE_STATIC) == SQLITE_OK
        && sqlite3_bind_text(stmt, 5, title, -1, SQLITE_STATIC) == SQLITE_OK
        && step_stmt_once(stmt)
    ))
        goto end;
    if(s->log_level)
        fprintf(
            stderr, "created new video: %lld %lld %lld %lld%s %s\n",
            (long long)sqlite3_last_insert_rowid(s->db),
            (long long)sub, (long long)timestamp, (long long)duration_seconds,
            ext_id, title);
    ret = true;
end:
    ret &= sqlite3_finalize(stmt) == SQLITE_OK;
    return ret;
}

bool subs_add_tag(const struct subs *s, const char *name) {
    const char sql[] = "insert into tags (name) values (?)";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret =
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC) == SQLITE_OK
        && step_stmt_once(stmt);
    ret = sqlite3_finalize(stmt) == SQLITE_OK && ret;
    return ret;
}

static bool tag_common(sqlite3 *db, const char *sql, int len, i64 tag, i64 id);

bool subs_tag_sub(const struct subs *s, i64 tag, i64 id) {
    const char sql[] = "insert into subs_tags (tag, sub) values (?, ?)";
    return tag_common(s->db, sql, sizeof(sql) - 1, tag, id);
}

bool subs_tag_video(const struct subs *s, i64 tag, i64 id) {
    const char sql[] = "insert into videos_tags (tag, video) values (?, ?)";
    return tag_common(s->db, sql, sizeof(sql) - 1, tag, id);
}

bool tag_common(sqlite3 *db, const char *sql, int len, i64 tag, i64 id) {
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, len, 0, &stmt, NULL);
    if(!stmt)
        return false;
    const bool ret =
        sqlite3_bind_int64(stmt, 1, tag) == SQLITE_OK
        && sqlite3_bind_int64(stmt, 2, id) == SQLITE_OK
        && step_stmt_once(stmt);
    return sqlite3_finalize(stmt) == SQLITE_OK && ret;
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

static bool cmd_db(struct subs *s, int argc, char **argv) {
    return check_argc("db", argc, 1)
        && sqlite3_exec(s->db, *argv, db_print_row, stdout, NULL) == SQLITE_OK;
}

static bool cmd_list(struct subs *s, int argc, char **argv) {
    enum { TAG = 1 };
    const char short_opts[] = "h";
    const struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {"tag", required_argument, 0, TAG},
        {0},
    };
    bool ret = false;
    i64 tag = 0;
    for(;;) {
        int long_idx = 0;
        const int c = getopt_long(argc, argv, short_opts, long_opts, &long_idx);
        if(c == -1)
            break;
        switch(c) {
        case '?': goto end;
        case TAG:
            if((tag = parse_i64(optarg)) == -1)
                goto end;
            break;
        case 'h':
            printf(
"Usage: %s [options] ls [options]\n"
"\n"
"Options:\n"
"    -h, --help      This help text.\n"
"    --tag TAG       Filter by tag.\n",
                PROG_NAME);
            ret = true;
            goto end;
        }
    }
    ret = subs_list(s, tag, stdout);
end:
    optind = 1;
    return ret;
}

static bool cmd_list_videos(struct subs *s, int argc, char **argv) {
    enum { TAG = 1 };
    const char short_opts[] = "h";
    const struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {"tag", required_argument, 0, TAG},
        {0},
    };
    bool ret = false;
    i64 tag = 0;
    for(;;) {
        int long_idx = 0;
        const int c = getopt_long(argc, argv, short_opts, long_opts, &long_idx);
        if(c == -1)
            break;
        switch(c) {
        case '?': goto end;
        case TAG:
            if((tag = parse_i64(optarg)) == -1)
                goto end;
            break;
        case 'h':
            printf(
"Usage: %s [options] videos [options]\n"
"\n"
"Options:\n"
"    -h, --help      This help text.\n"
"    --tag TAG       Filter by tag.\n",
                PROG_NAME);
            ret = true;
            goto end;
        }
    }
    ret = subs_list_videos(s, tag, stdout);
end:
    optind = 1;
    return ret;
}

static bool cmd_tag(struct subs *s, int argc, char **argv) {
    bool (*f)(const struct subs*, i64, i64) = NULL;
    if(!*argv)
        return subs_list_tags(s, stdout);
    else if(strcmp(*argv, "add") == 0)
        return check_argc("tag add", argc, 2)
            && subs_add_tag(s, *++argv);
    else if(strcmp(*argv, "subs") == 0)
        f = subs_tag_sub;
    else if(strcmp(*argv, "videos") == 0)
        f = subs_tag_video;
    else
        return log_err("invalid tag destination type: %s\n", *argv), false;
    if(!*++argv)
        return log_err("missing tag argument\n"), false;
    const i64 tag = parse_i64(*argv);
    if(tag == -1)
        return false;
    while(*++argv) {
        const i64 id = parse_i64(*argv);
        if(id == -1 || !f(s, tag, id))
            return false;
    }
    return true;
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
    enum { DEEP = 1, DELAY = 2, SINCE = 3 };
    const char short_opts[] = "h";
    const struct option long_opts[] = {
        {"help", no_argument, 0, 'h'},
        {"deep", no_argument, 0, DEEP},
        {"delay", required_argument, 0, DELAY},
        {"since", required_argument, 0, SINCE},
        {0},
    };
    bool ret = false;
    u32 flags = 0;
    int delay = 0, since = 0;
    for(;;) {
        int long_idx = 0;
        const int c = getopt_long(argc, argv, short_opts, long_opts, &long_idx);
        if(c == -1)
            break;
        switch(c) {
        case '?': goto end;
        case DEEP: flags |= SUBS_UPDATE_DEEP; break;
        case DELAY:
            if((delay = parse_int(optarg)) == -1)
                return false;
            break;
        case SINCE:
            if((since = parse_int(optarg)) == -1)
                return false;
            break;
        case 'h':
            printf(
"Usage: %s [options] update [options]\n"
"\n"
"Options:\n"
"    -h, --help      This help text.\n"
"    --deep          Fetch all pages when updating.  By default, updates stop\n"
"                    on the first page that does not contain new entries.\n"
"    --delay N       Stop for N seconds between each update.\n"
"    --since TIMESTAMP\n"
"                    Only update subscriptions which have not been updated.\n"
"                    since TIMESTAMP (Unix timestamp)\n"
,
                PROG_NAME);
            ret = true;
            goto end;
        }
    }
    struct http_client http = {0};
    http_client_init(&http, 0);
    ret = subs_update(s, &http, flags, delay, since);
end:
    optind = 1;
    return ret;
}

bool subs_exec(struct subs *s, int argc, char **argv) {
    if(!s->db)
        return true;
    if(!argc)
        return subs_list(s, 0, stdout);
    if(strcmp(*argv, "db") == 0)
        return cmd_db(s, --argc, ++argv);
    if(strcmp(*argv, "ls") == 0)
        return cmd_list(s, argc, argv);
    if(strcmp(*argv, "videos") == 0)
        return cmd_list_videos(s, argc, argv);
    if(strcmp(*argv, "add") == 0) {
        --argc, ++argv;
        enum subs_type t = 0;
        return check_argc("add", argc, 3)
            && (t = subs_parse_type(argv[0]))
            && subs_add(s, t, argv[1], argv[2]);
    }
    if(strcmp(*argv, "rm") == 0) {
        i64 id = 0;
        return check_argc("rm", --argc, 1)
            && ((id = parse_i64(argv[1])) != -1)
            && subs_rm(s, id);
    }
    if(strcmp(*argv, "tag") == 0)
        return cmd_tag(s, --argc, ++argv);
    if(strcmp(*argv, "watched") == 0)
        return cmd_watched(s, ++argv);
    if(strcmp(*argv, "update") == 0)
        return cmd_update(s, argc, argv);
    if(argc && strcmp(argv[0], "lua") == 0)
        return check_argc("lua", --argc, 1)
            && subs_lua(s, *++argv);
    if(strcmp(*argv, "tui") == 0)
        return check_argc("tui", --argc, 0)
            && subs_start_tui(s);
    log_err("invalid command: %s\n", *argv);
    return false;
}
