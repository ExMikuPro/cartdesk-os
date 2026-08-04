cmake_minimum_required(VERSION 3.22)

project(luavm_tool C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT DEFINED CARTDESK_PROJECT_ROOT)
    get_filename_component(CARTDESK_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

set(LUA_SRC_DIR ${CARTDESK_PROJECT_ROOT}/Core/LuaPort/src)

if(NOT MSVC)
    set_source_files_properties(
            ${CARTDESK_PROJECT_ROOT}/Core/LuaPort/modules/lua_random.c
            ${CARTDESK_PROJECT_ROOT}/Core/LuaPort/modules/lua_storage.c
            ${CARTDESK_PROJECT_ROOT}/Core/LuaPort/modules/lua_timer.c
            ${CARTDESK_PROJECT_ROOT}/Core/APPS/TASK/cart_storage_format.c
            PROPERTIES COMPILE_OPTIONS "-Wconversion;-Wsign-conversion")
endif()

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
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/lua_app_instance.c
        ${LUAVM_LUA_SOURCES}
)

add_executable(lua_ui_owner_test
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/lua_ui_owner_test.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/lua_ui.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules/lua_ui_label.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules/lua_ui_button.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules/lua_ui_container.c
        ${LUAVM_LUA_SOURCES}
)

add_executable(lua_crc_test
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/lua_crc_test.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules/lua_crc.c
        ${LUAVM_LUA_SOURCES}
)
add_executable(lua_random_test
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/lua_random_test.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules/lua_random.c
        ${LUAVM_LUA_SOURCES}
)
add_executable(lua_storage_test
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/lua_storage_test.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules/lua_storage.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/APPS/TASK/cart_storage_format.c
        ${LUAVM_LUA_SOURCES}
)
add_executable(lua_assets_test
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/lua_assets_test.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules/lua_assets.c
        ${LUAVM_LUA_SOURCES}
)
target_compile_definitions(lua_assets_test PRIVATE
        XHGC_CART_NO_FATFS=1
        XHGC_CART_NO_FLASH=1)
add_executable(lua_timer_test
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/lua_timer_test.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules/lua_timer.c
        ${LUAVM_LUA_SOURCES}
)
add_executable(lua_vm_lifecycle_test
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/lua_vm_lifecycle_test.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Src/lua_vm.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/lua_app_instance.c
        ${LUAVM_LUA_SOURCES}
)
add_executable(lua_foundation_owner_test
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/lua_foundation_owner_test.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/lua_foundation.c
        ${LUAVM_LUA_SOURCES}
)
add_executable(lua_runtime_task_test
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/lua_runtime_task_test.c
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/APPS/TASK/lua_runtime_task.c
)
target_include_directories(lua_runtime_task_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/APPS/TASK
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Inc
)
target_include_directories(lua_crc_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/stubs
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Inc
        ${LUA_SRC_DIR}
)
target_compile_definitions(lua_random_test PRIVATE CARTDESK_HOST_TEST=1)
target_include_directories(lua_random_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/include
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules
        ${LUA_SRC_DIR}
)
target_include_directories(lua_storage_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/include
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/stubs
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/APPS/TASK
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Inc
        ${LUA_SRC_DIR}
)
target_include_directories(lua_assets_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort/modules
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/APPS/TASK
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Cart
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Inc
        ${LUA_SRC_DIR}
)
target_include_directories(lua_timer_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/include
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/APPS/TASK
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Inc
        ${LUA_SRC_DIR}
)
target_compile_definitions(lua_vm_lifecycle_test PRIVATE
        XHGC_CART_NO_FLASH=1)
target_include_directories(lua_vm_lifecycle_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/include
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Inc
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/APPS/TASK
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Cart
        ${CMAKE_CURRENT_LIST_DIR}/../../Middlewares/Third_Party/FatFs/src
        ${CMAKE_CURRENT_LIST_DIR}/../../FATFS/Target
        ${LUA_SRC_DIR}
)
target_include_directories(lua_foundation_owner_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/APPS/TASK
        ${LUA_SRC_DIR}
)

target_compile_definitions(luavm PRIVATE LUAVM_HOST=1)
target_include_directories(luavm PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort
        ${LUA_SRC_DIR}
)

target_include_directories(lua_ui_owner_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/host/include
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/LuaPort
        ${CMAKE_CURRENT_LIST_DIR}/../../Core/Inc
        ${LUA_SRC_DIR}
)

set_target_properties(luavm lua_ui_owner_test lua_crc_test lua_random_test lua_storage_test lua_assets_test lua_timer_test lua_vm_lifecycle_test lua_foundation_owner_test lua_runtime_task_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(MSVC)
    target_compile_options(luavm PRIVATE /W4)
else()
    target_compile_options(luavm PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(luavm PRIVATE m)
endif()

if(MSVC)
    target_compile_options(lua_ui_owner_test PRIVATE /W4)
else()
    target_compile_options(lua_ui_owner_test PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(lua_ui_owner_test PRIVATE m)
    target_compile_options(lua_crc_test PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(lua_crc_test PRIVATE m)
    target_compile_options(lua_random_test PRIVATE -Wall -Wextra -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(lua_random_test PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_libraries(lua_random_test PRIVATE m)
    target_compile_options(lua_storage_test PRIVATE -Wall -Wextra -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(lua_storage_test PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_libraries(lua_storage_test PRIVATE m)
    target_compile_options(lua_assets_test PRIVATE -Wall -Wextra -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(lua_assets_test PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_libraries(lua_assets_test PRIVATE m)
    target_compile_options(lua_timer_test PRIVATE -Wall -Wextra -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(lua_timer_test PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_libraries(lua_timer_test PRIVATE m)
    target_compile_options(lua_vm_lifecycle_test PRIVATE -Wall -Wextra -Werror
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(lua_vm_lifecycle_test PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_libraries(lua_vm_lifecycle_test PRIVATE m)
    target_compile_options(lua_foundation_owner_test PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(lua_foundation_owner_test PRIVATE m)
    target_compile_options(lua_runtime_task_test PRIVATE -Wall -Wextra -Werror)
endif()
