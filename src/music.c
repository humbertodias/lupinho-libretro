#include "music.h"

#include <lauxlib.h>

/*
 * Libretro stand-in for lupinho's music.c. The desktop player renders Furnace
 * modules through raylib's audio device; this core has neither, and submits
 * silence. The Lua music.loop / music.stop names stay registered so games
 * that call them keep running.
 */

static int lua_music_loop(lua_State *L) {
    luaL_checkstring(L, 1);
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) luaL_checknumber(L, 2);
    return 0;
}

static int lua_music_stop(lua_State *L) {
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) luaL_checknumber(L, 1);
    return 0;
}

void music_bind(lua_State *L) {
    lua_newtable(L);
    lua_pushcfunction(L, lua_music_loop);
    lua_setfield(L, -2, "loop");
    lua_pushcfunction(L, lua_music_stop);
    lua_setfield(L, -2, "stop");
    lua_setglobal(L, "music");
}

void music_set_root(const char *dir) {
    (void)dir;
}

void music_update(float dt) {
    (void)dt;
}

void music_shutdown(void) {}
