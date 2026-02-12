/**
 * @file touch_advanced.c
 * @brief GT911 Advanced Features and Examples
 * @note  Optional enhancements for production use
 */

#include "touch.h"
#include <stdio.h>

/* ============================================================================
 * 1. INTERRUPT MODE IMPLEMENTATION
 * ============================================================================ */

static volatile bool g_touch_irq_pending = false;

/**
 * @brief Enable GT911 interrupt mode
 */
void GT911_Enable_Interrupt(void)
{
    /* Configure NVIC for EXTI interrupt */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/**
 * @brief EXTI callback - called when touch interrupt occurs
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == TOUCH_INT_Pin) {
        g_touch_irq_pending = true;
    }
}

/**
 * @brief Check if touch interrupt is pending
 */
bool GT911_Is_IRQ_Pending(void)
{
    bool pending = g_touch_irq_pending;
    g_touch_irq_pending = false;
    return pending;
}

/* ============================================================================
 * 2. MULTI-TOUCH SUPPORT
 * ============================================================================ */

/**
 * @brief Process multi-touch gestures
 * @param touch_data Pointer to touch data
 * @return Detected gesture type
 */
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_SINGLE_TAP,
    GESTURE_DOUBLE_TAP,
    GESTURE_LONG_PRESS,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_PINCH_IN,
    GESTURE_PINCH_OUT
} TouchGesture_t;

typedef struct {
    uint32_t press_time;
    int16_t  start_x;
    int16_t  start_y;
    int16_t  last_x;
    int16_t  last_y;
    uint16_t initial_distance;  // For pinch gesture
} GestureState_t;

static GestureState_t g_gesture = {0};

TouchGesture_t GT911_Detect_Gesture(GT911_TouchData_t *touch_data)
{
    static uint32_t last_tap_time = 0;
    uint32_t current_time = HAL_GetTick();
    
    if (touch_data->touch_count == 0) {
        // Touch released
        if (g_gesture.press_time > 0) {
            uint32_t press_duration = current_time - g_gesture.press_time;
            
            // Long press detection
            if (press_duration > 1000) {
                g_gesture.press_time = 0;
                return GESTURE_LONG_PRESS;
            }
            
            // Swipe detection
            int16_t dx = g_gesture.last_x - g_gesture.start_x;
            int16_t dy = g_gesture.last_y - g_gesture.start_y;
            
            if (abs(dx) > 50 || abs(dy) > 50) {
                g_gesture.press_time = 0;
                
                if (abs(dx) > abs(dy)) {
                    return (dx > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
                } else {
                    return (dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
                }
            }
            
            // Tap detection
            if (press_duration < 200) {
                if ((current_time - last_tap_time) < 300) {
                    last_tap_time = 0;
                    g_gesture.press_time = 0;
                    return GESTURE_DOUBLE_TAP;
                } else {
                    last_tap_time = current_time;
                    g_gesture.press_time = 0;
                    return GESTURE_SINGLE_TAP;
                }
            }
            
            g_gesture.press_time = 0;
        }
    } else if (touch_data->touch_count == 1) {
        // Single touch
        if (g_gesture.press_time == 0) {
            g_gesture.press_time = current_time;
            g_gesture.start_x = touch_data->points[0].x;
            g_gesture.start_y = touch_data->points[0].y;
        }
        g_gesture.last_x = touch_data->points[0].x;
        g_gesture.last_y = touch_data->points[0].y;
    } else if (touch_data->touch_count == 2) {
        // Two-finger pinch gesture
        int16_t dx = touch_data->points[0].x - touch_data->points[1].x;
        int16_t dy = touch_data->points[0].y - touch_data->points[1].y;
        uint16_t distance = sqrt(dx*dx + dy*dy);
        
        if (g_gesture.initial_distance == 0) {
            g_gesture.initial_distance = distance;
        } else {
            if (distance > g_gesture.initial_distance + 20) {
                g_gesture.initial_distance = 0;
                return GESTURE_PINCH_OUT;
            } else if (distance < g_gesture.initial_distance - 20) {
                g_gesture.initial_distance = 0;
                return GESTURE_PINCH_IN;
            }
        }
    }
    
    return GESTURE_NONE;
}

/* ============================================================================
 * 3. CALIBRATION SUPPORT
 * ============================================================================ */

typedef struct {
    int16_t offset_x;
    int16_t offset_y;
    float   scale_x;
    float   scale_y;
    bool    calibrated;
} TouchCalibration_t;

static TouchCalibration_t g_calibration = {
    .offset_x = 0,
    .offset_y = 0,
    .scale_x = 1.0f,
    .scale_y = 1.0f,
    .calibrated = false
};

/**
 * @brief Apply calibration to raw coordinates
 */
void GT911_Apply_Calibration(int16_t *x, int16_t *y)
{
    if (g_calibration.calibrated) {
        *x = (*x - g_calibration.offset_x) * g_calibration.scale_x;
        *y = (*y - g_calibration.offset_y) * g_calibration.scale_y;
    }
}

/**
 * @brief Perform 3-point calibration
 * @param ref_points  Reference points on screen
 * @param touch_points Touched points by user
 */
void GT911_Calibrate(const int16_t ref_points[3][2], const int16_t touch_points[3][2])
{
    // Simple linear calibration using first and last points
    g_calibration.offset_x = touch_points[0][0] - ref_points[0][0];
    g_calibration.offset_y = touch_points[0][1] - ref_points[0][1];
    
    int16_t ref_dx = ref_points[2][0] - ref_points[0][0];
    int16_t ref_dy = ref_points[2][1] - ref_points[0][1];
    int16_t touch_dx = touch_points[2][0] - touch_points[0][0];
    int16_t touch_dy = touch_points[2][1] - touch_points[0][1];
    
    g_calibration.scale_x = (float)ref_dx / touch_dx;
    g_calibration.scale_y = (float)ref_dy / touch_dy;
    g_calibration.calibrated = true;
}

/* ============================================================================
 * 4. POWER MANAGEMENT
 * ============================================================================ */

/**
 * @brief Put GT911 into sleep mode
 */
void GT911_Sleep(void)
{
    uint8_t cmd = 0x05;
    GT911_I2C_Write(GT911_REG_CTRL, &cmd, 1);
}

/**
 * @brief Wake up GT911 from sleep
 */
void GT911_Wakeup(void)
{
    // Hardware reset to wake up
    HAL_GPIO_WritePin(TOUCH_INT_GPIO_Port, TOUCH_INT_Pin, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(TOUCH_INT_GPIO_Port, TOUCH_INT_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
}

/* ============================================================================
 * 5. DIAGNOSTICS AND DEBUG
 * ============================================================================ */

/**
 * @brief Print GT911 configuration and status
 */
void GT911_Debug_Print(void)
{
    uint8_t product_id[4] = {0};
    uint16_t fw_version = 0;
    uint8_t status = 0;
    
    printf("\n=== GT911 Touch Controller Debug Info ===\n");
    
    // Product ID
    if (GT911_Read_ProductID(product_id)) {
        printf("Product ID: %c%c%c (0x%02X)\n", 
               product_id[0], product_id[1], product_id[2], product_id[3]);
    } else {
        printf("Failed to read Product ID\n");
    }
    
    // Firmware version
    if (GT911_Read_FirmwareVersion(&fw_version)) {
        printf("Firmware Version: 0x%04X\n", fw_version);
    } else {
        printf("Failed to read Firmware Version\n");
    }
    
    // Current status
    if (GT911_Read_Status(&status)) {
        printf("Status Register: 0x%02X\n", status);
        printf("  Buffer Ready: %s\n", (status & 0x80) ? "Yes" : "No");
        printf("  Touch Points: %d\n", status & 0x0F);
    } else {
        printf("Failed to read Status\n");
    }
    
    printf("==========================================\n\n");
}

/**
 * @brief Test GT911 I2C communication
 */
bool GT911_Test_Communication(void)
{
    uint8_t product_id[4];
    
    // Try to read product ID
    for (int retry = 0; retry < 5; retry++) {
        if (GT911_Read_ProductID(product_id)) {
            if (product_id[0] == '9' && product_id[1] == '1' && product_id[2] == '1') {
                printf("✓ GT911 communication OK\n");
                return true;
            }
        }
        HAL_Delay(10);
    }
    
    printf("✗ GT911 communication failed\n");
    return false;
}

/**
 * @brief Monitor touch data in real-time (for debugging)
 */
void GT911_Monitor_Touch(uint32_t duration_ms)
{
    uint32_t start_time = HAL_GetTick();
    
    printf("Monitoring touch for %lu ms...\n", duration_ms);
    
    while ((HAL_GetTick() - start_time) < duration_ms) {
        if (GT911_Scan()) {
            uint8_t num = GT911_Get_TouchNum();
            printf("[%lu] Touch points: %d ", HAL_GetTick(), num);
            
            for (uint8_t i = 0; i < num; i++) {
                uint16_t x, y;
                if (GT911_Get_Point(i, &x, &y)) {
                    printf("| Point %d: (%d, %d) ", i, x, y);
                }
            }
            printf("\n");
        }
        HAL_Delay(10);
    }
    
    printf("Monitoring complete.\n");
}

/* ============================================================================
 * 6. ERROR RECOVERY
 * ============================================================================ */

static uint32_t g_error_count = 0;

/**
 * @brief Handle I2C errors with recovery
 */
bool GT911_I2C_Read_WithRecovery(uint16_t reg_addr, uint8_t *buf, uint16_t len)
{
    #define MAX_RETRIES 3
    
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        if (GT911_I2C_Read(reg_addr, buf, len)) {
            g_error_count = 0;
            return true;
        }
        
        g_error_count++;
        
        // If multiple failures, try hardware reset
        if (g_error_count > 10) {
            printf("Too many I2C errors, resetting GT911...\n");
            GT911_Init();
            g_error_count = 0;
        }
        
        HAL_Delay(1);
    }
    
    return false;
}

/**
 * @brief Get error statistics
 */
uint32_t GT911_Get_ErrorCount(void)
{
    return g_error_count;
}
