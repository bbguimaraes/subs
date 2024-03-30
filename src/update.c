#include "subs.h"

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "buffer.h"
#include "db.h"
#include "def.h"
#include "http.h"
#include "log.h"
#include "update.h"

static void build_query_common(struct buffer *b, int since) {
    --b->n, buffer_append_str(b, " from subs where disabled == 0");
    if(since)
        --b->n, buffer_append_str(b, " and last_update < ?");
}

static bool count_subs(sqlite3 *db, struct buffer *b, int since, size_t *p) {
    buffer_append_str(b, "select count(*)");
    build_query_common(b, since);
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, b->p, (int)b->n, 0, &stmt, NULL);
    if(!stmt)
        return false;
    if(since)
        sqlite3_bind_int(stmt, 1, since);
    for(;;)
        switch(sqlite3_step(stmt)) {
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: assert(false);
        case SQLITE_ROW:
            *p = (size_t)sqlite3_column_int(stmt, 0);
            return sqlite3_finalize(stmt) == SQLITE_OK;
        }
}

static bool count_videos(sqlite3 *db, size_t *p) {
    const char sql[] = "select count(*) from videos";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    for(;;)
        switch(sqlite3_step(stmt)) {
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: assert(false);
        case SQLITE_ROW:
            *p = (size_t)sqlite3_column_int(stmt, 0);
            return sqlite3_finalize(stmt) == SQLITE_OK;
        }
}

static int has_youtube(sqlite3 *db) {
    const char sql[] =
        "select 1 from videos"
        " join subs on videos.sub == subs.id"
        " where subs.type == ?"
        " limit 1";
    const int param = SUBS_YOUTUBE;
    return exists_query(db, sql, sizeof(sql) - 1, &param);
}

static bool set_last_update(sqlite3 *db, int id) {
    time_t t = time(NULL);
    const char sql[] = "update subs set last_update == ? where id == ?";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    sqlite3_bind_int(stmt, 1, (int)t);
    sqlite3_bind_int(stmt, 2, id);
    bool ret = false;
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_BUSY:
        case SQLITE_ROW: continue;
        case SQLITE_DONE: ret = true; /* fallthrough */
        default: goto end;
        }
    }
end:
    ret = sqlite3_finalize(stmt) == SQLITE_OK && ret;
    return ret;
}

static bool report(sqlite3 *db, size_t initial_count) {
    size_t final_count = 0;
    if(!count_videos(db, &final_count))
        return false;
    assert(final_count >= initial_count);
    fprintf(
        stderr, "added %zu new video(s) in total\n",
        final_count - initial_count);
    return true;
}

bool subs_update(
    const struct subs *s, const struct http_client *http, u32 flags,
    int delay, int since)
{
    const bool verbose = s->log_level;
    sqlite3 *const db = s->db;
    bool ret = false;
    size_t subs_count = 0, videos_count = 0;
    struct buffer sql = {0};
    if(verbose) {
        if(!count_subs(db, &sql, since, &subs_count))
            goto e0;
        if(!count_videos(db, &videos_count))
            goto e0;
    }
    struct update_youtube youtube = {0};
    const int needs_youtube = has_youtube(db);
    switch(needs_youtube) {
    case -1:
        goto e1;
    case 1:
        if(!update_youtube_init(&youtube))
            goto e1;
    }
    sql.n = 0;
    buffer_append_str(&sql, "select id, ext_id, type, name");
    build_query_common(&sql, since);
    --sql.n, buffer_append_str(&sql, " order by id");
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql.p, (int)sql.n, 0, &stmt, NULL);
    if(!stmt)
        goto e1;
    if(since)
        sqlite3_bind_int(stmt, 1, since);
    struct buffer b = {0};
    size_t i = 0;
    goto after_delay;
    for(;; ++i) {
        if(delay)
            sleep((unsigned)delay);
after_delay:
        switch(sqlite3_step(stmt)) {
        case SQLITE_BUSY: continue;
        case SQLITE_ROW: break;
        case SQLITE_DONE: ret = true; /* fallthrough */
        default: goto e2;
        }
        const int id = sqlite3_column_int(stmt, 0);
        if(!id)
            continue;
        const char *const ext_id = (const char*)sqlite3_column_text(stmt, 1);
        const int type = sqlite3_column_int(stmt, 2);
        if(verbose)
            fprintf(
                stderr, "[%zd/%zd] processing %d %s\n",
                i, subs_count, id, sqlite3_column_text(stmt, 3));
        switch(type) {
        case SUBS_LBRY:
            if(!update_lbry(s, http, &b, flags, id, ext_id))
                goto e2;
            break;
        case SUBS_YOUTUBE:
            if(!update_youtube(s, &youtube, &b, flags, id, ext_id))
                goto e2;
            break;
        default:
            log_err("%s: unsupported type: %d\n", __func__, type);
            goto e2;
        }
        if(!set_last_update(db, id))
            goto e2;
        b.n = 0;
    }
e2:
    ret = sqlite3_finalize(stmt) == SQLITE_OK && ret;
    free(b.p);
e1:
    if(needs_youtube)
        ret = update_youtube_destroy(&youtube) && ret;
e0:
    free(sql.p);
    if(verbose && !report(db, videos_count))
        ret = false;
    return ret;
}
