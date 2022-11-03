#include "db.h"

#include "log.h"

static void sqlite_log(void *data, int code, const char *msg) {
    (void)data;
    log_err("sqlite: error code %d: %s\n", code, msg);
}

void db_sqlite_init(void) {
    sqlite3_config(SQLITE_CONFIG_LOG, sqlite_log, NULL);
}

sqlite3 *db_init(const char *path) {
    sqlite3 *ret = NULL;
    if(sqlite3_open(path, &ret) != SQLITE_OK)
        return NULL;
    const char *const tables =
        "create table if not exists subs ("
            "id integer primary key autoincrement not null,"
            " type unsigned integer not null,"
            // TODO rename to ext_id
            " yt_id text not null,"
            " name text null,"
            " disabled boolean default(0),"
            " last_update integer not null default(0),"
            " last_video integer not null default(0),"
            " constraint unique_type_yt_id unique(type, yt_id)"
        ");"
        " create table if not exists videos ("
            "id integer primary key autoincrement not null,"
            " sub integer not null,"
            // TODO rename to ext_id
            " yt_id text not null,"
            " title text not null,"
            " timestamp integer default(0),"
            " watched boolean not null default(0),"
            " foreign key(sub) references subs(id)"
            " constraint unique_sub_yt_id unique(sub, yt_id)"
        ");"
        " create unique index if not exists videos_sub_yt_id"
            " on videos (sub, yt_id);"
        " create table if not exists tags ("
            "id integer primary key autoincrement not null,"
            " name text not null"
        ");"
        " create table if not exists subs_tags ("
            "id integer primary key autoincrement not null,"
            " sub integer not null,"
            " tag integer not null,"
            " foreign key(sub) references subs(id),"
            " foreign key(tag) references tags(id)"
        ");"
        " create table if not exists videos_tags ("
            "id integer primary key autoincrement not null,"
            " video integer not null,"
            " tag integer not null,"
            " foreign key(video) references videos(id),"
            " foreign key(tag) references tags(id)"
        ");"
        " create index if not exists subs_tags_sub"
            " on subs_tags (sub);"
        " create index if not exists subs_tags_tag"
            " on subs_tags (tag);"
        " create index if not exists videos_tags_video"
            " on videos_tags (video);"
        " create index if not exists videos_tags_tag"
            " on videos_tags (tag);";
    if(sqlite3_exec(ret, tables, NULL, NULL, NULL) != SQLITE_OK) {
        if(sqlite3_close(ret) != SQLITE_OK)
            LOG_ERR("failed to close sqlite database\n", 0);
        return false;
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
