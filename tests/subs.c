#include <stdbool.h>

#include "db.h"
#include "http_fake.h"
#include "log.h"
#include "os.h"
#include "subs.h"
#include "util.h"

#include "common.h"

#define JSON(...) #__VA_ARGS__

const char *PROG_NAME = NULL;
const char *CMD_NAME = NULL;

static bool table_exists(sqlite3 *db, const char *name) {
    const char sql[] =
        "select count(*) from sqlite_master"
        " where  type == 'table' and name == ?";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC) != SQLITE_OK)
        goto end;
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: break;
        case SQLITE_BUSY: continue;
        default: goto end;
        }
        assert(sqlite3_column_count(stmt) == 1);
        if(!(ret = sqlite3_column_int(stmt, 0) == 1))
            log_err("table %s not found\n", name);
        break;
    }
end:
    ret &= sqlite3_finalize(stmt) == SQLITE_OK;
    return ret;
}

static bool init_db(void) {
    struct subs s = {.db_path = ":memory:"};
    if(!subs_init(&s))
        return false;
    const bool ret = table_exists(s.db, "subs")
        && table_exists(s.db, "videos")
        && table_exists(s.db, "tags")
        && table_exists(s.db, "subs_tags")
        && table_exists(s.db, "videos_tags");
    return subs_destroy(&s) && ret;
}

static bool add(void) {
    struct subs s = {.db_path = ":memory:"};
    bool ret = false;
    if(!(
        subs_init(&s)
        && subs_add(&s, SUBS_LBRY, "name0", "id0")
        && subs_add(&s, SUBS_LBRY, "name1", "id1")
        && subs_add(&s, SUBS_YOUTUBE, "name2", "id0")
    ))
        goto end;
    FILE *const tmp = tmpfile();
    if(!tmp) {
        LOG_ERRNO("tmpfile", 0);
        goto end;
    }
    if(!subs_list(&s, 0, tmp))
        goto end;
    const char expected[] =
        "1 lbry id0 name0\n"
        "2 lbry id1 name1\n"
        "3 youtube id0 name2\n";
    if(!CHECK_FILE(tmp, expected))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return ret;
}

static bool add_video(void) {
    struct subs s = {.db_path = ":memory:"};
    bool ret = false;
    if(!(
        subs_init(&s)
        && subs_add(&s, SUBS_LBRY, "name0", "id0")
        && subs_add(&s, SUBS_YOUTUBE, "name1", "id1")
        && subs_add_video(&s, 1, 1630796966, "claim_id0", "v0")
        && subs_add_video(&s, 1, 1630795115, "claim_id1", "v1")
        && subs_add_video(&s, 2, 1630795015, "claim_id2", "v2")
    ))
        goto end;
    FILE *const tmp = tmpfile();
    if(!tmp) {
        LOG_ERRNO("tmpfile", 0);
        goto end;
    }
    if(!subs_list_videos(&s, 0, tmp))
        goto end;
    const char expected[] =
        "3 0 youtube 1630795015 claim_id2 id1 v2\n"
        "2 0 lbry 1630795115 claim_id1 id0 v1\n"
        "1 0 lbry 1630796966 claim_id0 id0 v0\n";
    if(!CHECK_FILE(tmp, expected))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return ret;
}

static bool tag(void) {
    struct subs s = {.db_path = ":memory:"};
    bool ret = false;
    if(!(
        subs_init(&s)
        && subs_add_tag(&s, "tag0")
        && subs_add_tag(&s, "tag1")
    ))
        goto end;
    FILE *const tmp = tmpfile();
    if(!tmp) {
        LOG_ERRNO("tmpfile", 0);
        goto end;
    }
    if(!subs_list_tags(&s, tmp))
        goto end;
    const char expected0[] =
        "1 tag0\n"
        "2 tag1\n";
    if(!CHECK_FILE(tmp, expected0))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return ret;
}

static bool tag_subs(void) {
    struct subs s = {.db_path = ":memory:"};
    bool ret = false;
    if(!(
        subs_init(&s)
        && subs_add_tag(&s, "tag0")
        && subs_add_tag(&s, "tag1")
        && subs_add(&s, SUBS_LBRY, "name0", "id0")
        && subs_add(&s, SUBS_LBRY, "name1", "id1")
        && subs_add(&s, SUBS_LBRY, "name2", "id2")
        && subs_add(&s, SUBS_LBRY, "name3", "id3")
        && subs_tag_sub(&s, 1, 1)
        && subs_tag_sub(&s, 2, 2)
        && subs_tag_sub(&s, 1, 3)
    ))
        goto end;
    FILE *const tmp = tmpfile();
    if(!tmp) {
        LOG_ERRNO("tmpfile", 0);
        goto end;
    }
    if(!subs_list(&s, 1, tmp))
        goto end;
    const char expected0[] =
        "1 lbry id0 name0\n"
        "3 lbry id2 name2\n";
    if(!CHECK_FILE(tmp, expected0))
        goto end;
    if(fseek(tmp, SEEK_SET, 0)) {
        LOG_ERRNO("fseek", 0);
        goto end;
    }
    if(!subs_list(&s, 2, tmp))
        goto end;
    const char expected1[] =
        "2 lbry id1 name1\n";
    if(!CHECK_FILE(tmp, expected1))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return ret;
}

static bool tag_videos(void) {
    struct subs s = {.db_path = ":memory:"};
    bool ret = false;
    if(!(
        subs_init(&s)
        && subs_add(&s, SUBS_LBRY, "name0", "id0")
        && subs_add(&s, SUBS_YOUTUBE, "name1", "id1")
        && subs_add_video(&s, 1, 1630796966, "claim_id0", "v0")
        && subs_add_video(&s, 1, 1630795115, "claim_id1", "v1")
        && subs_add_video(&s, 2, 1630795015, "claim_id2", "v2")
        && subs_add_tag(&s, "tag0")
        && subs_add_tag(&s, "tag1")
        && subs_tag_video(&s, 1, 1)
        && subs_tag_video(&s, 2, 2)
    ))
        goto end;
    FILE *const tmp = tmpfile();
    if(!tmp) {
        LOG_ERRNO("tmpfile", 0);
        goto end;
    }
    if(!subs_list_videos(&s, 1, tmp))
        goto end;
    const char expected0[] = "1 0 lbry 1630796966 claim_id0 id0 v0\n";
    if(!CHECK_FILE(tmp, expected0))
        goto end;
    if(fseek(tmp, SEEK_SET, 0)) {
        LOG_ERRNO("fseek", 0);
        goto end;
    }
    if(!subs_list_videos(&s, 2, tmp))
        goto end;
    const char expected1[] = "2 0 lbry 1630795115 claim_id1 id0 v1\n";
    if(!CHECK_FILE(tmp, expected1))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return ret;
}

static bool watched(void) {
    struct subs s = {.db_path = ":memory:"};
    bool ret = false;
    if(!(
        subs_init(&s)
        && subs_add(&s, SUBS_LBRY, "name0", "id0")
        && subs_add(&s, SUBS_YOUTUBE, "name1", "id1")
        && subs_add_video(&s, 1, 1630796966, "claim_id0", "v0")
        && subs_add_video(&s, 1, 1630795115, "claim_id1", "v1")
        && subs_add_video(&s, 2, 1630795015, "claim_id2", "v2")
        && subs_set_watched(&s, 2, true)
        && subs_set_watched(&s, 2, false)
        && subs_set_watched(&s, 2, true)
        && subs_set_watched(&s, 3, true)
    ))
        goto end;
    FILE *const tmp = tmpfile();
    if(!tmp) {
        LOG_ERRNO("tmpfile", 0);
        goto end;
    }
    if(!subs_list_videos(&s, 0, tmp))
        goto end;
    const char expected[] =
        "3 w youtube 1630795015 claim_id2 id1 v2\n"
        "2 w lbry 1630795115 claim_id1 id0 v1\n"
        "1 0 lbry 1630796966 claim_id0 id0 v0\n";
    if(!CHECK_FILE(tmp, expected))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return ret;
}

static bool update(void) {
    const struct http_fake_response responses[] = {{
        .url = "/",
        .method = HTTP_POST,
        .post_data = "{"
            JSON("method":"claim_search",)
            JSON("params":{)
                JSON("channel":"id0","order_by":["release_time"],"page":1)
            JSON(})
        "}",
        .data = JSON({
            "result": {
                "items": [{
                    "claim_id": "claim_id6",
                    "value": {"title": "v6", "release_time": "1630795115"},
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id4",
                    "value": {"title": "v4", "release_time": "1630796966"},
                    "value_type": "stream"
                }],
                "page": 1,
                "page_size": 20,
                "total_items": 3,
                "total_pages": 1
            }
        }),
    }, {
        .url = "/",
        .method = HTTP_POST,
        .post_data = "{"
            JSON("method":"claim_search",)
            JSON("params":) "{"
                JSON("channel":"id1","order_by":["release_time"],"page":1)
            "}"
        "}",
        .data = JSON({
            "result": {
                "items": [{
                    "claim_id": "claim_id5",
                    "value": {"title": "v5", "release_time": "1630795015"},
                    "value_type": "stream"
                }],
                "page": 1,
                "page_size": 20,
                "total_items": 1,
                "total_pages": 1
            }
        }),
    }};
    const struct http_fake_server server = {
        .n = ARRAY_SIZE(responses),
        .responses = responses,
    };
    struct http_client http = http_client_fake_init(&server);
    struct subs s = {.db_path = ":memory:"};
    bool ret = false;
    if(!(
        subs_init(&s)
        && subs_add(&s, SUBS_LBRY, "name0", "id0")
        && subs_add(&s, SUBS_LBRY, "name1", "id1")
        /*TODO&& subs_add(&s, SUBS_YOUTUBE, "name2", "id0")*/
        && subs_update(&s, &http, 0, 0, 0)
    ))
        goto end;
    FILE *const tmp = tmpfile();
    if(!tmp) {
        LOG_ERRNO("tmpfile", 0);
        goto end;
    }
    if(!subs_list_videos(&s, 0, tmp))
        goto end;
    const char expected[] =
        "3 0 lbry 1630795015 claim_id5 id1 v5\n"
        "2 0 lbry 1630795115 claim_id6 id0 v6\n"
        "1 0 lbry 1630796966 claim_id4 id0 v4\n";
    if(!CHECK_FILE(tmp, expected))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return ret;
}

static bool update_pages(void) {
    const struct http_fake_response responses[] = {{
        .url = "/",
        .method = HTTP_POST,
        .post_data = "{"
            JSON("method":"claim_search",)
            JSON("params":) "{"
                JSON("channel":"id0","order_by":["release_time"],"page":1)
            "}"
        "}",
        .data = JSON({
            "result": {
                "items": [{
                    "claim_id": "claim_id7",
                    "value": {"title": "v7", "release_time": "1630796966"},
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id6",
                    "value": {"title": "v6", "release_time": "1630795115"},
                    "value_type": "stream"
                }],
                "page": 1,
                "page_size": 2,
                "total_items": 4,
                "total_pages": 2
            }
        }),
    }, {
        .url = "/",
        .method = HTTP_POST,
        .post_data = "{"
            JSON("method":"claim_search",)
            JSON("params":) "{"
                JSON("channel":"id0","order_by":["release_time"],"page":2)
            "}"
        "}",
        .data = JSON({
            "result": {
                "items": [{
                    "claim_id": "claim_id5",
                    "value": {"title": "v5", "release_time": "1630795015"},
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id4",
                    "value": {"title": "v4", "release_time": "1630794985"},
                    "value_type": "stream"
                }],
                "page": 2,
                "page_size": 2,
                "total_items": 4,
                "total_pages": 2
            }
        }),
    }};
    const struct http_fake_server server = {
        .n = ARRAY_SIZE(responses),
        .responses = responses,
    };
    struct http_client http = http_client_fake_init(&server);
    struct subs s = {.db_path = ":memory:"};
    bool ret = false;
    if(!(
        subs_init(&s)
        && subs_add(&s, SUBS_LBRY, "name0", "id0")
        && subs_update(&s, &http, 0, 0, 0)
    ))
        goto end;
    FILE *const tmp = tmpfile();
    if(!tmp) {
        LOG_ERRNO("tmpfile", 0);
        goto end;
    }
    if(!subs_list_videos(&s, 0, tmp))
        goto end;
    const char expected[] =
        "3 0 lbry 1630794985 claim_id4 id0 v4\n"
        "4 0 lbry 1630795015 claim_id5 id0 v5\n"
        "1 0 lbry 1630795115 claim_id6 id0 v6\n"
        "2 0 lbry 1630796966 claim_id7 id0 v7\n";
    if(!CHECK_FILE(tmp, expected))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return true;
}

static bool update_short(void) {
    const struct http_fake_response responses[] = {{
        .url = "/",
        .method = HTTP_POST,
        .post_data = "{"
            JSON("method":"claim_search",)
            JSON("params":) "{"
                JSON("channel":"id0","order_by":["release_time"],"page":1)
            "}"
        "}",
        .data = JSON({
            "result": {
                "items": [{
                    "claim_id": "claim_id7",
                    "value": {"title": "v7", "release_time": "1630796966"},
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id6",
                    "value": {"title": "v6", "release_time": "1630795115"},
                    "value_type": "stream"
                }],
                "page": 1,
                "page_size": 2,
                "total_items": 4,
                "total_pages": 2
            }
        }),
    }, {
        .url = "/",
        .method = HTTP_POST,
        .post_data = "{"
            JSON("method":"claim_search",)
            JSON("params":) "{"
                JSON("channel":"id0","order_by":["release_time"],"page":2)
            "}"
        "}",
        .data = JSON({
            "result": {
                "items": [{
                    "claim_id": "claim_id5",
                    "value": {"title": "v5", "release_time": "1630795015"},
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id4",
                    "value": {"title": "v4", "release_time": "1630794985"},
                    "value_type": "stream"
                }],
                "page": 2,
                "page_size": 2,
                "total_items": 4,
                "total_pages": 2
            }
        }),
    }};
    struct http_fake_server server = {
        .n = ARRAY_SIZE(responses),
        .responses = responses,
    };
    struct http_client http = http_client_fake_init(&server);
    struct subs s = {.db_path = ":memory:"};
    bool ret = false;
    if(!(
        subs_init(&s)
        && subs_add(&s, SUBS_LBRY, "name0", "id0")
        && subs_update(&s, &http, 0, 0, 0)
    ))
        goto end;
    server.n = 1;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return true;
}

int main(void) {
    log_set(stderr);
    db_sqlite_init();
    bool ret = true;
    ret = RUN(init_db) && ret;
    ret = RUN(add) && ret;
    ret = RUN(add_video) && ret;
    ret = RUN(tag) && ret;
    ret = RUN(tag_subs) && ret;
    ret = RUN(tag_videos) && ret;
    ret = RUN(watched) && ret;
    ret = RUN(update) && ret;
    ret = RUN(update_pages) && ret;
    ret = RUN(update_short) && ret;
    return !ret;
}
