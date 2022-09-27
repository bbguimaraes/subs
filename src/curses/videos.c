#include "videos.h"

#include <stdlib.h>
#include <time.h>

#include <lauxlib.h>

#include "../buffer.h"
#include "../log.h"
#include "../subs.h"

#include "curses.h"

static void clear_selection(struct videos *v) {
    v->tag = v->type = v->sub = 0;
    v->flags = (u8)(v->flags & ~VIDEOS_UNTAGGED);
}

static void render_border(struct list *l, int n) {
    int x = 0;
    x -= (int)int_digits(n) + 3;
    list_write_title(l, x, " %d ", n);
}

static bool item_watched(const char *s) {
    return s[1] == 'N';
}

static char *videos_row_to_str(sqlite3_stmt *stmt, int id) {
    static const char type_str[] = {
        [SUBS_LBRY] = 'L',
        [SUBS_YOUTUBE] = 'Y',
        [SUBS_TYPE_MAX] = '?',
    };
    static const char watched_str[] = {'N', ' ', '?'};
    const int type = sqlite3_column_int(stmt, 1);
    const int watched = sqlite3_column_int(stmt, 2);
    const char *const sub = (const char*)sqlite3_column_text(stmt, 3);
    const char *const title = (const char*)sqlite3_column_text(stmt, 4);
    const time_t timestamp = sqlite3_column_int(stmt, 5);
    struct tm tm, *tm_p = localtime_r(&timestamp, &tm);
    if(!tm_p)
        return LOG_ERRNO("localtime_r", 0), NULL;
    char timestamp_str[11];
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d", &tm);
    return sprintf_alloc(
        "%c%c %d %s %s | %s",
        type_str[MIN(SUBS_TYPE_MAX, (unsigned)type)],
        watched_str[MIN(2, (unsigned)watched)],
        id, timestamp_str, sub, title);
}

static bool populate(
    sqlite3 *db, const char *sql, size_t len, const int *param,
    char **lines, int *ids)
{
    bool ret = false;
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, (int)len, 0, &stmt, NULL);
    if(!stmt)
        return false;
    if(param && sqlite3_bind_int(stmt, 1, *param) != SQLITE_OK)
        goto end;
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: break;
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: ret = true; /* fall through */
        default: goto end;
        }
        const int id = sqlite3_column_int(stmt, 0);
        *ids++ = id;
        *lines++ = videos_row_to_str(stmt, id);
    }
end:
    return (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
}

bool reload_item(struct videos *v) {
    const char sql[] =
        "select"
            " videos.id, subs.type, videos.watched, subs.name, videos.title,"
            " videos.timestamp"
        " from videos"
        " join subs on videos.sub == subs.id"
        " where videos.id == ?";
    const int id = v->items[v->list.i];
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(v->s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(sqlite3_bind_int(stmt, 1, id) != SQLITE_OK)
        goto err;
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: break;
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: /* fall through */
        default: goto end;
        }
        list_set_name(&v->list, "%s", videos_row_to_str(stmt, id));
        ret = true;
        goto end;
    }
end:
err:
    ret = (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
    return ret;
}

static bool toggle_item_watched(struct videos *v) {
    const int id = v->items[v->list.i];
    const char sql[] = "update videos set watched = not watched where id == ?";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(v->s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(sqlite3_bind_int(stmt, 1, id) != SQLITE_OK)
        goto end;
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: break;
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: ret = true; /* fall through */
        default: goto end;
        }
    }
end:
    if(!(sqlite3_finalize(stmt) == SQLITE_OK && ret && reload_item(v)))
        return false;
    list_move(&v->list, v->list.i + 1);
    return true;
}

static bool next_unwatched(struct videos *v) {
    struct list *l = &v->list;
    const char *const *const lines = (const char *const*)l->lines;
    const int n = l->n;
    int i = l->i;
    if(item_watched(lines[i]))
        ++i;
    for(; i != n; ++i)
        if(item_watched(lines[i])) {
            list_move(l, i);
            return true;
        }
    return false;
}

static bool open_item(sqlite3 *db, lua_State *L, int id) {
    const char sql[] =
        "select subs.type, videos.yt_id from videos"
        " join subs on subs.id == videos.sub"
        " where videos.id == ?";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    if(sqlite3_bind_int(stmt, 1, id) != SQLITE_OK)
        goto err0;
    const int top = lua_gettop(L);
    lua_pushcfunction(L, subs_lua_msgh);
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: break;
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: ret = true; /* fall through */
        default: goto err1;
        }
        const char *src =
            "return function(...)\n"
            "    assert(os.execute(table.concat({'d subs open', ...}, ' ')))\n"
            "end\n";
        if(luaL_loadstring(L, src) != LUA_OK) {
            fprintf(stderr, "%s\n", lua_tostring(L, -1));
            goto err1;
        }
        if(lua_pcall(L, 0, 1, 1) != LUA_OK)
            goto err1;
        lua_pushinteger(L, sqlite3_column_int(stmt, 0));
        lua_pushstring(L, (const char*)sqlite3_column_text(stmt, 1));
        if(lua_pcall(L, 2, 0, 1) != LUA_OK)
            goto err1;
    }
err1:
    lua_settop(L, top);
err0:
    ret = (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
    return ret;
}

static int input_lua(lua_State *L, int c) {
    const int top = lua_gettop(L);
    int ret = 0;
    lua_pushcfunction(L, subs_lua_msgh);
    if(lua_getglobal(L, "videos_input") == LUA_TNIL)
        goto end;
    lua_pushinteger(L, c);
    ret = (lua_pcall(L, 1, 1, top + 1) == LUA_OK)
        ? lua_toboolean(L, -1) : ERR;
end:
    lua_settop(L, top);
    return ret;
}

static void build_query_common(
    int tag, int type, int sub, u8 flags, struct buffer *b)
{
    const bool untagged = flags & VIDEOS_UNTAGGED;
    const bool filtered = tag || type || sub || untagged;
    if(untagged || tag)
        --b->n, buffer_append_str(b,
            " left outer join videos_tags on videos.id == videos_tags.video"
            " left outer join subs_tags on videos.sub == subs_tags.sub");
    if(untagged)
        --b->n, buffer_append_str(b,
            " where (subs_tags.id is null and videos_tags.id is null)");
    else if(tag)
        --b->n, buffer_append_str(b,
            " where (videos_tags.tag == ?1 or subs_tags.tag == ?1)");
    else if(type)
        --b->n, buffer_append_str(b, " where subs.type == ?");
    else if(sub)
        --b->n, buffer_append_str(b, " where sub == ?");
    if(flags & VIDEOS_WATCHED) {
        if(filtered)
            --b->n, buffer_append_str(b, " and");
        else
            --b->n, buffer_append_str(b, " where");
        --b->n, buffer_append_str(b, " videos.watched == 1");
    } else if(flags & VIDEOS_NOT_WATCHED) {
        if(filtered)
            --b->n, buffer_append_str(b, " and");
        else
            --b->n, buffer_append_str(b, " where");
        --b->n, buffer_append_str(b, " videos.watched == 0");
    }
}

static void build_query_count(
    int tag, int type, int sub, u8 flags, struct buffer *b)
{
    buffer_append_str(b, "select count(*) from videos");
    if(type)
        --b->n, buffer_append_str(b, " join subs on videos.sub == subs.id");
    build_query_common(tag, type, sub, flags, b);
}

static void build_query_list(
    int tag, int type, int sub, u8 flags, struct buffer *b)
{
    buffer_append_str(b,
        "select"
            " videos.id, subs.type, videos.watched,"
            " subs.name, videos.title, videos.timestamp"
        " from videos"
        " join subs on videos.sub == subs.id");
    build_query_common(tag, type, sub, flags, b);
    --b->n, buffer_append_str(b, " order by videos.timestamp, videos.id");
}

void videos_destroy(struct videos *v) {
    list_destroy(&v->list);
    free(v->items);
}

void videos_toggle_watched(struct videos *v) {
    if((v->flags ^= VIDEOS_WATCHED))
        v->flags = (u8)((unsigned)v->flags & ~(unsigned)VIDEOS_NOT_WATCHED);
}

void videos_toggle_not_watched(struct videos *v) {
    if((v->flags ^= VIDEOS_NOT_WATCHED))
        v->flags = (u8)((unsigned)v->flags & ~(unsigned)VIDEOS_WATCHED);
}

void videos_set_untagged(struct videos *v) {
    clear_selection(v);
    v->flags |= VIDEOS_ACTIVE | VIDEOS_UNTAGGED;
}

void videos_set_tag(struct videos *v, int t) {
    clear_selection(v);
    v->tag = t;
    v->flags |= VIDEOS_ACTIVE;
}

void videos_set_type(struct videos *v, int t) {
    clear_selection(v);
    v->type = t;
    v->flags |= VIDEOS_ACTIVE;
}

void videos_set_sub(struct videos *v, int s) {
    clear_selection(v);
    v->sub = s;
    v->flags |= VIDEOS_ACTIVE;
}

bool videos_leave(struct window *w) {
    list_set_active(&((struct videos*)w->data)->list, false);
    return true;
}

bool videos_enter(struct window *w) {
    curs_set(0);
    list_set_active(&((struct videos*)w->data)->list, true);
    return true;
}

bool videos_input(struct window *w, int c) {
    struct videos *const v = w->data;
    switch(c) {
    case 'N': if(!toggle_item_watched(v)) return false; break;
    case 'n': if(!next_unwatched(v)) return true; break;
    case 'o':
        if(!open_item(v->s->db, v->s->L, v->items[v->list.i]))
            return false;
        break;
    case 'r': if(!reload_item(v)) return false; break;
    default:
        if(list_input(&v->list, c))
            break;
        switch(input_lua(v->s->L, c)) {
        case ERR: return false;
        case 0: return true;
        }
        break;
    }
    list_refresh(&v->list);
    return true;
}

bool videos_resize(struct videos *v) {
    if(v->flags & VIDEOS_ACTIVE)
        list_resize(&v->list, NULL, v->x, 0, v->width, LINES);
    else if(!list_init(&v->list, NULL, 0, NULL, v->x, v->y, v->width, LINES))
        return false;
    render_border(&v->list, v->n);
    list_refresh(&v->list);
    return true;
}

bool videos_reload(struct videos *v) {
    free(v->items);
    v->items = NULL;
    if(~v->flags & VIDEOS_ACTIVE) {
        if(!list_init(&v->list, NULL, 0, NULL, v->x, v->y, v->width, LINES))
            return false;
        list_refresh(&v->list);
        return true;
    }
    sqlite3 *const db = v->s->db;
    const int tag = v->tag, type = v->type, sub = v->sub;
    const int *const param = tag ? &tag : type ? &type : sub ? &sub : NULL;
    struct buffer sql = {0};
    build_query_count(tag, type, sub, v->flags, &sql);
    int n = 0;
    if(!query_to_int(db, sql.p, sql.n - 1, param, &n))
        goto err0;
    char **lines = calloc((size_t)n, sizeof(*lines));
    if(n && !lines)
        goto err0;
    int *const items = calloc((size_t)n, sizeof(*items));
    if(n && !items)
        goto err1;
    sql.n = 0;
    build_query_list(tag, type, sub, v->flags, &sql);
    if(!populate(db, sql.p, sql.n - 1, param, lines, items))
        goto err2;
    if(!list_init(&v->list, NULL, n, lines, v->x, v->y, v->width, LINES))
        goto err2;
    render_border(&v->list, n);
    free(sql.p);
    v->n = n;
    v->items = items;
    v->tag = tag;
    list_refresh(&v->list);
    return true;
err2:
    free(items);
err1:
    for(char **p = lines; *p; ++p)
        free(*p);
    free(lines);
err0:
    free(sql.p);
    return false;
}
