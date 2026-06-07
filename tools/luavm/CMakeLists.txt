cmake_minimum_required(VERSION 3.22)

project(luavm_tool C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT DEFINED CARTDESK_PROJECT_ROOT)
    get_filename_component(CARTDESK_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

set(LUA_SRC_DIR ${CARTDESK_PROJECT_ROOT}/Core/LuaPort/src)

set(LUAVM_LUA_SOURCES
        ${LUA_SRC_DIR}/lapi.c
        ${LUA_SRC_DIR}/lauxlib.c
        ${LUA_SRC_DIR}/lbaselib.c
        ${LUA_SRC_DIR}/lcode.c
        ${LUA_SRC_DIR}/lcorolib.c
        ${LUA_SRC_DIR}/lctype.c
        ${LUA_SRC_DIR}/ldebug.c
        ${LUA_SRC_DIR}/ldo.c
        ${LUA_SRC_DIR}/ldump.c
        ${LUA_SRC_DIR}/lfunc.c
        ${LUA_SRC_DIR}/lgc.c
        ${LUA_SRC_DIR}/llex.c
        ${LUA_SRC_DIR}/lmathlib.c
        ${LUA_SRC_DIR}/lmem.c
        ${LUA_SRC_DIR}/lobject.c
        ${LUA_SRC_DIR}/lopcodes.c
        ${LUA_SRC_DIR}/lparser.c
        ${LUA_SRC_DIR}/lstate.c
        ${LUA_SRC_DIR}/lstring.c
        ${LUA_SRC_DIR}/lstrlib.c
        ${LUA_SRC_DIR}/ltable.c
        ${LUA_SRC_DIR}/ltablib.c
        ${LUA_SRC_DIR}/ltm.c
        ${LUA_SRC_DIR}/lundump.c
        ${LUA_SRC_DIR}/lutf8lib.c
        ${LUA_SRC_DIR}/lvm.c
        ${LUA_SRC_DIR}/lzio.c
)

add_executable(luavm
        main.c
        host_platform.c
        luavm_platform.h
        ${LUAVM_LUA_SOURCES}
)

target_compile_definitions(luavm PRIVATE LUAVM_HOST=1)
target_include_directories(luavm PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${LUA_SRC_DIR}
)

set_target_properties(luavm PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(MSVC)
    target_compile_options(luavm PRIVATE /W4)
else()
    target_compile_options(luavm PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(luavm PRIVATE m)
endif()
