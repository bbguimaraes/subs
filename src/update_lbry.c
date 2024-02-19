#include "update.h"

#include <cjson/cJSON.h>

#include "buffer.h"
#include "http.h"
#include "log.h"
#include "subs.h"

enum { DONE = 1, ERR };

struct update_item {
    const char *claim_id, *title;
    i64 timestamp, duration_seconds;
};

static const char *POST_FMT = "{"
    "\"method\":\"claim_search\","
    "\"params\":{"
        "\"channel\":\"%s\","
        "\"order_by\":[\"release_time\"],"
        "\"page\":%zu"
    "}"
"}";

static cJSON *get_item(const cJSON *j, const char *k) {
    return cJSON_GetObjectItemCaseSensitive(j, k);
}

static cJSON *post(
    const struct http_client *http,
    const char *url, const char *id, size_t page,
    struct buffer *data, struct buffer *b);
static bool get_result_info(const cJSON *j, size_t *n_pages);
static int process_page(
    const struct subs *s, u32 flags, int id, size_t page,
    const cJSON *root, struct buffer *items);

bool update_lbry(
    const struct subs *s, const struct http_client *http, struct buffer *b,
    u32 flags, int id, const char *ext_id)
{
    bool ret = false;
    struct buffer post_data = {0};
    cJSON *root = post(http, s->url, ext_id, 1, &post_data, b);
    size_t n_pages;
    if(!root || !get_result_info(root, &n_pages))
        goto err0;
    if(s->log_level)
        fprintf(stderr, "total pages: %zu\n", n_pages);
    if(!n_pages)
        goto done;
    for(size_t page = 1;;) {
        b->n = 0;
        switch(process_page(s, flags, id, page, root, b)) {
        case DONE: goto done;
        case ERR: goto err1;
        }
        if(page++ == n_pages)
            break;
        b->n = post_data.n = 0;
        cJSON_Delete(root);
        root = post(http, s->url, ext_id, page, &post_data, b);
        if(!root)
            goto err1;
    }
done:
    ret = true;
err1:
    cJSON_Delete(root);
err0:
    free(post_data.p);
    return ret;
}

static cJSON *post(
    const struct http_client *http,
    const char *url, const char *id, size_t page,
    struct buffer *data, struct buffer *b)
{
    buffer_printf(data, POST_FMT, id, page);
    if(!http->post(http->data, url, data->p, b))
        return NULL;
    cJSON *const ret = cJSON_ParseWithLength(b->p, b->n);
    if(!ret) {
        const char *const e = cJSON_GetErrorPtr();
        LOG_ERR(
            "failed to parse JSON response%s%s\n",
            e ? ": " : "", e ? e : "");
        return NULL;
    }
    return ret;
}

static bool get_result_info(const cJSON *j, size_t *n_pages_p) {
    const cJSON *const result = get_item(j, "result");
    if(!result)
        return LOG_ERR("'result' missing\n", 0), false;
    const cJSON *const n_pages = get_item(result, "total_pages");
    if(!n_pages)
        return LOG_ERR("'result.total_pages' missing\n", 0), false;
    *n_pages_p = (size_t)n_pages->valueint;
    return true;
}

static bool list_to_items(
    const struct subs *s, const cJSON *items, struct buffer *b);
static int process(const struct subs *s, int id, const struct buffer *b);

static int process_page(
    const struct subs *s, u32 flags, int id, size_t page,
    const cJSON *j, struct buffer *b)
{
    const bool verbose = s->log_level;
    const cJSON *const result = get_item(j, "result");
    if(!result)
        return LOG_ERR("'result' missing\n", 0), ERR;
    const cJSON *const items = get_item(result, "items");
    if(!items)
        return LOG_ERR("'result.items' missing\n", 0), ERR;
    const int page_size = cJSON_GetArraySize(items);
    if(page_size < 0) {
        LOG_ERR("invalid page size (page %d): %d)\n", page, page_size);
        return ERR;
    }
    if(verbose)
        fprintf(stderr, "page %zu, size %d\n", page, page_size);
    buffer_reserve(b, (size_t)page_size * sizeof(struct update_item));
    if(!list_to_items(s, items, b))
        return ERR;
    const int n_updated = process(s, id, b);
    switch(n_updated) {
    case -1:
        return ERR;
    case 0:
        if(flags & SUBS_UPDATE_DEEP)
            return 0;
        if(verbose)
            fputs(
                "page has no new videos"
                " (use a deep update to unconditionally fetch all pages)\n",
                stderr);
        return DONE;
    default:
        if(verbose)
            fprintf(stderr, "added %d new video(s)\n", n_updated);
        return 0;
    }
}

static i64 lbry_timestamp(
    const char *id, const cJSON *item, const cJSON *value)
{
    i64 ret = -1;
    const cJSON *j = get_item(value, "release_time");
    if(j) {
        if(!cJSON_IsString(j))
            return LOG_ERR("%s: release_time is not a string\n", id), -1;
        if((ret = parse_i64(j->valuestring)) == -1)
            return LOG_ERR("%s: invalid release_time\n", id), -1;
        return ret;
    }
    j = get_item(get_item(item, "meta"), "creation_timestamp");
    if(j) {
        if(!cJSON_IsNumber(j))
            return LOG_ERR("%s: invalid meta.creation_timestamp\n", id), -1;
        return j->valueint;
    }
    LOG_ERR("%s: no usable timestamp\n", id);
    return -1;
}

static i64 lbry_duration_seconds(const char *id, const cJSON *value) {
    const cJSON *const video = get_item(value, "video");
    if(!video)
        return LOG_ERR("%s: missing video field\n", id), -1;
    const cJSON *const duration = get_item(video, "duration");
    if(!duration)
        return LOG_ERR("%s: missing video.duration field\n", id), -1;
    if(!cJSON_IsNumber(duration))
        return LOG_ERR("%s: invalid duration\n", id), -1;
    return duration->valueint;
}

static bool list_to_items(
    const struct subs *s, const cJSON *items, struct buffer *b)
{
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        const cJSON *const claim_id = get_item(item, "claim_id");
        if(!claim_id || !claim_id->valuestring)
            return LOG_ERR("item missing \"claim_id\"\n", 0), false;
        const char *const id = claim_id->valuestring;
        const cJSON *const value_type = get_item(item, "value_type");
        if(!value_type)
            return LOG_ERR("%s: missing value_type\n", id), false;
        if(strcmp(value_type->valuestring, "stream") != 0) {
            if(s->log_level)
                LOG_ERR("%s: not a stream, ignoring\n", id);
            continue;
        }
        const cJSON *const value = get_item(item, "value");
        if(!value)
            return LOG_ERR("%s: missing value\n", id), false;
        const cJSON *const title = get_item(value, "title");
        if(!title || !title->valuestring)
            return LOG_ERR("%s: invalid title\n", id), false;
        const i64 timestamp = lbry_timestamp(id, item, value);
        if(timestamp == -1)
            return false;
        const i64 duration_seconds = lbry_duration_seconds(id, value);
        if(duration_seconds == -1)
            return false;
        BUFFER_APPEND(b, (&(struct update_item){
            .claim_id = id,
            .title = title->valuestring,
            .timestamp = timestamp,
            .duration_seconds = duration_seconds,
        }));
    }
    return true;
}

static bool insert(
    sqlite3 *db, sqlite3_stmt *stmt, bool log,
    int id, const struct update_item *item, int *acc);

static int process(const struct subs *s, int id, const struct buffer *b) {
    sqlite3 *const db = s->db;
    const struct update_item *p = b->p;
    size_t n = b->n / sizeof(*p);
    assert(n * sizeof(*p) == b->n);
    const char sql[] =
        "insert into videos (sub, ext_id, timestamp, duration_seconds, title)"
        " values (?, ?, ?, ?, ?)"
        " on conflict (sub, ext_id) do nothing;";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return -1;
    bool ret = true;
    int n_changes = 0;
    for(p += n - 1; n--; --p)
        if(!insert(db, stmt, 1 < s->log_level, id, p, &n_changes)) {
            ret = false;
            break;
        }
    ret = sqlite3_finalize(stmt) == SQLITE_OK && ret;
    return ret ? n_changes : -1;
}

static bool insert(
    sqlite3 *db, sqlite3_stmt *stmt, bool log,
    int id, const struct update_item *item, int *acc)
{
    if(!(
        sqlite3_bind_int(stmt, 1, id) == SQLITE_OK
        && sqlite3_bind_text(
            stmt, 2, item->claim_id, -1, SQLITE_STATIC) == SQLITE_OK
        && sqlite3_bind_int64(stmt, 3, item->timestamp) == SQLITE_OK
        && sqlite3_bind_int64(stmt, 4, item->duration_seconds) == SQLITE_OK
        && sqlite3_bind_text(
            stmt, 5, item->title, -1, SQLITE_STATIC) == SQLITE_OK
    ))
        return false;
    for(;;)
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW:
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: goto done;
        default: return false;
        }
done:
    if(sqlite3_changes(db)) {
        if(log)
            fprintf(
                stderr, "created new video: %" PRId64 " %d %s %" PRId64 " %"
                    PRId64 " %s\n",
                (i64)sqlite3_last_insert_rowid(db), id, item->claim_id,
                item->timestamp, item->duration_seconds, item->title);
        ++(*acc);
    }
    return sqlite3_reset(stmt) == SQLITE_OK;
}
