#include "videos.h"

#include <stdlib.h>
#include <time.h>

#include <lauxlib.h>

#include "../buffer.h"
#include "../log.h"
#include "../subs.h"

#include "curses.h"

#include "window/list_search.h"

#define FIELDS \
    "select" \
        " videos.id, subs.type, videos.watched," \
        " subs.name, videos.title," \
        " videos.timestamp, videos.duration_seconds"

static void clear_selection(struct videos *v) {
    v->tag = v->type = v->sub = 0;
    v->flags = (u8)(v->flags & ~VIDEOS_UNTAGGED);
}

static void render_border(struct list *l, struct videos *v) {
    enum { BAR = 1, SPACE = 1 };
    const int i = l->i, n = l->n, h = l->height - 2 * LIST_BORDER_SIZE;
    int x = -2 * SPACE;
    const int n_pages = n / h, page = i / h;
    x -= (int)int_digits(n)
        + (int)int_digits(page)
        + (int)int_digits(n_pages)
        + BAR + 2 * SPACE;
    list_write_title(l, x, " %d %d/%d ", n, page, n_pages);
    if(!search_is_active(&v->search))
        return;
    const struct buffer search = v->search.b;
    x -= (search.n ? (int)search.n - 1 : 0) + BAR + SPACE;
    list_write_title(l, x, " /%s ", search.p ? (const char*)search.p : "");
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
    const unsigned duration_seconds = (unsigned)sqlite3_column_int(stmt, 6);
    struct tm tm, *const tm_p = localtime_r(&timestamp, &tm);
    if(!tm_p)
        return LOG_ERRNO("localtime_r", 0), NULL;
    const int COLON = 1, DIGIT = 1;
    char timestamp_str[11], duration_str[2 * COLON + 5 * DIGIT + 1];
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d", &tm);
    {
        const unsigned h = duration_seconds / 60 / 60;
        const unsigned m = duration_seconds / 60 % 60;
        const unsigned s = duration_seconds % 60;
        assert(h < 10000);
        if(h < 10)
            sprintf(duration_str, "%1u:%02u:%02u", h, m, s);
        else
            sprintf(duration_str, "%4u:%02u", h, m);
    }
    return sprintf_alloc(
        "%c%c %d %s %s %s | %s",
        type_str[MIN(SUBS_TYPE_MAX, (unsigned)type)],
        watched_str[MIN(2, (unsigned)watched)],
        id, timestamp_str, duration_str, sub, title);
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
    struct list *const l = &v->list;
    const char sql[] =
        FIELDS
        " from videos"
        " join subs on videos.sub == subs.id"
        " where videos.id == ?";
    const int id = v->items[l->i];
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
        list_set_name(l, "%s", videos_row_to_str(stmt, id));
        ret = true;
        goto end;
    }
end:
err:
    ret = (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
    return ret;
}

static bool toggle_item_watched(struct videos *v) {
    struct list *const l = &v->list;
    const int id = v->items[l->i];
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
    list_move(l, l->i + 1);
    render_border(l, v);
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
            render_border(l, v);
            return true;
        }
    return false;
}

static bool open_item(sqlite3 *db, lua_State *L, int id) {
    const char sql[] =
        "select subs.type, videos.ext_id from videos"
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

static enum subs_curses_key input_lua(lua_State *L, int c);

static enum subs_curses_key input(struct videos *v, int c) {
    struct list *const l = &v->list;
    switch(c) {
    case '/':
        search_reset(&v->search);
        list_box(l);
        render_border(l, v);
        break;
    case 'N':
        if(!l->n)
            return true;
        if(!toggle_item_watched(v))
            return false;
        break;
    case 'n':
        if(!l->n)
            return true;
        if(!search_is_empty(&v->search)) {
            if(list_search_next(&v->search, &v->list))
                render_border(l, v);
        } else if(!next_unwatched(v))
            return true;
        break;
    case 'o':
        if(!l->n)
            return true;
        if(!open_item(v->s->db, v->s->L, v->items[l->i]))
            return false;
        break;
    case 'r':
        if(!l->n)
            return true;
        if(!reload_item(v))
            return false;
        break;
    default:
        switch(list_input(l, c)) {
        case KEY_ERROR:
            return false;
        case KEY_HANDLED:
            list_box(l);
            render_border(l, v);
            break;
        case KEY_IGNORED:
            switch(input_lua(v->s->L, c)) {
            case KEY_ERROR:
                return false;
            case KEY_IGNORED:
                return KEY_IGNORED;
            case KEY_HANDLED:
                break;
            }
            break;
        }
        break;
    }
    list_refresh(l);
    return true;
}

static enum subs_curses_key input_lua(lua_State *L, int c) {
    const int top = lua_gettop(L);
    enum subs_curses_key ret = KEY_IGNORED;
    lua_pushcfunction(L, subs_lua_msgh);
    if(lua_getglobal(L, "videos_input") == LUA_TNIL)
        goto end;
    ret = KEY_ERROR;
    lua_pushinteger(L, c);
    if(lua_pcall(L, 1, 1, top + 1) == LUA_OK)
        ret = (int)lua_tointeger(L, -1);
end:
    lua_settop(L, top);
    return ret;
}

static enum subs_curses_key input_search(struct videos *v, int c) {
    struct search *const s = &v->search;
    struct list *const l = &v->list;
    const enum subs_curses_key ret = list_search_input(s, l, c);
    if(ret == KEY_HANDLED) {
        list_box(l);
        render_border(l, v);
        list_refresh(l);
    }
    return ret;
}

static void build_query_common(
    int tag, int type, int sub, u8 global_flags, u8 flags, struct buffer *b)
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
    if(global_flags & WATCHED) {
        if(filtered)
            --b->n, buffer_append_str(b, " and");
        else
            --b->n, buffer_append_str(b, " where");
        --b->n, buffer_append_str(b, " videos.watched == 1");
    } else if(global_flags & NOT_WATCHED) {
        if(filtered)
            --b->n, buffer_append_str(b, " and");
        else
            --b->n, buffer_append_str(b, " where");
        --b->n, buffer_append_str(b, " videos.watched == 0");
    }
}

static void build_query_count(
    int tag, int type, int sub, u8 global_flags, u8 flags, struct buffer *b)
{
    buffer_append_str(b, "select count(*) from videos");
    if(type)
        --b->n, buffer_append_str(b, " join subs on videos.sub == subs.id");
    build_query_common(tag, type, sub, global_flags, flags, b);
}

static void build_query_list(
    int tag, int type, int sub, u8 global_flags, u8 flags, struct buffer *b)
{
    buffer_append_str(b,
        FIELDS
        " from videos"
        " join subs on videos.sub == subs.id");
    build_query_common(tag, type, sub, global_flags, flags, b);
    --b->n, buffer_append_str(b, " order by videos.timestamp, videos.id");
}

void videos_destroy(struct videos *v) {
    list_destroy(&v->list);
    free(v->items);
    free(v->search.b.p);
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

bool videos_leave(void *data) {
    list_set_active(&((struct videos*)data)->list, false);
    return true;
}

bool videos_enter(void *data) {
    curs_set(0);
    list_set_active(&((struct videos*)data)->list, true);
    return true;
}

void videos_redraw(void *data) {
    struct list *const l = &((struct videos*)data)->list;
    list_redraw(l);
    list_refresh(l);
}

enum subs_curses_key videos_input(void *data, int c) {
    struct videos *const v = data;
    if(search_is_input_active(&v->search))
        return input_search(v, c);
    else
        return input(v, c);
}

bool videos_resize(struct videos *v) {
    struct list *const l = &v->list;
    if(v->flags & VIDEOS_ACTIVE)
        list_resize(l, NULL, v->x, 0, v->width, LINES);
    else if(!list_init(l, NULL, 0, NULL, v->x, v->y, v->width, LINES))
        return false;
    render_border(l, v);
    list_refresh(l);
    return true;
}

bool videos_reload(struct videos *v) {
    struct list *const l = &v->list;
    free(v->items);
    v->items = NULL;
    if(~v->flags & VIDEOS_ACTIVE) {
        if(!list_init(l, NULL, 0, NULL, v->x, v->y, v->width, LINES))
            return false;
        list_refresh(l);
        return true;
    }
    sqlite3 *const db = v->s->db;
    const int tag = v->tag, type = v->type, sub = v->sub;
    const int *const param = tag ? &tag : type ? &type : sub ? &sub : NULL;
    const u8 global_flags = v->s->flags, flags = v->flags;
    struct buffer sql = {0};
    build_query_count(tag, type, sub, global_flags, flags, &sql);
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
    build_query_list(tag, type, sub, global_flags, flags, &sql);
    if(!populate(db, sql.p, sql.n - 1, param, lines, items))
        goto err2;
    if(!list_init(l, NULL, n, lines, v->x, v->y, v->width, LINES))
        goto err2;
    render_border(l, v);
    free(sql.p);
    v->n = n;
    v->items = items;
    v->tag = tag;
    list_refresh(l);
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
