/**
 * @file lv_port_indev.c
 * @brief LVGL 9.x 输入设备移植层实现
 * @note  支持GT911多点触摸，中断驱动
 */

/*********************
 *      包含文件
 *********************/
#include "lv_port_indev.h"
#include "lvgl.h"
#include "runtime_stats.h"
#include "touch.h"

/*********************
 *      宏定义
 *********************/
/* 触摸屏功能开关 - 设置为0禁用触摸屏 */
#define TOUCHSCREEN_ENABLED     1

/* 多点触摸开关 - 设置为1启用多点触摸 */
#define MULTITOUCH_ENABLED      1

/* 屏幕分辨率 - 根据实际屏幕调整 */
#define DISP_HOR_RES            800
#define DISP_VER_RES            480

/* 坐标转换设置 */
#define TOUCH_SWAP_XY           0  // 横竖屏切换时设为1
#define TOUCH_INVERT_X          0  // X轴镜像
#define TOUCH_INVERT_Y          0  // Y轴镜像

/* 中断模式开关 */
#define USE_IRQ_MODE            1  // 使用中断模式，降低CPU占用

/**********************
 *      类型定义
 **********************/

/**********************
 *   静态函数声明
 **********************/
#if TOUCHSCREEN_ENABLED
static void touchpad_init(void);
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data);
static void touchpad_transform_xy(int16_t *x, int16_t *y);

#if USE_IRQ_MODE
static void touchpad_irq_callback(void);
#endif

#if MULTITOUCH_ENABLED
static void touchpad_read_multitouch(lv_indev_t * indev, lv_indev_data_t * data);
#endif
#endif

/**********************
 *   静态变量
 **********************/
#if TOUCHSCREEN_ENABLED
static lv_indev_t * indev_touchpad = NULL;
static bool touchpad_enabled = true;
static uint8_t touch_sensitivity = 5;

#if USE_IRQ_MODE
static volatile bool touch_data_ready = false;
#endif

#if MULTITOUCH_ENABLED
/* 多点触摸状态 */
static Touch_Point_t last_points[GT911_MAX_TOUCH_POINTS];
static uint8_t last_point_count = 0;
#endif
#endif

/**********************
 *      宏函数
 **********************/

/**********************
 *   全局函数实现
 **********************/

/**
 * @brief 初始化所有输入设备
 */
void lv_port_indev_init(void)
{
#if TOUCHSCREEN_ENABLED
    /* 初始化触摸屏硬件 */
    touchpad_init();

    /* 注册触摸屏输入设备 */
    indev_touchpad = lv_indev_create();
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);

#if MULTITOUCH_ENABLED
    /* 多点触摸模式 */
    lv_indev_set_read_cb(indev_touchpad, touchpad_read_multitouch);
#else
    /* 单点触摸模式 */
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);
#endif

#if USE_IRQ_MODE
    /* 启用中断模式 */
    Touch_EnableIRQ();
    Touch_RegisterCallback(touchpad_irq_callback);
#endif
#endif
}

/**
 * @brief 获取触摸屏输入设备对象
 * @return 触摸屏indev对象指针，如果禁用则返回NULL
 */
lv_indev_t * lv_port_indev_get_touchpad(void)
{
#if TOUCHSCREEN_ENABLED
    return indev_touchpad;
#else
    return NULL;
#endif
}

/**
 * @brief 启用/禁用触摸输入
 * @param enable true: 启用; false: 禁用
 */
void lv_port_indev_enable(bool enable)
{
#if TOUCHSCREEN_ENABLED
    touchpad_enabled = enable;

    if (enable) {
        Touch_EnableIRQ();
    } else {
        Touch_DisableIRQ();
    }
#endif
}

/**
 * @brief 设置触摸灵敏度
 * @param sensitivity 灵敏度值 (1-10, 默认5)
 */
void lv_port_indev_set_sensitivity(uint8_t sensitivity)
{
#if TOUCHSCREEN_ENABLED
    if (sensitivity >= 1 && sensitivity <= 10) {
        touch_sensitivity = sensitivity;
    }
#endif
}

/**********************
 *   静态函数实现
 **********************/

#if TOUCHSCREEN_ENABLED

/**
 * @brief 初始化触摸屏硬件
 */
static void touchpad_init(void)
{
    /* 初始化GT911触摸控制器 */
    Touch_Init();

    /* 打印调试信息 */
    Touch_DebugInfo();
}

#if USE_IRQ_MODE
/**
 * @brief 触摸中断回调函数
 * @note  在中断中被调用，设置数据就绪标志
 */
static void touchpad_irq_callback(void)
{
    touch_data_ready = true;
}
#endif

/**
 * @brief 坐标转换函数
 * @param x X坐标指针
 * @param y Y坐标指针
 */
static void touchpad_transform_xy(int16_t *x, int16_t *y)
{
    /* XY轴交换 */
#if TOUCH_SWAP_XY
    int16_t temp = *x;
    *x = *y;
    *y = temp;
#endif

    /* X轴镜像 */
#if TOUCH_INVERT_X
    *x = DISP_HOR_RES - *x;
#endif

    /* Y轴镜像 */
#if TOUCH_INVERT_Y
    *y = DISP_VER_RES - *y;
#endif

    /* 边界检查 */
    if (*x < 0) *x = 0;
    if (*x >= DISP_HOR_RES) *x = DISP_HOR_RES - 1;
    if (*y < 0) *y = 0;
    if (*y >= DISP_VER_RES) *y = DISP_VER_RES - 1;
}

/**
 * @brief 读取触摸屏数据（单点模式）
 * @param indev  输入设备对象指针
 * @param data   输入数据结构指针
 * @note  LVGL会周期性调用此函数读取触摸数据
 */
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    static int16_t last_x = 0;
    static int16_t last_y = 0;
    bool has_touch = false;

    LV_UNUSED(indev);
    RuntimeStats_BeginLvglInput();

    /* 如果触摸被禁用，直接返回释放状态 */
    if (!touchpad_enabled) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = last_x;
        data->point.y = last_y;
        RuntimeStats_EndLvglInput();
        return;
    }

#if USE_IRQ_MODE
    /* 中断模式：检查中断标志 */
    if (Touch_IsIRQPending()) {
        has_touch = Touch_Scan();
    }
#else
    /* 轮询模式：直接扫描 */
    has_touch = Touch_Scan();
#endif

    /* 处理触摸数据 */
    if (has_touch) {
        uint16_t raw_x, raw_y;

        /* 获取第一个触摸点（单点模式） */
        if (Touch_GetPoint(0, &raw_x, &raw_y)) {
            int16_t x = raw_x;
            int16_t y = raw_y;

            /* 应用坐标转换 */
            touchpad_transform_xy(&x, &y);

            /* 更新坐标 */
            last_x = x;
            last_y = y;

            /* 设置按下状态 */
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            /* 无有效触摸点 */
            data->state = LV_INDEV_STATE_RELEASED;
        }
    } else {
        /* 无触摸 */
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* 始终更新坐标（LVGL要求） */
    data->point.x = last_x;
    data->point.y = last_y;
    RuntimeStats_EndLvglInput();
}

#if MULTITOUCH_ENABLED
/**
 * @brief 读取触摸屏数据（多点模式）
 * @param indev  输入设备对象指针
 * @param data   输入数据结构指针
 * @note  支持最多5个触摸点
 */
static void touchpad_read_multitouch(lv_indev_t * indev, lv_indev_data_t * data)
{
    static uint8_t current_point_index = 0;
    static int16_t last_x = 0;
    static int16_t last_y = 0;
    bool has_touch = false;

    LV_UNUSED(indev);
    RuntimeStats_BeginLvglInput();

    /* 如果触摸被禁用，直接返回释放状态 */
    if (!touchpad_enabled) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = last_x;
        data->point.y = last_y;
        RuntimeStats_EndLvglInput();
        return;
    }

#if USE_IRQ_MODE
    /* 中断模式：检查中断标志 */
    if (Touch_IsIRQPending()) {
        has_touch = Touch_Scan();
    }
#else
    /* 轮询模式：直接扫描 */
    has_touch = Touch_Scan();
#endif

    /* 获取当前触摸点数 */
    uint8_t touch_count = Touch_GetNum();

    /* 如果是第一次读取本轮触摸 */
    if (current_point_index == 0) {
        if (has_touch && touch_count > 0) {
            /* 读取所有触摸点 */
            Touch_GetAllPoints(last_points, GT911_MAX_TOUCH_POINTS);
            last_point_count = touch_count;
        } else {
            last_point_count = 0;
        }
    }

    /* 返回当前索引的触摸点数据 */
    if (current_point_index < last_point_count) {
        int16_t x = last_points[current_point_index].x;
        int16_t y = last_points[current_point_index].y;

        /* 应用坐标转换 */
        touchpad_transform_xy(&x, &y);

        /* 更新数据 */
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;

        /* 保存最后坐标 */
        last_x = x;
        last_y = y;

        /* 移动到下一个点 */
        current_point_index++;

        /* 如果还有更多触摸点，通知LVGL继续读取 */
        if (current_point_index < last_point_count) {
            data->continue_reading = true;
        } else {
            /* 所有点已读取完毕，重置索引 */
            data->continue_reading = false;
            current_point_index = 0;
        }
    } else {
        /* 无触摸点 */
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = last_x;
        data->point.y = last_y;
        data->continue_reading = false;
        current_point_index = 0;
    }

    RuntimeStats_EndLvglInput();
}
#endif /* MULTITOUCH_ENABLED */

#endif /* TOUCHSCREEN_ENABLED */
