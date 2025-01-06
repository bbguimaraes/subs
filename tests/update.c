#include "db.h"
#include "http_fake.h"
#include "subs.h"

#include "common.h"

#define JSON(...) #__VA_ARGS__

const char *PROG_NAME = NULL;
const char *CMD_NAME = NULL;

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
                    "value": {
                        "title": "v6",
                        "release_time": "1630795115",
                        "video": {"duration": 33675}
                    },
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id4",
                    "value": {
                        "title": "v4",
                        "release_time": "1630796966",
                        "video": {"duration": 26233}
                    },
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
                    "value": {
                        "title": "v5",
                        "release_time": "1630795015",
                        "video": {"duration": 29954}
                    },
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
        && subs_update(&s, &http, 0, -1, 0, 0, 0, NULL)
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
        "3 0 lbry 1630795015 29954 claim_id5 id1 v5\n"
        "2 0 lbry 1630795115 33675 claim_id6 id0 v6\n"
        "1 0 lbry 1630796966 26233 claim_id4 id0 v4\n";
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
                    "value": {
                        "title": "v7",
                        "release_time": "1630796966",
                        "video": {"duration": 37396}
                    },
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id6",
                    "value": {
                        "title": "v6",
                        "release_time": "1630795115",
                        "video": {"duration": 33675}
                    },
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
                    "value": {
                        "title": "v5",
                        "release_time": "1630795015",
                        "video": {"duration": 29954}
                    },
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id4",
                    "value": {
                        "title": "v4",
                        "release_time": "1630794985",
                        "video": {"duration": 26233}
                    },
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
        && subs_update(&s, &http, 0, -1, 0, 0, 0, NULL)
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
        "3 0 lbry 1630794985 26233 claim_id4 id0 v4\n"
        "4 0 lbry 1630795015 29954 claim_id5 id0 v5\n"
        "1 0 lbry 1630795115 33675 claim_id6 id0 v6\n"
        "2 0 lbry 1630796966 37396 claim_id7 id0 v7\n";
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
                    "value": {
                        "title": "v7",
                        "release_time": "1630796966",
                        "video": {"duration": 37396}
                    },
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id6",
                    "value": {
                        "title": "v6",
                        "release_time": "1630795115",
                        "video": {"duration": 33675}
                    },
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
                    "value": {
                        "title": "v5",
                        "release_time": "1630795015",
                        "video": {"duration": 29954}
                    },
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id4",
                    "value": {
                        "title": "v4",
                        "release_time": "1630794985",
                        "video": {"duration": 26233}
                    },
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
        && subs_update(&s, &http, 0, -1, 0, 0, 0, NULL)
    ))
        goto end;
    server.n = 1;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return true;
}

static bool update_ids(void) {
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
                    "value": {
                        "title": "v6",
                        "release_time": "1630795115",
                        "video": {"duration": 33675}
                    },
                    "value_type": "stream"
                }, {
                    "claim_id": "claim_id4",
                    "value": {
                        "title": "v4",
                        "release_time": "1630796966",
                        "video": {"duration": 26233}
                    },
                    "value_type": "stream"
                }],
                "page": 1,
                "page_size": 20,
                "total_items": 2,
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
                    "value": {
                        "title": "v5",
                        "release_time": "1630795015",
                        "video": {"duration": 29954}
                    },
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
        && subs_update(&s, &http, 0, -1, 0, 0, 1, (i64[]){2})
    ))
        goto end;
    FILE *const tmp = tmpfile();
    if(!tmp) {
        LOG_ERRNO("tmpfile", 0);
        goto end;
    }
    if(!subs_list_videos(&s, 0, tmp))
        goto end;
    const char expected[] = "1 0 lbry 1630795015 29954 claim_id5 id1 v5\n";
    if(!CHECK_FILE(tmp, expected))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&s) && ret;
    return ret;
}

int main(void) {
    log_set(stderr);
    db_sqlite_init();
    bool ret = true;
    ret = RUN(update) && ret;
    ret = RUN(update_pages) && ret;
    ret = RUN(update_short) && ret;
    ret = RUN(update_ids) && ret;
    return !ret;
}
