cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME xih6-display)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME}
        Core/Driver/SDRAM/sdram.h
        Core/Driver/SDRAM/sdram.c
        Core/Driver/LCD/lcd.c
        Core/Driver/LCD/lcd.h
        Core/Inc/lua_vm.h
        Core/Src/lua_vm.c
        Core/LuaPort/lua_port.c
        Core/LuaPort/modules/lua_gpio.c
        Core/LuaPort/modules/lua_delay.c
        Core/LuaPort/modules/lua_tim.c
        Core/LuaPort/modules/Lua_sd.c
        Core/Driver/CRC/crc.h
        Core/Screen/Page/ui_screen_launcher.c
        Core/Screen/Page/ui_screen_launcher.h
        Core/Driver/LCD/jbmono_thin_a8_20px.h
        Core/Screen/Page/jbm_el_20px.h
        Core/Driver/FLASH/flash.c
        Core/Driver/FLASH/flash.h
        Core/Driver/FLASH/lfs.c
        Core/Driver/FLASH/lfs.h
        Core/Driver/FLASH/lfs_util.c
        Core/Driver/FLASH/lfs_util.h
        Core/Driver/FLASH/lfs_port.c
        Core/Driver/FLASH/lfs_port.h
        Core/Driver/EEPROM/eeprom.c
        Core/Driver/EEPROM/eeprom.h
        Core/Driver/UID/uid.c
        Core/Driver/UID/uid.h
        Core/Driver/RNG/rng_port.c
        Core/Driver/RNG/rng_port.h
        Core/APPS/LVGL/port/lv_port_disp.c
        Core/APPS/LVGL/port/lv_port_disp.h
        Core/APPS/LVGL/port/lv_port_tick.c
        Core/APPS/LVGL/port/lv_port_tick.h
        Core/APPS/LVGL/port/lv_port_indev.c
        Core/APPS/LVGL/port/lv_port_indev.h
        Core/APPS/LVGL/port/lvgl_init.c
        Core/APPS/LVGL/port/lvgl_init.h
        Core/APPS/LVGL/lv_conf.h
        Core/APPS/TASK/Task.h
        Core/APPS/TASK/LED.c
        Core/APPS/TASK/LVGL.c
        Core/APPS/TASK/LUA.c
        Core/LuaPort/modules/lua_ui.c

)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)


# ===== Add Lua (minimal) =====
set(LUA_SRC_DIR ${CMAKE_SOURCE_DIR} Core/LuaPort/src)

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

        ${LUA_SRC_DIR}/lauxlib.c

        ${LUA_SRC_DIR}/lbaselib.c
        ${LUA_SRC_DIR}/lcorolib.c
        ${LUA_SRC_DIR}/lmathlib.c
        ${LUA_SRC_DIR}/lstrlib.c
#        ${LUA_SRC_DIR}/lstrlib.c
        ${LUA_SRC_DIR}/ltablib.c
        ${LUA_SRC_DIR}/lutf8lib.c

)

# ===== LVGL v9.x =====
set(LVGL_DIR ${CMAKE_SOURCE_DIR}/Core/APPS/LVGL)

file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS
        ${LVGL_DIR}/src/*.c
        ${LVGL_DIR}/demos/widgets/*.c
)

add_library(lvgl STATIC ${LVGL_SOURCES})

target_include_directories(lvgl PUBLIC
        ${LVGL_DIR}
        ${LVGL_DIR}/src
        ${LVGL_DIR}/port

        ${CMAKE_SOURCE_DIR}/Core/Inc

        ${CMAKE_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
        ${CMAKE_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy

        ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
        ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
)

# 让 LVGL 使用你的 lv_conf.h（放在 LVGL_DIR 或 LVGL_DIR/port 里都行）
target_compile_definitions(lvgl PUBLIC
        LV_CONF_INCLUDE_SIMPLE=1
        STM32H743xx
)


# 去重：上面我写了两次 lstrlib.c，你删掉其中一个（保留一个即可）

target_compile_options(lua PRIVATE -ffunction-sections -fdata-sections)

target_include_directories(lua PUBLIC ${LUA_SRC_DIR})


# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
        Core/Driver/TOUCH/touch.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
        Core/Inc
        Core/LuaPort
        Core/LuaPort/modules
        Core/Driver/
        Core/Driver/TOUCH
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
        lvgl
    # Add user defined libraries
    lua
    m
)
