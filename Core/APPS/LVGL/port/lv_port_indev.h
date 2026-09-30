/**
* @file lv_port_indev.h
 * @brief LVGL 输入设备移植层头文件
 */

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

 /*********************
  *      包含文件
  *********************/
#include "lvgl.h"

typedef struct {
    int16_t x;
    int16_t y;
    bool pressed;
    uint8_t touch_count;
    uint32_t sequence;
} lv_port_pointer_sample_t;

 /*********************
  *      宏定义
  *********************/

 /**********************
  *      类型定义
  **********************/

 /**********************
  *   全局函数声明
  **********************/

 /**
  * @brief 初始化LVGL输入设备
  * @note  在lv_init()之后、创建UI之前调用
  */
 void lv_port_indev_init(void);

 /**
  * @brief 获取触摸屏输入设备对象
  * @return 触摸屏indev对象指针，如果禁用则返回NULL
  */
 lv_indev_t * lv_port_indev_get_touchpad(void);

 /**
  * @brief 获取最近一次经过坐标校准的物理触摸采样
  * @note 不触发新的 GT911 扫描；Launcher HW-pan 路由与 LVGL 共用同一采样源
  */
 bool lv_port_indev_get_pointer_sample(lv_port_pointer_sample_t *sample);

 /**
  * @brief 启用/禁用触摸输入
  * @param enable true: 启用; false: 禁用
  */
 void lv_port_indev_enable(bool enable);

 /**
  * @brief 设置触摸灵敏度
  * @param sensitivity 灵敏度值 (1-10, 默认5)
  */
 void lv_port_indev_set_sensitivity(uint8_t sensitivity);

 /**********************
  *      宏函数
  **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LV_PORT_INDEV_H */
