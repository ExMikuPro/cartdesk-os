/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "Core/APPS/LVGL/lvgl.h"

/*********************
 *      DEFINES
 *********************/
/* 触摸屏功能开关 - 设置为0禁用触摸屏 */
#define TOUCHSCREEN_ENABLED  0

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
#if TOUCHSCREEN_ENABLED
static void touchpad_init(void);
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data);
static bool touchpad_is_pressed(void);
static void touchpad_get_xy(int16_t * x, int16_t * y);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
#if TOUCHSCREEN_ENABLED
static lv_indev_t * indev_touchpad;
#endif

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
#if TOUCHSCREEN_ENABLED
    /*------------------
     * Touchpad
     * -----------------*/

    /*Initialize your touchpad */
    touchpad_init();

    /*Register a touchpad input device*/
    indev_touchpad = lv_indev_create();
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
#endif
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#if TOUCHSCREEN_ENABLED

/*Initialize your touchpad*/
static void touchpad_init(void)
{
    /* TODO: 初始化触摸屏硬件
     *
     * 示例 - 电容触摸屏(I2C接口如GT911/FT6236):
     * - 初始化I2C接口
     * - 配置触摸芯片中断引脚
     * - 读取触摸芯片ID验证通信
     *
     * 示例 - 电阻触摸屏(XPT2046等):
     * - 初始化SPI接口
     * - 配置触摸笔中断引脚
     *
     * 常见触摸芯片初始化代码:
     * GT911_Init();
     * FT6236_Init();
     * XPT2046_Init();
     */
}

/*Will be called by the library to read the touchpad*/
static void touchpad_read(lv_indev_t * indev_drv, lv_indev_data_t * data)
{
    static int16_t last_x = 0;
    static int16_t last_y = 0;

    /*Save the pressed coordinates and the state*/
    if(touchpad_is_pressed()) {
        touchpad_get_xy(&last_x, &last_y);
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /*Set the last pressed coordinates*/
    data->point.x = last_x;
    data->point.y = last_y;
}

/*Return true if the touchpad is pressed*/
static bool touchpad_is_pressed(void)
{
    /* TODO: 检测触摸屏是否被按下
     *
     * 方法1: 读取中断引脚状态
     * return (HAL_GPIO_ReadPin(TOUCH_INT_GPIO_Port, TOUCH_INT_Pin) == GPIO_PIN_RESET);
     *
     * 方法2: 读取触摸芯片寄存器
     * return GT911_Scan();
     * return FT6236_Scan();
     *
     * 方法3: 电阻屏读取ADC值判断
     * return XPT2046_GetTouch();
     */
    return false;
}

/*Get the x and y coordinates if the touchpad is pressed*/
static void touchpad_get_xy(int16_t * x, int16_t * y)
{
    /* TODO: 读取触摸坐标
     *
     * 电容屏示例:
     * GT911_Read_XY(x, y);
     * FT6236_Read_XY(x, y);
     *
     * 电阻屏示例:
     * XPT2046_Read_XY(x, y);
     *
     * 注意: 根据屏幕方向可能需要坐标转换
     * 横屏/竖屏转换:
     * int16_t temp = *x;
     * *x = *y;
     * *y = MY_DISP_VER_RES - temp;
     *
     * 镜像翻转:
     * *x = MY_DISP_HOR_RES - *x;
     * *y = MY_DISP_VER_RES - *y;
     */

    /* 临时返回屏幕中心坐标 */
    *x = 0;
    *y = 0;
}

#endif  /* TOUCHSCREEN_ENABLED */