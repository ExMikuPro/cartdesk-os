# QFLASH A8 font pack generation and GDB/OpenOCD programming targets.

set(QFLASH_FONT_SOURCE
        "${PROJECT_ROOT}/Font/霞鹜臻楷.ttf"
        CACHE FILEPATH "Source TTF/OTF used to build the QFLASH font pack")
set(QFLASH_FONT_PACK
        "${PROJECT_ROOT}/Font/霞鹜臻楷-16-20-24-a8.qfnt"
        CACHE FILEPATH "Generated QFNT font pack")
set(QFLASH_FONT_OPENOCD_CONFIG
        "${PROJECT_ROOT}/CartDeck.cfg"
        CACHE FILEPATH "OpenOCD configuration used by flash_qflash_font")

# GDB calls these entry points by symbol name; keep them despite --gc-sections.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        -Wl,-u,QFlashFont_ProgramBegin
        -Wl,-u,QFlashFont_ProgramBufferAddress
        -Wl,-u,QFlashFont_ProgramBlock
        -Wl,-u,QFlashFont_ProgramFinish
)

find_program(QFLASH_FONT_OPENOCD_EXECUTABLE NAMES openocd)
find_program(QFLASH_FONT_GDB_EXECUTABLE NAMES arm-none-eabi-gdb)

if(NOT Python3_Interpreter_FOUND)
    message(WARNING "Python3 not found; QFLASH font CMake targets are unavailable")
    return()
endif()

add_custom_command(
        OUTPUT "${QFLASH_FONT_PACK}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_ROOT}/tools/qflash_font/build_qflash_font.py"
                build
                "${QFLASH_FONT_SOURCE}"
                "${QFLASH_FONT_PACK}"
                --sizes 16 20 24
        DEPENDS
                "${QFLASH_FONT_SOURCE}"
                "${PROJECT_ROOT}/tools/qflash_font/build_qflash_font.py"
        COMMENT "Building QFLASH A8 font pack"
        VERBATIM
)

add_custom_target(qflash_font_pack DEPENDS "${QFLASH_FONT_PACK}")

if(QFLASH_FONT_OPENOCD_EXECUTABLE AND QFLASH_FONT_GDB_EXECUTABLE)
    add_custom_target(flash_qflash_font
            COMMAND "${Python3_EXECUTABLE}"
                    "${PROJECT_ROOT}/tools/qflash_font/flash_qflash_font.py"
                    --elf "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                    --font-pack "${QFLASH_FONT_PACK}"
                    --openocd "${QFLASH_FONT_OPENOCD_EXECUTABLE}"
                    --gdb "${QFLASH_FONT_GDB_EXECUTABLE}"
                    --openocd-config "${QFLASH_FONT_OPENOCD_CONFIG}"
            DEPENDS
                    ${CMAKE_PROJECT_NAME}
                    qflash_font_pack
            COMMENT "Programming and verifying QFLASH font through GDB/OpenOCD"
            USES_TERMINAL
            VERBATIM
    )
else()
    message(WARNING
            "openocd or arm-none-eabi-gdb not found; flash_qflash_font target is unavailable")
endif()
