# Optional USB Mass Storage transport backed by the board SD card.
# The normal firmware remains CDC-only; the dedicated preset enables these
# sources so USB can be switched from CDC to MSC explicitly from Launcher.
option(CARTDESK_USB_SD_MSC_ENABLE
       "Enable Launcher-controlled USB MSC access to the board SD card" OFF)

set(CARTDESK_USB_MSC_INCLUDE_DIR
    ${PROJECT_ROOT}/Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc)
set(CARTDESK_USB_MSC_SOURCES
    ${PROJECT_ROOT}/Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc.c
    ${PROJECT_ROOT}/Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc_bot.c
    ${PROJECT_ROOT}/Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc_data.c
    ${PROJECT_ROOT}/Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Src/usbd_msc_scsi.c)
set(CARTDESK_USB_MSC_APP_SOURCES
    ${PROJECT_ROOT}/USB_DEVICE/App/usbd_storage_if.c
    ${PROJECT_ROOT}/USB_DEVICE/App/usbd_storage_if.h)

if(CARTDESK_USB_SD_MSC_ENABLE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING
        "USB SD MSC is intended for the Debug-USB-SD-MSC preset; "
        "the selected build type is ${CMAKE_BUILD_TYPE}")
endif()
