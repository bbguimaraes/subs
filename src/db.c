#include "db.h"

#include "log.h"

static void sqlite_log(void *data, int code, const char *msg) {
    (void)data;
    log_err("sqlite: error code %d: %s\n", code, msg);
}

bool db_sqlite_init(void) {
    if(!sqlite3_threadsafe()) {
        LOG_ERR("cannot use sqlite3 compiled without threading support\n", 0);
        return false;
    }
    sqlite3_config(SQLITE_CONFIG_LOG, sqlite_log, NULL);
    sqlite3_config(SQLITE_CONFIG_MULTITHREAD, NULL);
    return true;
}

sqlite3 *db_init(const char *path) {
    sqlite3 *ret = NULL;
    if(sqlite3_open(path, &ret) != SQLITE_OK)
        return NULL;
    const char *const tables =
        "pragma foreign_keys = on;"
        "create table if not exists subs ("
            "id integer primary key autoincrement not null,"
            " type unsigned integer not null,"
            " ext_id text not null,"
            " name text null,"
            " disabled boolean default(0),"
            " last_update integer not null default(0),"
            " last_video integer not null default(0),"
            " constraint unique_type_ext_id unique(type, ext_id)"
        ");"
        " create table if not exists videos ("
            "id integer primary key autoincrement not null,"
            " sub integer not null,"
            " ext_id text not null,"
            " title text not null,"
            " timestamp integer default(0),"
            " watched boolean not null default(0),"
            " foreign key(sub) references subs(id)"
            " constraint unique_sub_ext_id unique(sub, ext_id)"
        ");"
        " create unique index if not exists videos_sub_ext_id"
            " on videos (sub, ext_id);"
        " create table if not exists tags ("
            "id integer primary key autoincrement not null,"
            " name text not null"
        ");"
        " create table if not exists subs_tags ("
            "id integer primary key autoincrement not null,"
            " sub integer not null,"
            " tag integer not null,"
            " foreign key(sub) references subs(id),"
            " foreign key(tag) references tags(id),"
            " constraint unique_subs_tags_sub_tag unique(sub, tag)"
        ");"
        " create table if not exists videos_tags ("
            "id integer primary key autoincrement not null,"
            " video integer not null,"
            " tag integer not null,"
            " foreign key(video) references videos(id),"
            " foreign key(tag) references tags(id)"
            " constraint unique_videos_tags_video_tag unique(video, tag)"
        ");"
        " create index if not exists subs_tags_sub"
            " on subs_tags (sub);"
        " create index if not exists subs_tags_tag"
            " on subs_tags (tag);"
        " create unique index if not exists subs_tags_sub_tag"
            " on subs_tags (sub, tag);"
        " create index if not exists videos_tags_video"
            " on videos_tags (video);"
        " create index if not exists videos_tags_tag"
            " on videos_tags (tag);"
        " create unique index if not exists videos_tags_video_tag"
            " on videos_tags (video, tag);";
    if(sqlite3_exec(ret, tables, NULL, NULL, NULL) != SQLITE_OK) {
        if(sqlite3_close(ret) != SQLITE_OK)
            LOG_ERR("failed to close sqlite database\n", 0);
        return NULL;
    }
    return ret;
}

int exists_query(sqlite3 *db, const char *sql, int len, const int *param) {
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, len, 0, &stmt, NULL);
    if(!stmt)
        return -1;
    int ret = -1;
    if(param && sqlite3_bind_int(stmt, 1, *param) != SQLITE_OK)
        goto end;
    for(;;)
        switch(sqlite3_step(stmt)) {
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: ret = 0; goto end;
        case SQLITE_ROW: ret = 1; goto end;
        default: ret = -1; goto end;
        }
end:
    if(sqlite3_finalize(stmt) != SQLITE_OK)
        return -1;
    return ret;
}
