#include <stdbool.h>
#include <stdlib.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "log.h"
#include "subs.h"
#include "util.h"

static void new_userdata_ptr(lua_State *L, const void *p) {
    *(const void**)lua_newuserdatauv(L, sizeof(p), 0) = p;
}

static void *userdata_ptr(lua_State *L, int i) {
    return *(void**)lua_touserdata(L, i);
}

static const struct subs *from_state(lua_State *L) {
    lua_getglobal(L, "S");
    const struct subs *const ret = userdata_ptr(L, -1);
    lua_pop(L, 1);
    return ret;
}

static int type(lua_State *L) {
    lua_pushstring(L, subs_type_name(lua_tointeger(L, 1)));
    return 1;
}

static int db(lua_State *L) {
    const struct subs *const s = from_state(L);
    const char *const sql = lua_tostring(L, 1);
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(s->db, sql, -1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    lua_pushcfunction(L, subs_lua_msgh);
    const int msgh = lua_gettop(L);
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: break;
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: goto end;
        default: goto err;
        }
        lua_pushvalue(L, 2);
        new_userdata_ptr(L, stmt);
        luaL_getmetatable(L, "subs_row");
        lua_setmetatable(L, -2);
        if(lua_pcall(L, 1, 0, msgh) != LUA_OK)
            goto err;
    }
end:
    if(sqlite3_finalize(stmt) != SQLITE_OK)
        goto err;
    return 0;
err:
    sqlite3_finalize(stmt);
    luaL_error(L, "error");
    return 0;
}

int row_col_count(lua_State *L) {
    lua_pushinteger(L, sqlite3_column_count(userdata_ptr(L, 1)));
    return 1;
}

#define D(name, type, cast, f) \
    static int row_ ## name(lua_State *L) { \
        f(L, cast sqlite3_column_ ## type( \
            userdata_ptr(L, 1), \
            (int)lua_tointeger(L, 2))); \
        return 1; \
    }
D(int, int,, lua_pushinteger)
D(str, text, (const char*), lua_pushstring)
#undef D

static bool err(lua_State *L, const char *f, const char *s) {
    log_err("%s: %s: %s\n", f, s, lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
}

static void init_state(const struct subs *s, lua_State *L) {
    new_userdata_ptr(L, s);
    luaL_newmetatable(L, "subs");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "WATCHED");
    lua_pushinteger(L, SUBS_LBRY);
    lua_setfield(L, -2, "LBRY");
    lua_pushinteger(L, SUBS_YOUTUBE);
    lua_setfield(L, -2, "YOUTUBE");
    lua_pushcfunction(L, type);
    lua_setfield(L, -2, "type");
    lua_pushcfunction(L, db);
    lua_setfield(L, -2, "db");
    lua_setmetatable(L, -2);
    lua_setglobal(L, "S");
    luaL_newmetatable(L, "subs_row");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, row_col_count);
    lua_setfield(L, -2, "col_count");
    lua_pushcfunction(L, row_int);
    lua_setfield(L, -2, "int");
    lua_pushcfunction(L, row_str);
    lua_setfield(L, -2, "str");
    lua_pop(L, 1);
}

static bool read_config(lua_State *L) {
    char v[SUBS_MAX_PATH] = {0};
#define B "/subs/init.lua"
    const char *const base = B;
    const char *const home_base = "/.config" B;
#undef B
    const char *const xdg = getenv("XDG_CONFIG_HOME");
    if(xdg) {
        if(!join_path(v, 2, xdg, base))
            return false;
    } else {
        const char *const home = getenv("HOME");
        if(!home)
            return true;
        if(!join_path(v, 2, home, home_base))
            return false;
    }
    if(!file_exists(v))
        return true;
    if(luaL_dofile(L, v) != LUA_OK) {
        LOG_ERR("%s: %s\n", v, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

lua_State *subs_lua_init(struct subs *s) {
    lua_State *const L = luaL_newstate();
    if(!L) {
        log_err("%s: failed to create Lua state\n");
        return NULL;
    }
    luaL_openlibs(L);
    init_state(s, L);
    if(!read_config(L))
        goto err;
    return L;
err:
    lua_close(L);
    return NULL;
}

int subs_lua_msgh(lua_State *L) {
    luaL_traceback(L, L, lua_tostring(L, 1), 0);
    log_err("lua: %s\n", lua_tostring(L, -1));
    return 0;
}

bool subs_lua(const struct subs *s, const char *src) {
    lua_State *const L = s->L;
    return luaL_dostring(L, src) == LUA_OK || err(L, __func__, src);
}
