#include "luavm_platform.h"

#include "lauxlib.h"
#include "lualib.h"

FILE *luavm_platform_open_binary_write(const char *path)
{
    return fopen(path, "wb");
}

void luaL_openlibs(lua_State *L)
{
    static const luaL_Reg loadedlibs[] = {
        {LUA_GNAME, luaopen_base},
        {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_UTF8LIBNAME, luaopen_utf8},
        {LUA_MATHLIBNAME, luaopen_math},
        {NULL, NULL}
    };

    const luaL_Reg *lib = loadedlibs;
    for (; lib->func != NULL; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
}
