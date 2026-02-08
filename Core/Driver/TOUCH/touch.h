/**
 * @file touch.h
 * @brief Goodix GT911 电容触摸屏驱动
 * @note  支持最多5点多点触控
 */

#pragma once

#include "main.h"
#include <stdbool.h>

/* GT911 I2C地址 */
#define GT911_I2C_ADDR          0xBA    // 7位地址左移1位

/* GT911 寄存器定义 */
#define GT911_REG_CTRL          0x8040  // 控制寄存器
#define GT911_REG_CONFIG        0x8047  // 配置寄存器
#define GT911_REG_PRODUCT_ID    0x8140  // 产品ID寄存器
#define GT911_REG_FIRMWARE_VER  0x8144  // 固件版本寄存器
#define GT911_REG_STATUS        0x814E  // 状态寄存器
#define GT911_REG_POINT_1       0x814F  // 第一个触摸点数据起始地址

/* 配置参数 */
#define GT911_MAX_TOUCH_POINTS  5       // 最大触摸点数
#define GT911_ADDR_LENGTH       2       // 寄存器地址长度

/* 触摸点结构体 */
typedef struct {
    uint8_t  track_id;      // 触摸轨迹ID (0-4)
    uint16_t x;             // X坐标
    uint16_t y;             // Y坐标
    uint16_t size;          // 触摸区域大小
    bool     valid;         // 该触摸点是否有效
} Touch_Point_t;

/* 触摸数据结构体 */
typedef struct {
    uint8_t        touch_count;                      // 当前触摸点数量
    Touch_Point_t  points[GT911_MAX_TOUCH_POINTS];   // 触摸点数组
    bool           data_ready;                       // 数据是否就绪
} Touch_Data_t;

/* 中断回调函数类型 */
typedef void (*Touch_IRQ_Callback_t)(void);

/* ==================== 公共API函数 ==================== */

/**
 * @brief 初始化GT911触摸控制器
 * @note  会执行硬件复位和配置
 */
void Touch_Init(void);

/**
 * @brief 扫描触摸事件
 * @return true: 有新的触摸数据; false: 无触摸或数据未就绪
 * @note  此函数会读取并解析触摸数据
 */
bool Touch_Scan(void);

/**
 * @brief 获取当前触摸点数量
 * @return 触摸点数量 (0-5)
 */
uint8_t Touch_GetNum(void);

/**
 * @brief 获取指定触摸点的坐标
 * @param index 触摸点索引 (0-4)
 * @param x 输出参数: X坐标指针
 * @param y 输出参数: Y坐标指针
 * @return true: 该触摸点有效; false: 无效或索引越界
 */
bool Touch_GetPoint(uint8_t index, uint16_t *x, uint16_t *y);

/**
 * @brief 读取完整的触摸数据结构
 * @param touch_data 输出参数: 触摸数据结构指针
 * @return true: 有新数据; false: 无新数据
 */
bool Touch_ReadData(Touch_Data_t *touch_data);

/**
 * @brief 获取所有有效的触摸点
 * @param points 输出参数: 触摸点数组
 * @param max_points 数组最大容量
 * @return 实际有效的触摸点数量
 */
uint8_t Touch_GetAllPoints(Touch_Point_t *points, uint8_t max_points);

/* ==================== 中断相关函数 ==================== */

/**
 * @brief 启用触摸中断
 * @note  配置NVIC和中断优先级
 */
void Touch_EnableIRQ(void);

/**
 * @brief 禁用触摸中断
 */
void Touch_DisableIRQ(void);

/**
 * @brief 检查是否有待处理的中断
 * @return true: 有待处理的中断; false: 无
 * @note  此函数会清除中断标志
 */
bool Touch_IsIRQPending(void);

/**
 * @brief 注册中断回调函数
 * @param callback 回调函数指针
 */
void Touch_RegisterCallback(Touch_IRQ_Callback_t callback);

/**
 * @brief 触摸中断处理函数
 * @note  在EXTI中断回调中调用此函数
 */
void Touch_IRQHandler(void);

/* ==================== 高级功能函数 ==================== */

/**
 * @brief 读取产品ID
 * @param id_buf 输出参数: 4字节产品ID缓冲区
 * @return true: 读取成功; false: 读取失败
 */
bool Touch_ReadProductID(uint8_t *id_buf);

/**
 * @brief 读取固件版本
 * @param version 输出参数: 版本号指针
 * @return true: 读取成功; false: 读取失败
 */
bool Touch_ReadFirmwareVersion(uint16_t *version);

/**
 * @brief 读取状态寄存器
 * @param status 输出参数: 状态字节指针
 * @return true: 读取成功; false: 读取失败
 */
bool Touch_ReadStatus(uint8_t *status);

/**
 * @brief 清除状态寄存器
 * @return true: 清除成功; false: 清除失败
 * @note  告诉GT911芯片数据已被读取
 */
bool Touch_ClearStatus(void);

/**
 * @brief 进入休眠模式
 * @note  降低功耗
 */
void Touch_Sleep(void);

/**
 * @brief 从休眠模式唤醒
 */
void Touch_Wakeup(void);

/**
 * @brief 打印调试信息
 * @note  需要启用printf支持
 */
void Touch_DebugInfo(void);
