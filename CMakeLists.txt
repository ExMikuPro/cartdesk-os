cmake_minimum_required(VERSION 3.22)

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME cartdesk-os)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(${CMAKE_PROJECT_NAME} C ASM)
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

option(CARTDESK_ENABLE_BOARD_TESTS "Run board-level test diagnostics at startup" OFF)
option(XHGC_MEMINFO_SELFTEST_ENABLE "Run APP_ARENA_REST meminfo selftest in Debug builds" OFF)
option(XHGC_DMA_POOL_SELFTEST_ENABLE "Run DMA_POOL allocator meminfo selftest in Debug builds" OFF)
option(XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE "Enable legacy experimental cart resource cache owner for RESOURCE_ARENA" OFF)
find_package(Python3 QUIET COMPONENTS Interpreter)
include(ExternalProject)

# ==============================================================================
# Project paths
# ==============================================================================
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CORE_DIR ${PROJECT_ROOT}/Core)
set(CORE_INC_DIR ${CORE_DIR}/Inc)
set(CORE_SRC_DIR ${CORE_DIR}/Src)
set(DRIVER_DIR ${CORE_DIR}/Driver)
set(LUA_PORT_DIR ${CORE_DIR}/LuaPort)
set(LUA_SRC_DIR ${LUA_PORT_DIR}/src)
set(LVGL_DIR ${CORE_DIR}/APPS/LVGL)
set(LVGL_PORT_DIR ${LVGL_DIR}/port)
set(TASK_DIR ${CORE_DIR}/APPS/TASK)
set(CART_DIR ${CORE_DIR}/Cart)
set(MEMORY_DIR ${CORE_DIR}/Memory)
set(BOARD_GPIO_DIR ${DRIVER_DIR}/GPIO)
set(SCREEN_DIR ${CORE_DIR}/Screen)
set(SCREEN_PAGE_DIR ${SCREEN_DIR}/Page)
set(SCREEN_UTILS_DIR ${SCREEN_DIR}/utils)
set(DEBUG_DIR ${CORE_DIR}/Debug)
set(TOOLS_DIR ${PROJECT_ROOT}/tools)
set(FATFS_DIR ${PROJECT_ROOT}/FATFS)
set(FATFS_APP_DIR ${FATFS_DIR}/App)
set(FATFS_TARGET_DIR ${FATFS_DIR}/Target)
set(FATFS_MIDDLEWARE_DIR ${PROJECT_ROOT}/Middlewares/Third_Party/FatFs/src)
set(FREERTOS_DIR ${PROJECT_ROOT}/Middlewares/Third_Party/FreeRTOS/Source)

set(STM32_HAL_INCLUDES
        ${PROJECT_ROOT}/Drivers/STM32H7xx_HAL_Driver/Inc
        ${PROJECT_ROOT}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
        ${PROJECT_ROOT}/Drivers/CMSIS/Include
        ${PROJECT_ROOT}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
)

set(FATFS_INCLUDES
        ${FATFS_MIDDLEWARE_DIR}
        ${FATFS_TARGET_DIR}
        ${FATFS_APP_DIR}
)

set(FREERTOS_INCLUDES
        ${FREERTOS_DIR}/include
        ${FREERTOS_DIR}/portable/GCC/ARM_CM4F
        ${FREERTOS_DIR}/CMSIS_RTOS_V2/include
        ${PROJECT_ROOT}/Drivers/CMSIS/RTOS2/Include
)

set(LVGL_PUBLIC_INCLUDES
        ${LVGL_DIR}
        ${LVGL_DIR}/src
        ${LVGL_DIR}/src/core
        ${LVGL_PORT_DIR}
)

# ==============================================================================
# Host tools
#
# The firmware presets use an arm-none-eabi toolchain. Build PC-side tools in a
# separate native CMake project so they are runnable by packers on the host.
# ==============================================================================
set(HOST_TOOLS_BUILD_DIR ${PROJECT_ROOT}/build/host_tools CACHE PATH
        "Native build directory for host-side tools")
set(LUAVM_PACKER_TOOLS_DIR ${PROJECT_ROOT}/packer/tools CACHE PATH
        "Directory where the packer expects the host luavm executable")

if(CMAKE_HOST_WIN32)
    set(LUAVM_HOST_EXE_NAME luavm.exe)
else()
    set(LUAVM_HOST_EXE_NAME luavm)
endif()
set(LUAVM_HOST_EXECUTABLE ${HOST_TOOLS_BUILD_DIR}/bin/${LUAVM_HOST_EXE_NAME})
set(LUAVM_PACKER_EXECUTABLE ${LUAVM_PACKER_TOOLS_DIR}/${LUAVM_HOST_EXE_NAME})

ExternalProject_Add(luavm_tool
        SOURCE_DIR ${TOOLS_DIR}/luavm
        BINARY_DIR ${HOST_TOOLS_BUILD_DIR}
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCARTDESK_PROJECT_ROOT=${PROJECT_ROOT}
        BUILD_BYPRODUCTS ${LUAVM_HOST_EXECUTABLE}
        BUILD_ALWAYS TRUE
        EXCLUDE_FROM_ALL TRUE
        INSTALL_COMMAND ""
)

add_custom_target(copy_luavm_to_packer
        COMMAND ${CMAKE_COMMAND} -E make_directory ${LUAVM_PACKER_TOOLS_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${LUAVM_HOST_EXECUTABLE}
                ${LUAVM_PACKER_EXECUTABLE}
        DEPENDS luavm_tool
        COMMENT "Copying host luavm to ${LUAVM_PACKER_EXECUTABLE}"
        VERBATIM
)

# ==============================================================================
# Common compile interface
# ==============================================================================
set(COMMON_DEFINES
        LV_CONF_INCLUDE_SIMPLE=1
        USE_HAL_DRIVER
        USE_PWR_LDO_SUPPLY
        STM32H743xx
)

if(CARTDESK_ENABLE_BOARD_TESTS)
    list(APPEND COMMON_DEFINES CARTDESK_ENABLE_BOARD_TESTS=1)
endif()

if(XHGC_MEMINFO_SELFTEST_ENABLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND COMMON_DEFINES XHGC_MEMINFO_SELFTEST_ENABLE=1)
endif()

if(XHGC_DMA_POOL_SELFTEST_ENABLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND COMMON_DEFINES XHGC_DMA_POOL_SELFTEST_ENABLE=1)
endif()

add_library(cartdesk_common INTERFACE)
target_compile_definitions(cartdesk_common INTERFACE ${COMMON_DEFINES})
target_include_directories(cartdesk_common INTERFACE
        ${CORE_INC_DIR}
        ${STM32_HAL_INCLUDES}
        ${FREERTOS_INCLUDES}
)

# ==============================================================================
# FreeRTOS Kernel + CMSIS-RTOS2 wrapper
# ==============================================================================
add_library(freertos_kernel STATIC
        ${FREERTOS_DIR}/tasks.c
        ${FREERTOS_DIR}/queue.c
        ${FREERTOS_DIR}/list.c
        ${FREERTOS_DIR}/timers.c
        ${FREERTOS_DIR}/event_groups.c
        ${FREERTOS_DIR}/stream_buffer.c
        ${FREERTOS_DIR}/croutine.c
        ${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c
        ${FREERTOS_DIR}/portable/MemMang/heap_4.c
        ${FREERTOS_DIR}/CMSIS_RTOS_V2/cmsis_os2.c
        ${FREERTOS_DIR}/CMSIS_RTOS_V2/os_systick.c
)

target_compile_definitions(freertos_kernel PUBLIC
        CMSIS_OS2_CUSTOM_SYSTICK_HANDLER
        CMSIS_device_header="stm32h7xx.h"
)
target_link_libraries(freertos_kernel PUBLIC cartdesk_common)

# ==============================================================================
# 目标 1: lua —— Lua 5.x 解释器核心
# ==============================================================================
add_library(lua STATIC
        ${LUA_SRC_DIR}/lapi.c
        ${LUA_SRC_DIR}/lcode.c
        ${LUA_SRC_DIR}/lctype.c
        ${LUA_SRC_DIR}/ldebug.c
        ${LUA_SRC_DIR}/ldo.c
        ${LUA_SRC_DIR}/ldump.c
        ${LUA_SRC_DIR}/lfunc.c
        ${LUA_SRC_DIR}/lgc.c
        ${LUA_SRC_DIR}/llex.c
        ${LUA_SRC_DIR}/lmem.c
        ${LUA_SRC_DIR}/lobject.c
        ${LUA_SRC_DIR}/lopcodes.c
        ${LUA_SRC_DIR}/lparser.c
        ${LUA_SRC_DIR}/lstate.c
        ${LUA_SRC_DIR}/lstring.c
        ${LUA_SRC_DIR}/ltable.c
        ${LUA_SRC_DIR}/ltm.c
        ${LUA_SRC_DIR}/lundump.c
        ${LUA_SRC_DIR}/lvm.c
        ${LUA_SRC_DIR}/lzio.c
        # 辅助库
        ${LUA_SRC_DIR}/lauxlib.c
        # 标准库（按需裁剪）
        ${LUA_SRC_DIR}/lbaselib.c
        ${LUA_SRC_DIR}/lcorolib.c
        ${LUA_SRC_DIR}/lmathlib.c
        ${LUA_SRC_DIR}/lstrlib.c
        ${LUA_SRC_DIR}/ltablib.c
        ${LUA_SRC_DIR}/lutf8lib.c
)

target_compile_options(lua PRIVATE -ffunction-sections -fdata-sections)
target_include_directories(lua PUBLIC ${LUA_SRC_DIR})

# ==============================================================================
# 目标 2: lvgl —— LVGL v9.x GUI 库
# ==============================================================================
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS
        ${LVGL_DIR}/src/*.c
        ${LVGL_DIR}/demos/widgets/*.c
)

add_library(lvgl STATIC
        ${LVGL_SOURCES}
        ${LVGL_DIR}/font/lv_menu_font.c
)

# LVGL v9 内部头文件通过相对路径互相引用，只暴露根目录、src 和移植层。
# 不递归添加子目录，避免把 thorvg/rapidjson/msinttypes 等第三方路径带进 GCC。
target_include_directories(lvgl PUBLIC ${LVGL_PUBLIC_INCLUDES})
target_link_libraries(lvgl PUBLIC cartdesk_common)

# ==============================================================================
# 目标 3: drv_display —— 显示相关硬件驱动（LCD / SDRAM / LVGL Port）
# ==============================================================================
add_library(drv_display STATIC
        ${DRIVER_DIR}/LCD/lcd.c
        ${DRIVER_DIR}/LCD/lcd.h
        ${DRIVER_DIR}/SDRAM/sdram.c
        ${DRIVER_DIR}/SDRAM/sdram.h
        ${DRIVER_DIR}/SDRAM/sdram_cold_pool.c
        ${CORE_INC_DIR}/sdram_cold_pool.h
        ${LVGL_PORT_DIR}/lv_port_disp.c
        ${LVGL_PORT_DIR}/lv_port_disp.h
        ${LVGL_PORT_DIR}/lv_port_tick.c
        ${LVGL_PORT_DIR}/lv_port_tick.h
        ${LVGL_PORT_DIR}/lv_port_indev.c
        ${LVGL_PORT_DIR}/lv_port_indev.h
        ${LVGL_PORT_DIR}/lvgl_init.c
        ${LVGL_PORT_DIR}/lvgl_init.h
        ${DRIVER_DIR}/TOUCH/touch.c
)

target_include_directories(drv_display PUBLIC
        ${DRIVER_DIR}/LCD
        ${DRIVER_DIR}/SDRAM
        ${DRIVER_DIR}/TOUCH
        ${LVGL_PORT_DIR}
)
target_link_libraries(drv_display PUBLIC cartdesk_common lvgl)

# ==============================================================================
# 目标 4: drv_storage —— 存储相关驱动（Flash/LittleFS / EEPROM / SD）
# ==============================================================================
add_library(drv_storage STATIC
        ${DRIVER_DIR}/FLASH/flash.c
        ${DRIVER_DIR}/FLASH/flash.h
        ${DRIVER_DIR}/FLASH/lfs.c
        ${DRIVER_DIR}/FLASH/lfs.h
        ${DRIVER_DIR}/FLASH/lfs_util.c
        ${DRIVER_DIR}/FLASH/lfs_util.h
        ${DRIVER_DIR}/FLASH/lfs_port.c
        ${DRIVER_DIR}/FLASH/lfs_port.h
        ${DRIVER_DIR}/EEPROM/eeprom.c
        ${DRIVER_DIR}/EEPROM/eeprom.h
)

target_include_directories(drv_storage PUBLIC
        ${DRIVER_DIR}/FLASH
        ${DRIVER_DIR}/EEPROM
)
target_link_libraries(drv_storage PUBLIC cartdesk_common)

# ==============================================================================
# 目标 5: drv_misc —— 其他外设驱动（UID / RNG）
# ==============================================================================
add_library(drv_misc STATIC
        ${DRIVER_DIR}/UID/uid.c
        ${DRIVER_DIR}/UID/uid.h
        ${DRIVER_DIR}/RNG/rng_port.c
        ${DRIVER_DIR}/RNG/rng_port.h
)

target_include_directories(drv_misc PUBLIC
        ${DRIVER_DIR}/UID
        ${DRIVER_DIR}/RNG
)
target_link_libraries(drv_misc PUBLIC cartdesk_common)

# ==============================================================================
# 目标 6: drv_gpio —— 板级 GPIO 逻辑映射
# ==============================================================================
add_library(drv_gpio STATIC
        ${BOARD_GPIO_DIR}/board_gpio.c
        ${BOARD_GPIO_DIR}/board_gpio.h
        ${BOARD_GPIO_DIR}/board_pwm.c
        ${BOARD_GPIO_DIR}/board_pwm.h
)

target_include_directories(drv_gpio PUBLIC ${BOARD_GPIO_DIR})
target_link_libraries(drv_gpio PUBLIC cartdesk_common)

# ==============================================================================
# 目标 7: mem_layout —— SDRAM 固定分区描述 / 校验 / dump
# ==============================================================================
add_library(mem_layout STATIC
        ${MEMORY_DIR}/xhgc_memory_layout.c
        ${MEMORY_DIR}/xhgc_memory_layout.h
        ${MEMORY_DIR}/xhgc_dcache.c
        ${MEMORY_DIR}/xhgc_dcache.h
        ${MEMORY_DIR}/xhgc_meminfo.c
        ${MEMORY_DIR}/xhgc_meminfo.h
)

target_include_directories(mem_layout PUBLIC ${MEMORY_DIR})
target_link_libraries(mem_layout PUBLIC cartdesk_common)
target_link_libraries(drv_display PUBLIC mem_layout)

# ==============================================================================
# 目标 8: lua_port —— Lua 与硬件的绑定层（C 模块 + VM 封装）
# ==============================================================================
add_library(lua_port STATIC
        ${CORE_INC_DIR}/lua_vm.h
        ${CORE_INC_DIR}/lua_vm_memory.h
        ${CORE_SRC_DIR}/lua_vm.c
        ${CORE_SRC_DIR}/lua_vm_memory.c
        ${LUA_PORT_DIR}/app_arena.c
        ${LUA_PORT_DIR}/app_arena.h
        ${LUA_PORT_DIR}/lua_cart_resource_cache.c
        ${LUA_PORT_DIR}/lua_cart_resource_cache.h
        ${LUA_PORT_DIR}/lua_port.c
        ${LUA_PORT_DIR}/resource_arena_owner.c
        ${LUA_PORT_DIR}/resource_arena_owner.h
        ${LUA_PORT_DIR}/resource_manager.c
        ${LUA_PORT_DIR}/resource_manager.h
        ${LUA_PORT_DIR}/modules/lua_gpio.c
        ${LUA_PORT_DIR}/modules/lua_pwm.c
        ${LUA_PORT_DIR}/modules/lua_delay.c
        ${LUA_PORT_DIR}/modules/lua_tim.c
        ${LUA_PORT_DIR}/modules/lua_rng.c
        ${LUA_PORT_DIR}/modules/lua_crc.c
        ${LUA_PORT_DIR}/modules/lua_ui_button.c
        ${LUA_PORT_DIR}/modules/lua_ui_slider.c
        ${LUA_PORT_DIR}/modules/lua_ui_image.c
)

target_include_directories(lua_port PUBLIC
        ${LUA_PORT_DIR}
        ${LUA_PORT_DIR}/modules
        ${CART_DIR}
        ${DRIVER_DIR}/SDRAM
        ${FATFS_INCLUDES}
)
target_link_libraries(lua_port PUBLIC cartdesk_common lua lvgl cart_core drv_gpio mem_layout)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(lua_port PRIVATE XHGC_RESOURCE_ARENA_OWNER_DEBUG_LOG=1)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_target(check_lua_allocator_usage ALL
            COMMAND ${CMAKE_COMMAND}
                    -DPROJECT_ROOT=${PROJECT_ROOT}
                    -P ${PROJECT_ROOT}/cmake/check_lua_allocator_usage.cmake
            COMMENT "Checking Lua VM allocator entry usage"
            VERBATIM
    )
    add_dependencies(lua_port check_lua_allocator_usage)
endif()

if(XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE)
    target_compile_definitions(lua_port PRIVATE XHGC_ENABLE_EXPERIMENTAL_CART_RESOURCE_CACHE=1)
endif()

# ==============================================================================
# 目标 9: cart_core —— cart.bin 格式读取
# ==============================================================================
add_library(cart_core STATIC
        ${CART_DIR}/cart_bin.c
        ${CART_DIR}/cart_bin.h
        ${CART_DIR}/cart_index.c
        ${CART_DIR}/cart_index.h
        ${CART_DIR}/xhgc_cart.c
        ${CART_DIR}/xhgc_cart.h
)

target_include_directories(cart_core
        PUBLIC
        ${CART_DIR}
        ${FATFS_INCLUDES}
)
target_link_libraries(cart_core
        PUBLIC cartdesk_common drv_storage
)

option(CARTDESK_BUILD_HOST_TESTS "Build host-side parser tests with a native toolchain" OFF)
if(CARTDESK_BUILD_HOST_TESTS)
    add_executable(xhgc_cart_host_test
            ${PROJECT_ROOT}/tests/xhgc_cart_host_test.c
            ${CART_DIR}/xhgc_cart.c
            ${CART_DIR}/xhgc_cart.h
    )
    target_compile_definitions(xhgc_cart_host_test PRIVATE
            XHGC_CART_NO_FATFS=1
            XHGC_CART_NO_FLASH=1
            XHGC_CART_NO_HW_CRC=1
    )
    target_include_directories(xhgc_cart_host_test PRIVATE ${CART_DIR})
endif()

# ==============================================================================
# 目标 10: app_screen —— 应用层 UI 界面（启动器 / 缓存 / 预览）
# ==============================================================================
add_library(app_screen STATIC
        ${SCREEN_PAGE_DIR}/launcher_action_hints.c
        ${SCREEN_PAGE_DIR}/launcher_action_hints.h
        ${SCREEN_PAGE_DIR}/ui_screen_launcher.c
        ${SCREEN_PAGE_DIR}/ui_screen_launcher.h
        ${SCREEN_PAGE_DIR}/jbm_el_20px.h
        ${SCREEN_PAGE_DIR}/ui_launcher_cache.c
        ${SCREEN_PAGE_DIR}/ui_launcher_cache.h
        ${SCREEN_UTILS_DIR}/preview_argb_current.c
        ${SCREEN_UTILS_DIR}/preview_argb_current.h
)

target_include_directories(app_screen PUBLIC
        ${SCREEN_PAGE_DIR}
        ${SCREEN_UTILS_DIR}
        ${TASK_DIR}
        ${DRIVER_DIR}/LCD
)
target_link_libraries(app_screen PUBLIC cartdesk_common lvgl drv_storage cart_core)

# ==============================================================================
# 目标 11: app_task —— RTOS / 裸机任务（LED / LVGL / LUA 任务）
# ==============================================================================
add_library(app_task STATIC
        ${TASK_DIR}/cartdesk_task.h
        ${TASK_DIR}/LED.c
        ${TASK_DIR}/LVGL.c
        ${TASK_DIR}/LUA.c
)

target_include_directories(app_task PUBLIC ${TASK_DIR})
target_link_libraries(app_task PUBLIC cartdesk_common lvgl lua_port)

# ==============================================================================
# 目标 12: board_test —— 板级 bring-up / smoke test 入口（默认不在 main 中执行）
# ==============================================================================
add_library(board_test STATIC
        ${DEBUG_DIR}/board_test.c
        ${DEBUG_DIR}/board_test.h
)

target_include_directories(board_test PUBLIC
        ${DEBUG_DIR}
        ${DRIVER_DIR}/FLASH
        ${DRIVER_DIR}/LCD
        ${DRIVER_DIR}/TOUCH
)
target_link_libraries(board_test PUBLIC cartdesk_common lvgl drv_display drv_storage)

# ==============================================================================
# 主可执行目标：链接所有子目标
# ==============================================================================
add_executable(${CMAKE_PROJECT_NAME})

# 添加 STM32CubeMX 生成的源（main.c / 外设初始化 / 启动文件等）
add_subdirectory(cmake/stm32cubemx)

# 去掉 CMake 自动添加的 libob.a（使用 C++ 时的遗留问题）
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

target_link_libraries(${CMAKE_PROJECT_NAME}
        PRIVATE
        cartdesk_common
        stm32cubemx
        # 用 --start-group/--end-group 让链接器对组内静态库多遍扫描，
        # 彻底解决库之间的循环引用问题（如 lua 依赖 m，lvgl 依赖 m 等）。
        -Wl,--start-group
        drv_display
        drv_storage
        drv_misc
        drv_gpio
        mem_layout
        lua_port
        app_screen
        app_task
        board_test
        freertos_kernel
        lvgl
        lua
        -Wl,--end-group
        m
)

if(Python3_Interpreter_FOUND)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${Python3_EXECUTABLE}
                    ${PROJECT_ROOT}/cmake/print_sdram_usage.py
                    --map $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.map
            COMMENT "Printing SDRAM usage summary"
            VERBATIM
    )
else()
    message(WARNING "Python3 not found; SDRAM usage summary will not be printed after build")
endif()
