#include "lua.h"

#include <string.h>

#include <lauxlib.h>

#include "../log.h"
#include "../subs.h"

#include "curses.h"
#include "message.h"
#include "subs.h"
#include "source.h"
#include "videos.h"

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

static int add_message_lua(lua_State *L) {
    if(!add_message(
        *(struct subs_curses**)lua_touserdata(L, 1),
        lua_tostring(L, 2)
    ))
        luaL_error(L, "failed to add message");
    return 0;
}

static int cur_item(lua_State *L) {
    struct videos *const v = *(struct videos**)lua_touserdata(L, 1);
    lua_pushinteger(L, v->list.ids[v->list.i]);
    return 1;
}

static int items_iter(lua_State *L) {
    struct videos *const v = *(struct videos**)lua_touserdata(L, 1);
    const lua_Integer i = lua_tointeger(L, 2);
    if(i < 0 || v->n <= i)
        return 0;
    lua_pushinteger(L, i + 1);
    lua_pushinteger(L, v->list.ids[i]);
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
    lua_pushcfunction(L, cur_item);
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
    lua_pushcfunction(L, add_message_lua);
    lua_setfield(L, -2, "add_message");
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
    lua_State *L, struct message *message, struct source_bar *source_bar,
    struct subs_bar *subs_bar, struct videos *videos)
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
    if(lua_pcall(L, 3, 16, top + 1) != LUA_OK)
        goto end;
    message->x = (int)lua_tointeger(L, 2);
    message->y = (int)lua_tointeger(L, 3);
    message->width = (int)lua_tointeger(L, 4);
    message->height = (int)lua_tointeger(L, 5);
    source_bar->x = (int)lua_tointeger(L, 6);
    source_bar->y = (int)lua_tointeger(L, 7);
    source_bar->width = (int)lua_tointeger(L, 8);
    source_bar->height = (int)lua_tointeger(L, 9);
    subs_bar->x = (int)lua_tointeger(L, 10);
    subs_bar->y = (int)lua_tointeger(L, 11);
    subs_bar->width = (int)lua_tointeger(L, 12);
    subs_bar->height = (int)lua_tointeger(L, 13);
    videos->x = (int)lua_tointeger(L,  14);
    videos->y = (int)lua_tointeger(L,  15);
    videos->width = (int)lua_tointeger(L, 16);
    videos->height = (int)lua_tointeger(L, 17);
    ret = true;
end:
    lua_settop(L, top);
    return ret;
}
