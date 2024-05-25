#include "lua.h"

#include <string.h>

#include <lauxlib.h>

#include "../../log.h"
#include "../../subs.h"

#include "../curses.h"
#include "../subs.h"
#include "../source.h"
#include "../videos.h"

static bool check_type(lua_State *L, int type, int expected, const char *name) {
    return (type == expected) || (
        LOG_ERR(
            "%s: expected %s, got %s\n",
            name, lua_typename(L, expected), lua_typename(L, type)),
        false
    );
}

static bool get_int(lua_State *L, int type, const char *name, int *p) {
    return check_type(L, type, LUA_TNUMBER, name)
        && ((*p = (int)lua_tointeger(L, -1)), true);
}

static bool get_pos_int(lua_State *L, int type, const char *name, int *p) {
    int ret;
    if(!get_int(L, type, name, &ret))
        return false;
    if(ret <= 0) {
        LOG_ERR("%s: expected positive integer, got %d\n", name, ret);
        return false;
    }
    *p = ret;
    return true;
}

static bool get_dimensions(
    lua_State *L, int i, const char *name,
    int *x, int *y, int *w, int *h)
{
    return check_type(L, lua_getfield(L, i, name), LUA_TTABLE, name)
        && get_int(L, lua_geti(L, -1, 1), name, x)
        && get_int(L, lua_geti(L, -2, 2), name, y)
        && get_pos_int(L, lua_geti(L, -3, 3), name, w)
        && get_pos_int(L, lua_geti(L, -4, 4), name, h);
}

static int shell_mode_new(lua_State *L) {
    lua_newtable(L);
    luaL_getmetatable(L, "shell_mode");
    lua_setmetatable(L, -2);
    suspend_tui();
    return 1;
}

static int shell_mode_close(lua_State *L) {
    (void)L;
    resume_tui();
    return 0;
}

static int videos_cur_item(lua_State *L) {
    struct videos *const v = *(struct videos**)lua_touserdata(L, 1);
    lua_pushinteger(L, v->items[v->list.i]);
    return 1;
}

static int items_iter(lua_State *L) {
    struct videos *const v = *(struct videos**)lua_touserdata(L, 1);
    const lua_Integer i = lua_tointeger(L, 2);
    if(i < 0 || v->n <= i)
        return 0;
    lua_pushinteger(L, i + 1);
    lua_pushinteger(L, v->items[i]);
    return 2;
}

static int items(lua_State *L) {
    lua_pushcfunction(L, items_iter);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 0);
    return 3;
}

static void init_videos(lua_State *L, struct videos *videos) {
    memcpy(lua_newuserdatauv(L, sizeof(videos), 0), &videos, sizeof(videos));
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, videos_cur_item);
    lua_setfield(L, -2, "cur_item");
    lua_pushcfunction(L, items);
    lua_setfield(L, -2, "items");
    lua_setmetatable(L, -2);
}

static void init_shell_mode(lua_State *L) {
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, shell_mode_new);
    lua_setfield(L, -2, "new");
    lua_pushcfunction(L, shell_mode_close);
    lua_setfield(L, -2, "__close");
}

static void init_meta(lua_State *L, struct videos *videos) {
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushinteger(L, KEY_ERROR);
    lua_setfield(L, -2, "KEY_ERROR");
    lua_pushinteger(L, KEY_HANDLED);
    lua_setfield(L, -2, "KEY_HANDLED");
    lua_pushinteger(L, KEY_IGNORED);
    lua_setfield(L, -2, "KEY_IGNORED");
    init_videos(L, videos);
    lua_setfield(L, -2, "videos");
    luaL_newmetatable(L, "shell_mode");
    init_shell_mode(L);
    lua_setfield(L, -2, "shell_mode");
}

void init_lua(lua_State *L, struct subs_curses *s, struct videos *videos) {
    memcpy(lua_newuserdatauv(L, sizeof(s), 0), &s, sizeof(s));
    lua_newtable(L);
    init_meta(L, videos);
    lua_setmetatable(L, -2);
    lua_setglobal(L, "curses");
}

bool calc_pos_lua(
    lua_State *L, struct source_bar *source_bar, struct subs_bar *subs_bar,
    struct videos *videos)
{
    const int top = lua_gettop(L);
    lua_pushcfunction(L, subs_lua_msgh);
    bool ret = false;
    if(lua_getglobal(L, "calc_pos") == LUA_TNIL) {
        LOG_ERR("'calc_pos' function not found\n", 0);
        goto end;
    }
    lua_pushinteger(L, LINES);
    lua_pushinteger(L, COLS);
    lua_pushinteger(L, source_bar->n_tags);
    if(lua_pcall(L, 3, 1, top + 1) != LUA_OK)
        goto end;
    if(!check_type(L, lua_type(L, -1), LUA_TTABLE, "return type"))
        goto end;
    const int t = lua_gettop(L);
    int x, y, w, h;
#define X(p) \
    if(!(get_dimensions(L, t, #p, &x, &y, &w, &h))) goto end; \
    p->x = x; p->y = y; p->width = w; p->height = h;
    X(source_bar)
    X(subs_bar)
    X(videos)
#undef X
    ret = true;
end:
    lua_settop(L, top);
    return ret;
}
