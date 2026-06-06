cmake_minimum_required(VERSION 3.22)

enable_language(C ASM)

get_filename_component(CUBEMX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# STM32CubeMX generated symbols (macros)
set(MX_Defines_Syms
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H743xx
    $<$<CONFIG:Debug>:DEBUG>
)

# STM32CubeMX generated include paths
set(MX_Include_Dirs
    ${CUBEMX_ROOT}/Core/Inc
    ${CUBEMX_ROOT}/FATFS/Target
    ${CUBEMX_ROOT}/FATFS/App
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${CUBEMX_ROOT}/Middlewares/Third_Party/FatFs/src
    ${CUBEMX_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/include
    ${CUBEMX_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
    ${CUBEMX_ROOT}/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/include
    ${CUBEMX_ROOT}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CUBEMX_ROOT}/Drivers/CMSIS/Include
    ${CUBEMX_ROOT}/Drivers/CMSIS/RTOS2/Include
)

# STM32CubeMX generated application sources
set(MX_Application_Src
    ${CUBEMX_ROOT}/FATFS/Target/bsp_driver_sd.c
    ${CUBEMX_ROOT}/FATFS/Target/sd_diskio.c
    ${CUBEMX_ROOT}/FATFS/App/fatfs.c
    ${CUBEMX_ROOT}/Core/Src/main.c
    ${CUBEMX_ROOT}/Core/Src/gpio.c
    ${CUBEMX_ROOT}/Core/Src/crc.c
    ${CUBEMX_ROOT}/Core/Src/dma2d.c
    ${CUBEMX_ROOT}/Core/Src/fmc.c
    ${CUBEMX_ROOT}/Core/Src/i2c.c
    ${CUBEMX_ROOT}/Core/Src/ltdc.c
    ${CUBEMX_ROOT}/Core/Src/mdma.c
    ${CUBEMX_ROOT}/Core/Src/quadspi.c
    ${CUBEMX_ROOT}/Core/Src/rng.c
    ${CUBEMX_ROOT}/Core/Src/sdmmc.c
    ${CUBEMX_ROOT}/Core/Src/tim.c
    ${CUBEMX_ROOT}/Core/Src/usart.c
    ${CUBEMX_ROOT}/Core/Src/stm32h7xx_it.c
    ${CUBEMX_ROOT}/Core/Src/stm32h7xx_hal_msp.c
    ${CUBEMX_ROOT}/Core/Src/freertos_hooks.c
    ${CUBEMX_ROOT}/Core/Src/sysmem.c
    ${CUBEMX_ROOT}/Core/Src/syscalls.c
    ${CUBEMX_ROOT}/startup_stm32h743xx.s
)

# STM32 HAL/LL Drivers
set(STM32_Drivers_Src
    ${CUBEMX_ROOT}/Core/Src/system_stm32h7xx.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_crc.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_crc_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma2d.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dsi.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mmc.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mmc_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_nand.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_nor.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_qspi.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rng.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rng_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sd.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sd_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sdram.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sram.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_delayblock.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_fmc.c
    ${CUBEMX_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_sdmmc.c
)

# FatFs middleware
set(FatFs_Src
    ${CUBEMX_ROOT}/Middlewares/Third_Party/FatFs/src/diskio.c
    ${CUBEMX_ROOT}/Middlewares/Third_Party/FatFs/src/ff.c
    ${CUBEMX_ROOT}/Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ${CUBEMX_ROOT}/Middlewares/Third_Party/FatFs/src/option/syscall.c
)

set(MX_LINK_DIRS)
set(MX_LINK_LIBS
    STM32_Drivers
    ${TOOLCHAIN_LINK_LIBRARIES}
    FatFs
)

# Interface library for CubeMX generated includes and symbols.
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})
target_link_libraries(stm32cubemx INTERFACE cartdesk_common)

add_library(STM32_Drivers OBJECT ${STM32_Drivers_Src})
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)

add_library(FatFs OBJECT ${FatFs_Src})
target_link_libraries(FatFs PUBLIC stm32cubemx)

# Add STM32CubeMX generated application sources to the project.
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MX_LINK_DIRS})
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${MX_LINK_LIBS})

set_target_properties(${CMAKE_PROJECT_NAME}
    PROPERTIES
        ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map
)

if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
