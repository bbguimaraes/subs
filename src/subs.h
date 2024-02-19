#ifndef SUBS_SUBS_H
#define SUBS_SUBS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <lua.h>
#include <sqlite3.h>

#include "const.h"

struct lua_State;

struct http_client;

enum subs_type {
    SUBS_LBRY = (uint32_t)1,
    SUBS_YOUTUBE,
    SUBS_TYPE_MAX,
};

enum subs_flags {
    SUBS_UPDATE_DEEP = (uint32_t)1 << 0,
};

struct subs {
    sqlite3 *db;
    lua_State *L;
    uint32_t log_level;
    const char *url;
    char db_path[SUBS_MAX_PATH];
};

const char *subs_type_name(enum subs_type type);
enum subs_type subs_parse_type(const char *type);
void subs_sqlite_setup(void);
sqlite3 *subs_new_db_connection(const struct subs *s);
bool subs_init(struct subs *s);
bool subs_init_from_argv(struct subs *s, int *argc, char ***argv);
bool subs_destroy(struct subs *s);
bool subs_list(const struct subs *s, int64_t tag, FILE *f);
bool subs_list_videos(const struct subs *s, int64_t tag, FILE *f);
bool subs_list_tags(const struct subs *s, FILE *f);
bool subs_add(
    const struct subs *s,
    enum subs_type type, const char *name, const char *id);
bool subs_rm(const struct subs *s, int64_t id);
bool subs_add_video(
    const struct subs *s,
    int64_t sub, int64_t timestamp, int64_t duration_seconds,
    const char *ext_id, const char *title);
bool subs_add_tag(const struct subs *s, const char *name);
bool subs_tag_sub(const struct subs *s, int64_t tag, int64_t id);
bool subs_tag_video(const struct subs *s, int64_t tag, int64_t id);
bool subs_set_watched(const struct subs *s, int64_t id, bool b);
bool subs_update(
    const struct subs *s, const struct http_client *http, uint32_t flags,
    int since, int delay);
bool subs_start_tui(const struct subs *s);
lua_State *subs_lua_init(struct subs *s);
bool subs_lua(const struct subs *s, const char *src);
int subs_lua_msgh(lua_State *L);
bool subs_exec(struct subs *s, int argc, char **argv);

#endif
