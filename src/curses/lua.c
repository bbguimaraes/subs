#include "lua.h"

#include <string.h>

#include <lauxlib.h>

#include "../log.h"
#include "../subs.h"

#include "curses.h"

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

static int cur_item(lua_State *L) {
    struct videos *const v = *(struct videos**)lua_touserdata(L, 1);
    lua_pushinteger(L, v->items[v->list.i]);
    return 1;
}

void init_lua(lua_State *L, struct videos *videos) {
    lua_newtable(L);
    void *const ud = lua_newuserdatauv(L, sizeof(videos), 0);
    memcpy(ud, &videos, sizeof(videos));
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, cur_item);
    lua_setfield(L, -2, "cur_item");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "videos");
    luaL_newmetatable(L, "shell_mode");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, shell_mode_new);
    lua_setfield(L, -2, "new");
    lua_pushcfunction(L, shell_mode_close);
    lua_setfield(L, -2, "__close");
    lua_setfield(L, -2, "shell_mode");
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
    if(lua_pcall(L, 3, 12, top + 1) != LUA_OK)
        goto end;
    source_bar->x = (int)lua_tointeger(L, 2);
    source_bar->y = (int)lua_tointeger(L, 3);
    source_bar->width = (int)lua_tointeger(L, 4);
    source_bar->height = (int)lua_tointeger(L, 5);
    subs_bar->x = (int)lua_tointeger(L, 6);
    subs_bar->y = (int)lua_tointeger(L, 7);
    subs_bar->width = (int)lua_tointeger(L, 8);
    subs_bar->height = (int)lua_tointeger(L, 9);
    videos->x = (int)lua_tointeger(L,  10);
    videos->y = (int)lua_tointeger(L,  11);
    videos->width = (int)lua_tointeger(L, 12);
    videos->height = (int)lua_tointeger(L, 13);
    ret = true;
end:
    lua_settop(L, top);
    return ret;
}
