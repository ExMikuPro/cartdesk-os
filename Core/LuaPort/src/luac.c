/*
** luac_cross.c  —  minimal luac for LuaPort (cross-compile to STM32)
** Usage: luac_cross [-o output.luac] input.lua
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lundump.h"
#include "lobject.h"
#include "lstate.h"

/* writer callback: dump bytes to FILE* */
static int file_writer(lua_State *L, const void *b, size_t size, void *ud) {
    (void)L;
    return fwrite(b, size, 1, (FILE*)ud) != 1;
}

int main(int argc, char *argv[]) {
    const char *input  = NULL;
    const char *output = "output.luac";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else {
            input = argv[i];
        }
    }

    if (!input) {
        fprintf(stderr, "Usage: %s [-o output.luac] input.lua\n", argv[0]);
        return 1;
    }

    lua_State *L = luaL_newstate();
    if (!L) { fprintf(stderr, "cannot create Lua state\n"); return 1; }

    /* load (parse) the source file */
    if (luaL_loadfile(L, input) != LUA_OK) {
        fprintf(stderr, "Error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    /* open output file */
    FILE *fout = fopen(output, "wb");
    if (!fout) {
        fprintf(stderr, "cannot open output file: %s\n", output);
        lua_close(L);
        return 1;
    }

    /* dump bytecode — strip=0 keeps debug info */
    const LClosure *cl = clLvalue(s2v(L->top.p - 1));
    int strip = 0;
    if (lua_dump(L, file_writer, fout, strip) != 0) {
        fprintf(stderr, "cannot dump bytecode\n");
        fclose(fout);
        lua_close(L);
        return 1;
    }

    fclose(fout);
    lua_close(L);

    printf("OK: %s -> %s\n", input, output);
    printf("  lua_Integer : %zu bytes\n", sizeof(lua_Integer));
    printf("  lua_Number  : %zu bytes\n", sizeof(lua_Number));
    return 0;
}
