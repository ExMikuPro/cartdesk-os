#pragma once

#include <stdbool.h>

bool UsbSdTransferMode_IsAvailable(void);
bool UsbSdTransferMode_IsActive(void);
bool UsbSdTransferMode_Enter(void);
bool UsbSdTransferMode_Exit(void);
const char *UsbSdTransferMode_GetLastError(void);
