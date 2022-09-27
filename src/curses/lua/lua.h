#ifndef SUBS_CURSES_LUA_LUA_H
#define SUBS_CURSES_LUA_LUA_H

#include <stdbool.h>

typedef struct lua_State lua_State;
struct source_bar;
struct subs_bar;
struct videos;

void init_lua(lua_State *L, struct videos *videos);
bool calc_pos_lua(
    lua_State *L, struct source_bar *source_bar, struct subs_bar *subs_bar,
    struct videos *videos);

#endif
