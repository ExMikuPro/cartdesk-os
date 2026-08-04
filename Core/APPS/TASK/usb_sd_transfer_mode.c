#include "usb_sd_transfer_mode.h"

#include "cart_io_service.h"
#include "fatfs.h"
#include "usb_device.h"

static const char *s_last_error = "none";

bool UsbSdTransferMode_IsAvailable(void)
{
#if CARTDESK_USB_SD_MSC_ENABLE
    return true;
#else
    return false;
#endif
}

bool UsbSdTransferMode_IsActive(void)
{
#if CARTDESK_USB_SD_MSC_ENABLE
    return USB_Device_IsSdMscMode();
#else
    return false;
#endif
}

bool UsbSdTransferMode_Enter(void)
{
#if CARTDESK_USB_SD_MSC_ENABLE
    if (USB_Device_IsSdMscMode()) return true;
    if (!CartIoService_BeginSdExclusive()) {
        s_last_error = "SD is busy; retry after the current card operation";
        return false;
    }
    if (SD_FATFS_Unmount() != FR_OK) {
        CartIoService_EndSdExclusive();
        s_last_error = "FatFs unmount failed";
        return false;
    }
    if (!USB_Device_SetSdMscMode(true)) {
        CartIoService_EndSdExclusive();
        SD_FATFS_InvalidateMount();
        s_last_error = "USB MSC initialization failed";
        return false;
    }
    s_last_error = "none";
    return true;
#else
    s_last_error = "USB SD MSC is disabled in this build";
    return false;
#endif
}

bool UsbSdTransferMode_Exit(void)
{
#if CARTDESK_USB_SD_MSC_ENABLE
    if (!USB_Device_IsSdMscMode()) return true;
    if (!USB_Device_SetSdMscMode(false)) {
        s_last_error = "USB CDC restore failed";
        return false;
    }
    if (SD_FATFS_Reinitialize() != FR_OK) {
        CartIoService_EndSdExclusive();
        s_last_error = "SD reinitialization failed";
        return false;
    }
    CartIoService_EndSdExclusive();
    s_last_error = "none";
    return true;
#else
    s_last_error = "USB SD MSC is disabled in this build";
    return false;
#endif
}

const char *UsbSdTransferMode_GetLastError(void)
{
    return s_last_error;
}
