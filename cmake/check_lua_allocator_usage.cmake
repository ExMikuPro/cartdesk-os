if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(GLOB_RECURSE lua_allocator_check_files
        LIST_DIRECTORIES false
        "${PROJECT_ROOT}/Core/Src/*.c"
        "${PROJECT_ROOT}/Core/Src/*.h"
        "${PROJECT_ROOT}/Core/Inc/*.h"
        "${PROJECT_ROOT}/Core/LuaPort/*.c"
        "${PROJECT_ROOT}/Core/LuaPort/*.h"
)

set(allowed_lua_newstate_file "${PROJECT_ROOT}/Core/Src/lua_vm_memory.c")
set(lua_allocator_errors "")

foreach(source_file IN LISTS lua_allocator_check_files)
    if(source_file MATCHES "/Core/LuaPort/src/")
        continue()
    endif()

    file(READ "${source_file}" source_text)

    string(REGEX MATCH "(^|[^A-Za-z0-9_])luaL_newstate[ \t\r\n]*\\("
           has_luaL_newstate "${source_text}")
    if(has_luaL_newstate)
        list(APPEND lua_allocator_errors
             "${source_file}: use lua_vm_newstate(), not luaL_newstate()")
    endif()

    string(REGEX MATCH "(^|[^A-Za-z0-9_])lua_newstate[ \t\r\n]*\\("
           has_lua_newstate "${source_text}")
    if(has_lua_newstate AND NOT source_file STREQUAL allowed_lua_newstate_file)
        list(APPEND lua_allocator_errors
             "${source_file}: use lua_vm_newstate(), not raw lua_newstate()")
    endif()
endforeach()

if(lua_allocator_errors)
    list(JOIN lua_allocator_errors "\n" lua_allocator_error_text)
    message(FATAL_ERROR
            "Lua allocator entry misuse detected:\n${lua_allocator_error_text}")
endif()

message(STATUS "Lua allocator entry usage check passed")
