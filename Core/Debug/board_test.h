#pragma once

#include "flash.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CARTDESK_ENABLE_BOARD_TESTS
#define CARTDESK_ENABLE_BOARD_TESTS 0
#endif

void BoardTest_RunFlashStartupDiagnostics(FLASH_Handle *flash);
void BoardTest_FlashWriteSmoke(FLASH_Handle *flash);
void BoardTest_ReadGt814d(void);
void BoardTest_StartMovingBox(void);
void BoardTest_StartTouchDrag(void);

void ui_test_moving_box_start(void);
void ui_test_touch_drag_start(void);

#ifdef __cplusplus
}
#endif
