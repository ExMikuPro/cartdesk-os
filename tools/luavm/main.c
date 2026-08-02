#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lua_app_instance.h"
#include "luavm_platform.h"

typedef struct {
    FILE *file;
    const char *path;
} dump_writer_t;

static void print_usage(const char *argv0)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s --compile input.lua output.luac\n"
            "  %s --check script.lua\n"
            "  %s --self-test\n",
            argv0, argv0, argv0);
}

static int dump_writer(lua_State *L, const void *data, size_t size, void *ud)
{
    dump_writer_t *writer = (dump_writer_t *)ud;
    (void)L;

    return fwrite(data, 1, size, writer->file) == size ? 0 : 1;
}

static void *luavm_alloc(void *ud, void *ptr, size_t old_size, size_t new_size)
{
    (void)ud;
    (void)old_size;

    if (new_size == 0u) {
        free(ptr);
        return NULL;
    }

    return realloc(ptr, new_size);
}

static lua_State *luavm_newstate(void)
{
    return lua_newstate(luavm_alloc, NULL);
}

static int compile_lua(const char *input_path, const char *output_path)
{
    lua_State *L = luavm_newstate();
    if (L == NULL) {
        fprintf(stderr, "luavm: failed to create Lua state\n");
        return 1;
    }

    int rc = luaL_loadfile(L, input_path);
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "luavm: failed to load '%s': %s\n",
                input_path, err ? err : "(unknown error)");
        lua_close(L);
        return 1;
    }

    dump_writer_t writer = {
        .file = luavm_platform_open_binary_write(output_path),
        .path = output_path
    };
    if (writer.file == NULL) {
        fprintf(stderr, "luavm: failed to open output '%s'\n", output_path);
        lua_close(L);
        return 1;
    }

    rc = lua_dump(L, dump_writer, &writer, 0);
    if (fclose(writer.file) != 0 && rc == 0) {
        rc = 1;
    }

    if (rc != 0) {
        fprintf(stderr, "luavm: failed to write bytecode '%s'\n", writer.path);
        lua_close(L);
        return 1;
    }

    lua_close(L);
    return 0;
}

static int push_lifecycle_args(lua_State *L, int self_ref, const char *name)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, self_ref);

    if (strcmp(name, "fixed_update") == 0 ||
        strcmp(name, "update") == 0 ||
        strcmp(name, "late_update") == 0) {
        lua_pushnumber(L, 0.016);
        return 2;
    }

    return 1;
}

static int call_lifecycle(lua_State *L, int self_ref, const char *name)
{
    lua_getglobal(L, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }

    if (!lua_isfunction(L, -1)) {
        fprintf(stderr, "luavm: lifecycle '%s' exists but is not a function\n", name);
        lua_pop(L, 1);
        return 1;
    }

    int nargs = push_lifecycle_args(L, self_ref, name);
    int rc = lua_pcall(L, nargs, 0, 0);
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "luavm: lifecycle '%s' failed: %s\n",
                name, err ? err : "(unknown error)");
        lua_pop(L, 1);
        return 1;
    }

    return 0;
}

static int check_lua(const char *script_path)
{
    /* TODO: add optional on_message/on_input simulation once packer payload formats are fixed. */
    static const char *const lifecycle_names[] = {
        "init",
        "fixed_update",
        "update",
        "late_update",
        "final",
        "on_reload",
        NULL
    };

    lua_State *L = luavm_newstate();
    if (L == NULL) {
        fprintf(stderr, "luavm: failed to create Lua state\n");
        return 1;
    }

    luaL_openlibs(L);

    int rc = luaL_dofile(L, script_path);
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "luavm: failed to run '%s': %s\n",
                script_path, err ? err : "(unknown error)");
        lua_close(L);
        return 1;
    }

    lua_newtable(L);
    if (!LuaAppInstance_CreateDefaultTables(L, -1)) {
        fprintf(stderr, "luavm: failed to initialize self\n");
        lua_close(L);
        return 1;
    }
    int self_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    int failed = 0;
    for (const char *const *name = lifecycle_names; *name != NULL; ++name) {
        if (call_lifecycle(L, self_ref, *name) != 0) {
            failed = 1;
            break;
        }
    }

    luaL_unref(L, LUA_REGISTRYINDEX, self_ref);
    lua_close(L);
    return failed ? 1 : 0;
}

static int self_tables_test(void)
{
    static const char *const fields[] = {
        "state", "ui", "assets", "timers", "services", NULL
    };
    lua_State *L = luavm_newstate();
    if (L == NULL) return 1;

    lua_newtable(L);
    if (!LuaAppInstance_CreateDefaultTables(L, -1)) {
        lua_close(L);
        return 1;
    }
    int first = lua_gettop(L);
    lua_newtable(L);
    if (!LuaAppInstance_CreateDefaultTables(L, -1)) {
        lua_close(L);
        return 1;
    }
    int second = lua_gettop(L);

    for (const char *const *field = fields; *field != NULL; ++field) {
        lua_getfield(L, first, *field);
        lua_getfield(L, second, *field);
        if (!lua_istable(L, -1) || !lua_istable(L, -2) ||
            lua_rawequal(L, -1, -2)) {
            fprintf(stderr, "luavm: self table test failed for '%s'\n", *field);
            lua_close(L);
            return 1;
        }
        lua_pop(L, 2);

        for (const char *const *other = field + 1; *other != NULL; ++other) {
            lua_getfield(L, first, *field);
            lua_getfield(L, first, *other);
            if (lua_rawequal(L, -1, -2)) {
                fprintf(stderr,
                        "luavm: self fields '%s' and '%s' share a table\n",
                        *field, *other);
                lua_close(L);
                return 1;
            }
            lua_pop(L, 2);
        }
    }

    lua_getfield(L, first, "children");
    int has_unexpected_field = !lua_isnil(L, -1);
    lua_pop(L, 1);
    lua_close(L);
    if (has_unexpected_field) {
        fprintf(stderr, "luavm: unexpected self field\n");
        return 1;
    }
    puts("luavm: self table test passed");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--compile") == 0) {
        return compile_lua(argv[2], argv[3]);
    }

    if (argc == 3 && strcmp(argv[1], "--check") == 0) {
        return check_lua(argv[2]);
    }

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        return self_tables_test();
    }

    print_usage(argv[0]);
    return 2;
}
