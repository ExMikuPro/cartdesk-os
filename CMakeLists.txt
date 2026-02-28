cmake_minimum_required(VERSION 3.22)

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME cartdeck-os)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# ==============================================================================
# 公共宏定义 & 公共头文件路径（所有驱动/应用目标共用）
# ==============================================================================
set(COMMON_DEFINES
        LV_CONF_INCLUDE_SIMPLE=1
        STM32H743xx
)

# STM32 HAL + CMSIS 路径，所有依赖 HAL 的子目标都需要它
set(STM32_HAL_INCLUDES
        ${CMAKE_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
        ${CMAKE_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
        ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
        ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
)

# ==============================================================================
# 目标 1: lua —— Lua 5.x 解释器核心
# ==============================================================================
set(LUA_SRC_DIR ${CMAKE_SOURCE_DIR}/Core/LuaPort/src)

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

# ==============================================================================
# 目标 3: drv_display —— 显示相关硬件驱动（LCD / SDRAM / LVGL Port）
# ==============================================================================
add_library(drv_display STATIC
        # LCD 驱动
        Core/Driver/LCD/lcd.c
        Core/Driver/LCD/lcd.h
        # 触摸驱动
        Core/Driver/TOUCH/touch.c
)

target_include_directories(drv_display PUBLIC
        Core/Inc
        Core/Driver/LCD
        Core/Driver/SDRAM
        Core/Driver/TOUCH
        ${STM32_HAL_INCLUDES}
)

target_compile_definitions(drv_display PUBLIC ${COMMON_DEFINES})

# ==============================================================================
# 目标 4: drv_storage —— 存储相关驱动（Flash/LittleFS / EEPROM / SD）
# ==============================================================================
add_library(drv_storage STATIC
        # SPI NOR Flash + LittleFS
        Core/Driver/FLASH/flash.c
        Core/Driver/FLASH/flash.h
        Core/Driver/FLASH/lfs.c
        Core/Driver/FLASH/lfs.h
        Core/Driver/FLASH/lfs_util.c
        Core/Driver/FLASH/lfs_util.h
        Core/Driver/FLASH/lfs_port.c
        Core/Driver/FLASH/lfs_port.h
        # EEPROM
        Core/Driver/EEPROM/eeprom.c
        Core/Driver/EEPROM/eeprom.h
)

target_include_directories(drv_storage PUBLIC
        Core/Inc
        Core/Driver/FLASH
        Core/Driver/EEPROM
        ${STM32_HAL_INCLUDES}
)

target_compile_definitions(drv_storage PUBLIC ${COMMON_DEFINES})

# ==============================================================================
# 目标 5: drv_misc —— 其他外设驱动（UID / RNG / CRC）
# ==============================================================================
add_library(drv_misc STATIC
        Core/Driver/UID/uid.c
        Core/Driver/UID/uid.h
        Core/Driver/RNG/rng_port.c
        Core/Driver/RNG/rng_port.h
        Core/Driver/SDRAM/sdram.c
)

target_include_directories(drv_misc PUBLIC
        Core/Inc
        Core/Driver/UID
        Core/Driver/RNG
        Core/Driver/CRC
        ${STM32_HAL_INCLUDES}
)

target_compile_definitions(drv_misc PUBLIC ${COMMON_DEFINES})

# ==============================================================================
# 目标 6: lua_port —— Lua 与硬件的绑定层（C 模块 + VM 封装）
# ==============================================================================
add_library(lua_port STATIC
        # VM 封装
        Core/Inc/lua_vm.h
        Core/Src/lua_vm.c
        # 移植入口
        Core/LuaPort/lua_port.c
        # 硬件模块绑定
        Core/LuaPort/modules/lua_gpio.c
        Core/LuaPort/modules/lua_delay.c
        Core/LuaPort/modules/lua_tim.c
        Core/LuaPort/modules/Lua_sd.c
)

target_include_directories(lua_port PUBLIC
        Core/Inc
        Core/LuaPort
        Core/LuaPort/modules
        ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FatFs/src
        ${CMAKE_SOURCE_DIR}/FATFS/Target
        ${CMAKE_SOURCE_DIR}/FATFS/App
        ${STM32_HAL_INCLUDES}
)

target_compile_definitions(lua_port PUBLIC ${COMMON_DEFINES})
target_link_libraries(lua_port PUBLIC lua)

# ==============================================================================
# 目标 7: app_screen —— 应用层 UI 界面（启动器 / 缓存 / 预览）
# ==============================================================================



# ==============================================================================
# 目标 8: app_task —— RTOS / 裸机任务（LED / LVGL / LUA 任务）
# ==============================================================================
add_library(app_task STATIC
        Core/APPS/TASK/Task.h
        Core/APPS/TASK/LED.c
        Core/APPS/TASK/LUA.c
)

target_include_directories(app_task PUBLIC
        Core/Inc
        Core/APPS/TASK
        ${STM32_HAL_INCLUDES}
)

target_compile_definitions(app_task PUBLIC ${COMMON_DEFINES})
target_link_libraries(app_task PUBLIC lua_port)

# ==============================================================================
# 主可执行目标：链接所有子目标
# ==============================================================================
add_executable(${CMAKE_PROJECT_NAME})

# 添加 STM32CubeMX 生成的源（main.c / 外设初始化 / 启动文件等）
add_subdirectory(cmake/stm32cubemx)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        Core/Inc
        Core/Driver/
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ${COMMON_DEFINES})

# 去掉 CMake 自动添加的 libob.a（使用 C++ 时的遗留问题）
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

target_link_libraries(${CMAKE_PROJECT_NAME}
        stm32cubemx      # CubeMX 生成的 HAL / BSP
        # 用 --start-group/--end-group 让链接器对组内静态库多遍扫描，
        # 彻底解决库之间的循环引用问题（如 lua 依赖 m，lvgl 依赖 m 等）
        -Wl,--start-group
        drv_display
        drv_storage
        drv_misc
        lua_port
        app_task
        lua
        -Wl,--end-group
        m                # libm 放在 group 外且最后，保证所有库的数学符号都能被解析
)