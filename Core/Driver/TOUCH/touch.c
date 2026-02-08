/**
 * @file touch.c
 * @brief Goodix GT911 电容触摸屏驱动实现
 */

#include "touch.h"
#include "i2c.h"
#include <string.h>
#include <stdio.h>

/* ==================== 私有变量 ==================== */
static Touch_Data_t g_touch_data = {0};                              // 触摸数据
static int16_t g_last_x[GT911_MAX_TOUCH_POINTS] = {-1,-1,-1,-1,-1};  // 上次X坐标
static int16_t g_last_y[GT911_MAX_TOUCH_POINTS] = {-1,-1,-1,-1,-1};  // 上次Y坐标
static volatile bool g_touch_irq_pending = false;                    // 中断待处理标志
static Touch_IRQ_Callback_t g_irq_callback = NULL;                   // 中断回调函数

/* I2C超时时间 */
#define I2C_TIMEOUT_MS  100

/* ==================== 私有函数声明 ==================== */
static void Touch_HW_Reset(void);
static bool Touch_I2C_Read(uint16_t reg_addr, uint8_t *buf, uint16_t len);
static bool Touch_I2C_Write(uint16_t reg_addr, uint8_t *buf, uint16_t len);

/* ==================== 硬件层函数 ==================== */

/**
 * @brief 硬件复位GT911芯片
 * @note  按照GT911数据手册的复位时序操作
 */
static void Touch_HW_Reset(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 配置RST和INT引脚为输出模式 */
    GPIO_InitStruct.Pin = TOUCH_RST_Pin | TOUCH_INT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* 执行复位时序 */
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TOUCH_INT_GPIO_Port, TOUCH_INT_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);

    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100);  // 等待芯片启动

    /* 配置INT引脚为下降沿中断模式 */
    GPIO_InitStruct.Pin = TOUCH_INT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(TOUCH_INT_GPIO_Port, &GPIO_InitStruct);
}

/**
 * @brief 通过I2C读取GT911数据
 * @param reg_addr 寄存器地址 (16位)
 * @param buf 数据缓冲区
 * @param len 读取长度
 * @return true: 成功; false: 失败
 */
static bool Touch_I2C_Read(uint16_t reg_addr, uint8_t *buf, uint16_t len)
{
    uint8_t addr_buf[2];
    addr_buf[0] = (reg_addr >> 8) & 0xFF;  // 高字节
    addr_buf[1] = reg_addr & 0xFF;         // 低字节

    HAL_StatusTypeDef status;

    /* 发送寄存器地址 */
    status = HAL_I2C_Master_Transmit(&hi2c2, GT911_I2C_ADDR, addr_buf, 2, I2C_TIMEOUT_MS);
    if (status != HAL_OK) {
        return false;
    }

    /* 读取数据 */
    status = HAL_I2C_Master_Receive(&hi2c2, GT911_I2C_ADDR, buf, len, I2C_TIMEOUT_MS);
    return (status == HAL_OK);
}

/**
 * @brief 通过I2C写入GT911数据
 * @param reg_addr 寄存器地址 (16位)
 * @param buf 数据缓冲区
 * @param len 写入长度
 * @return true: 成功; false: 失败
 */
static bool Touch_I2C_Write(uint16_t reg_addr, uint8_t *buf, uint16_t len)
{
    uint8_t write_buf[64];  // 临时缓冲区

    /* 防止缓冲区溢出 */
    if (len + 2 > sizeof(write_buf)) {
        return false;
    }

    /* 组装数据: 寄存器地址 + 数据 */
    write_buf[0] = (reg_addr >> 8) & 0xFF;  // 高字节
    write_buf[1] = reg_addr & 0xFF;         // 低字节
    memcpy(&write_buf[2], buf, len);

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(&hi2c2, GT911_I2C_ADDR,
                                                        write_buf, len + 2, I2C_TIMEOUT_MS);
    return (status == HAL_OK);
}

/* ==================== 公共API实现 ==================== */

/**
 * @brief 初始化GT911触摸控制器
 */
void Touch_Init(void)
{
    /* 硬件复位 */
    Touch_HW_Reset();

    /* 读取并验证产品ID */
    uint8_t product_id[4] = {0};
    if (Touch_ReadProductID(product_id)) {
        // 产品ID应为 "911"
        if (product_id[0] == '9' && product_id[1] == '1' && product_id[2] == '1') {
            // 验证成功
        }
    }

    /* 清除待处理的触摸数据 */
    Touch_ClearStatus();

    /* 清除中断标志 */
    g_touch_irq_pending = false;
}

/**
 * @brief 扫描触摸事件
 * @return true: 有新的触摸数据; false: 无触摸或数据未就绪
 */
bool Touch_Scan(void)
{
    uint8_t status = 0;

    /* 读取状态寄存器 */
    if (!Touch_ReadStatus(&status)) {
        return false;
    }

    /* 检查缓冲区状态位(bit 7) */
    if ((status & 0x80) == 0) {
        return false;  // 数据未就绪
    }

    /* 获取触摸点数量 */
    g_touch_data.touch_count = status & 0x0F;

    /* 触摸点数量校验 */
    if (g_touch_data.touch_count > GT911_MAX_TOUCH_POINTS) {
        Touch_ClearStatus();
        return false;
    }

    /* 如果有触摸点，读取触摸数据 */
    if (g_touch_data.touch_count > 0) {
        uint8_t point_data[40] = {0};  // 最多5个点 * 8字节
        uint16_t data_len = g_touch_data.touch_count * 8;

        /* 读取所有触摸点数据 */
        if (!Touch_I2C_Read(GT911_REG_POINT_1, point_data, data_len)) {
            Touch_ClearStatus();
            return false;
        }

        /* 解析每个触摸点 */
        for (uint8_t i = 0; i < g_touch_data.touch_count; i++) {
            uint8_t *p = &point_data[i * 8];

            g_touch_data.points[i].track_id = p[0] & 0x0F;
            g_touch_data.points[i].x = p[1] | (p[2] << 8);
            g_touch_data.points[i].y = p[3] | (p[4] << 8);
            g_touch_data.points[i].size = p[5] | (p[6] << 8);
            g_touch_data.points[i].valid = true;

            /* 保存上次位置 */
            uint8_t id = g_touch_data.points[i].track_id;
            if (id < GT911_MAX_TOUCH_POINTS) {
                g_last_x[id] = g_touch_data.points[i].x;
                g_last_y[id] = g_touch_data.points[i].y;
            }
        }

        /* 清空未使用的触摸点 */
        for (uint8_t i = g_touch_data.touch_count; i < GT911_MAX_TOUCH_POINTS; i++) {
            g_touch_data.points[i].valid = false;
        }

        g_touch_data.data_ready = true;
    }
    else {
        /* 无触摸，清空所有触摸点 */
        memset(&g_touch_data.points, 0, sizeof(g_touch_data.points));
        g_touch_data.data_ready = false;

        /* 重置上次位置 */
        for (uint8_t i = 0; i < GT911_MAX_TOUCH_POINTS; i++) {
            g_last_x[i] = -1;
            g_last_y[i] = -1;
        }
    }

    /* 清除状态寄存器 */
    Touch_ClearStatus();

    return g_touch_data.data_ready;
}

/**
 * @brief 获取当前触摸点数量
 * @return 触摸点数量 (0-5)
 */
uint8_t Touch_GetNum(void)
{
    return g_touch_data.touch_count;
}

/**
 * @brief 获取指定触摸点的坐标
 * @param index 触摸点索引 (0-4)
 * @param x X坐标指针
 * @param y Y坐标指针
 * @return true: 该触摸点有效; false: 无效或索引越界
 */
bool Touch_GetPoint(uint8_t index, uint16_t *x, uint16_t *y)
{
    if (index >= GT911_MAX_TOUCH_POINTS || !g_touch_data.points[index].valid) {
        return false;
    }

    *x = g_touch_data.points[index].x;
    *y = g_touch_data.points[index].y;
    return true;
}

/**
 * @brief 读取完整的触摸数据结构
 * @param touch_data 触摸数据结构指针
 * @return true: 有新数据; false: 无新数据
 */
bool Touch_ReadData(Touch_Data_t *touch_data)
{
    if (Touch_Scan()) {
        memcpy(touch_data, &g_touch_data, sizeof(Touch_Data_t));
        return true;
    }
    return false;
}

/**
 * @brief 获取所有有效的触摸点
 * @param points 触摸点数组
 * @param max_points 数组最大容量
 * @return 实际有效的触摸点数量
 */
uint8_t Touch_GetAllPoints(Touch_Point_t *points, uint8_t max_points)
{
    uint8_t count = 0;

    for (uint8_t i = 0; i < GT911_MAX_TOUCH_POINTS && count < max_points; i++) {
        if (g_touch_data.points[i].valid) {
            memcpy(&points[count], &g_touch_data.points[i], sizeof(Touch_Point_t));
            count++;
        }
    }

    return count;
}

/* ==================== 中断相关函数 ==================== */

/**
 * @brief 启用触摸中断
 */
void Touch_EnableIRQ(void)
{
    /* 配置NVIC */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/**
 * @brief 禁用触摸中断
 */
void Touch_DisableIRQ(void)
{
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
}

/**
 * @brief 检查是否有待处理的中断
 * @return true: 有待处理的中断; false: 无
 */
bool Touch_IsIRQPending(void)
{
    bool pending = g_touch_irq_pending;
    g_touch_irq_pending = false;
    return pending;
}

/**
 * @brief 注册中断回调函数
 * @param callback 回调函数指针
 */
void Touch_RegisterCallback(Touch_IRQ_Callback_t callback)
{
    g_irq_callback = callback;
}

/**
 * @brief 触摸中断处理函数
 * @note  在HAL_GPIO_EXTI_Callback中调用
 */
void Touch_IRQHandler(void)
{
    /* 设置中断待处理标志 */
    g_touch_irq_pending = true;

    /* 调用用户注册的回调函数 */
    if (g_irq_callback != NULL) {
        g_irq_callback();
    }
}

/* ==================== 高级功能函数 ==================== */

/**
 * @brief 读取产品ID
 * @param id_buf 4字节产品ID缓冲区
 * @return true: 读取成功; false: 读取失败
 */
bool Touch_ReadProductID(uint8_t *id_buf)
{
    return Touch_I2C_Read(GT911_REG_PRODUCT_ID, id_buf, 4);
}

/**
 * @brief 读取固件版本
 * @param version 版本号指针
 * @return true: 读取成功; false: 读取失败
 */
bool Touch_ReadFirmwareVersion(uint16_t *version)
{
    uint8_t buf[2];
    if (Touch_I2C_Read(GT911_REG_FIRMWARE_VER, buf, 2)) {
        *version = (buf[1] << 8) | buf[0];
        return true;
    }
    return false;
}

/**
 * @brief 读取状态寄存器
 * @param status 状态字节指针
 * @return true: 读取成功; false: 读取失败
 */
bool Touch_ReadStatus(uint8_t *status)
{
    return Touch_I2C_Read(GT911_REG_STATUS, status, 1);
}

/**
 * @brief 清除状态寄存器
 * @return true: 清除成功; false: 清除失败
 */
bool Touch_ClearStatus(void)
{
    uint8_t clear_flag = 0;
    return Touch_I2C_Write(GT911_REG_STATUS, &clear_flag, 1);
}

/**
 * @brief 进入休眠模式
 */
void Touch_Sleep(void)
{
    uint8_t cmd = 0x05;
    Touch_I2C_Write(GT911_REG_CTRL, &cmd, 1);
}

/**
 * @brief 从休眠模式唤醒
 */
void Touch_Wakeup(void)
{
    /* 通过INT引脚唤醒 */
    HAL_GPIO_WritePin(TOUCH_INT_GPIO_Port, TOUCH_INT_Pin, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(TOUCH_INT_GPIO_Port, TOUCH_INT_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
}

/**
 * @brief 打印调试信息
 */
void Touch_DebugInfo(void)
{
    uint8_t product_id[4] = {0};
    uint16_t fw_version = 0;
    uint8_t status = 0;

    printf("\n========== GT911 触摸屏调试信息 ==========\n");

    /* 产品ID */
    if (Touch_ReadProductID(product_id)) {
        printf("产品ID: %c%c%c (0x%02X)\n",
               product_id[0], product_id[1], product_id[2], product_id[3]);
    } else {
        printf("读取产品ID失败\n");
    }

    /* 固件版本 */
    if (Touch_ReadFirmwareVersion(&fw_version)) {
        printf("固件版本: 0x%04X\n", fw_version);
    } else {
        printf("读取固件版本失败\n");
    }

    /* 当前状态 */
    if (Touch_ReadStatus(&status)) {
        printf("状态寄存器: 0x%02X\n", status);
        printf("  缓冲区就绪: %s\n", (status & 0x80) ? "是" : "否");
        printf("  触摸点数: %d\n", status & 0x0F);
    } else {
        printf("读取状态寄存器失败\n");
    }

    printf("==========================================\n\n");
}