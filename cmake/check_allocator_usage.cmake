if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(GLOB_RECURSE allocator_check_files
        LIST_DIRECTORIES false
        "${PROJECT_ROOT}/Core/Src/*.c"
        "${PROJECT_ROOT}/Core/Src/*.h"
        "${PROJECT_ROOT}/Core/Inc/*.h"
        "${PROJECT_ROOT}/Core/Driver/*.c"
        "${PROJECT_ROOT}/Core/Driver/*.h"
        "${PROJECT_ROOT}/Core/LuaPort/*.c"
        "${PROJECT_ROOT}/Core/LuaPort/*.h"
        "${PROJECT_ROOT}/Core/Cart/*.c"
        "${PROJECT_ROOT}/Core/Cart/*.h"
        "${PROJECT_ROOT}/Core/Memory/*.c"
        "${PROJECT_ROOT}/Core/Memory/*.h"
        "${PROJECT_ROOT}/Core/APPS/TASK/*.c"
        "${PROJECT_ROOT}/Core/APPS/TASK/*.h"
        "${PROJECT_ROOT}/Core/Screen/*.c"
        "${PROJECT_ROOT}/Core/Screen/*.h"
        "${PROJECT_ROOT}/Drivers/*.c"
        "${PROJECT_ROOT}/Drivers/*.h"
        "${PROJECT_ROOT}/FATFS/App/*.c"
        "${PROJECT_ROOT}/FATFS/App/*.h"
        "${PROJECT_ROOT}/FATFS/Target/*.c"
        "${PROJECT_ROOT}/FATFS/Target/*.h"
)

set(allocator_errors "")

foreach(source_file IN LISTS allocator_check_files)
    if(source_file MATCHES "/Core/Src/sysmem\\.c$")
        continue()
    endif()
    if(source_file MATCHES "/Core/Driver/FLASH/lfs(_util)?\\.(c|h)$")
        continue()
    endif()
    if(source_file MATCHES "/Core/LuaPort/src/")
        continue()
    endif()
    if(source_file MATCHES "/Core/APPS/LVGL/")
        continue()
    endif()
    if(source_file MATCHES "/Drivers/(CMSIS|STM32H7xx_HAL_Driver)/")
        continue()
    endif()
    if(source_file MATCHES "/Middlewares/Third_Party/")
        continue()
    endif()
    if(source_file MATCHES "/tools/")
        continue()
    endif()
    if(source_file MATCHES "/build/")
        continue()
    endif()

    file(STRINGS "${source_file}" source_lines)
    set(line_number 0)

    foreach(source_line IN LISTS source_lines)
        math(EXPR line_number "${line_number} + 1")

        if(source_line MATCHES "XHGC_ALLOCATOR_ALLOW_NEWLIB")
            continue()
        endif()
        if(source_line MATCHES "^[ \t]*(//|/\\*|\\*)")
            continue()
        endif()

        string(REGEX MATCH "(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free)[ \t]*\\("
               allocator_match "${source_line}")
        if(allocator_match)
            list(APPEND allocator_errors
                 "${source_file}:${line_number}: direct newlib allocator use is not allowed in firmware business code")
        endif()
    endforeach()
endforeach()

if(allocator_errors)
    list(JOIN allocator_errors "\n" allocator_error_text)
    message(FATAL_ERROR
            "Allocator usage check failed:\n${allocator_error_text}\n"
            "Use the project allocator for the owning subsystem, or add a rare XHGC_ALLOCATOR_ALLOW_NEWLIB exception with a reason.")
endif()

message(STATUS "Allocator usage check passed")
