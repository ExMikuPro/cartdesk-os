/**
 ******************************************************************************
 * @file    eeprom.h
 * @brief   AT24Cxx EEPROM驱动头文件 (优化版本)
 * @note    支持AT24C01/02/04/08A/16A系列
 ******************************************************************************
 */

#ifndef __EEPROM_H
#define __EEPROM_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ===== 芯片配置 ===== */
/* AT24C01/02: 每页8字节 */
#define EEPROM_PAGESIZE           8
/* AT24C04/08A/16A: 每页16字节 */
// #define EEPROM_PAGESIZE        16

/* AT24C02容量: 2KB = 256字节 */
#define EEPROM_SIZE               256

/* ===== I2C配置 ===== */
#define EEPROM_I2C                I2C4
#define EEPROM_I2C_CLK_ENABLE()   __HAL_RCC_I2C4_CLK_ENABLE()
#define EEPROM_I2C_FORCE_RESET()  __HAL_RCC_I2C4_FORCE_RESET()
#define EEPROM_I2C_RELEASE_RESET() __HAL_RCC_I2C4_RELEASE_RESET()

/* I2C中断配置 */
#define EEPROM_I2C_EV_IRQn        I2C4_EV_IRQn
#define EEPROM_I2C_ER_IRQn        I2C4_ER_IRQn
#define EEPROM_I2C_EV_IRQHandler  I2C4_EV_IRQHandler
#define EEPROM_I2C_ER_IRQHandler  I2C4_ER_IRQHandler

/* I2C GPIO配置 */
#define EEPROM_I2C_SCL_PIN        GPIO_PIN_8
#define EEPROM_I2C_SCL_GPIO_PORT  GPIOB
#define EEPROM_I2C_SCL_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define EEPROM_I2C_SCL_AF         GPIO_AF6_I2C4

#define EEPROM_I2C_SDA_PIN        GPIO_PIN_9
#define EEPROM_I2C_SDA_GPIO_PORT  GPIOB
#define EEPROM_I2C_SDA_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define EEPROM_I2C_SDA_AF         GPIO_AF6_I2C4

/* I2C从机地址(7位地址) */
#define I2C_OWN_ADDRESS7          0x0A

/* ===== EEPROM I2C地址 ===== */
/*
 * AT24C02设备地址格式:
 * 1 0 1 0 A2 A1 A0 R/W
 * 写地址: 0xA0 (1010 000 0)
 * 读地址: 0xA1 (1010 000 1)
 */
#define EEPROM_ADDRESS            0xA0

/* 多块EEPROM地址(根据A2 A1 A0引脚配置) */
#define EEPROM_Block0_ADDRESS     0xA0  /* A2=0, A1=0, A0=0 */
#define EEPROM_Block1_ADDRESS     0xA2  /* A2=0, A1=0, A0=1 */
#define EEPROM_Block2_ADDRESS     0xA4  /* A2=0, A1=1, A0=0 */
#define EEPROM_Block3_ADDRESS     0xA6  /* A2=0, A1=1, A0=1 */

/* ===== 超时配置 ===== */
#define EEPROM_TIMEOUT_MS         100   /* 单次操作超时(ms) */
#define EEPROM_MAX_TRIALS         300   /* 设备就绪检查最大次数 */
#define EEPROM_READY_TIMEOUT_MS   300   /* 设备就绪超时(ms) */

/* ===== 调试开关 ===== */
#define EEPROM_DEBUG_ON           0

#if EEPROM_DEBUG_ON
#define EEPROM_INFO(fmt, ...)     printf("<<-EEPROM-INFO->> " fmt "\n", ##__VA_ARGS__)
#define EEPROM_ERROR(fmt, ...)    printf("<<-EEPROM-ERROR->> " fmt "\n", ##__VA_ARGS__)
#define EEPROM_DEBUG(fmt, ...)    printf("<<-EEPROM-DEBUG->> [%d] " fmt "\n", __LINE__, ##__VA_ARGS__)
#else
#define EEPROM_INFO(fmt, ...)
#define EEPROM_ERROR(fmt, ...)
#define EEPROM_DEBUG(fmt, ...)
#endif

/* ===== 错误码定义 ===== */
typedef enum {
    EEPROM_OK = 0,              /* 操作成功 */

    EEPROM_E_PARAM      = 1,    /* 参数错误(空指针、长度为0等) */
    EEPROM_E_RANGE      = 2,    /* 地址范围错误(超出EEPROM容量) */
    EEPROM_E_ALIGN      = 3,    /* 地址对齐错误(预留,当前未使用) */
    EEPROM_E_HAL        = 4,    /* HAL层错误(I2C通信失败) */
    EEPROM_E_TIMEOUT    = 5,    /* 操作超时(设备未就绪) */
    EEPROM_E_BUSY       = 6,    /* 设备繁忙(I2C总线忙) */
    EEPROM_E_INIT       = 7,    /* 初始化错误 */
} EEPROM_Status;

/* ===== 错误信息结构体 ===== */
typedef struct {
    EEPROM_Status     code;         /* 错误代码 */
    HAL_StatusTypeDef hal;          /* HAL状态码 */
    uint32_t          i2c_error;    /* I2C错误码(HAL_I2C_GetError) */
    const char       *step;         /* 错误发生的步骤描述 */
    uint32_t          line;         /* 源代码行号 */
    uint32_t          addr;         /* 相关地址 */
    uint32_t          len;          /* 相关长度 */
} EEPROM_ErrorInfo;

/* ===== 操作统计结构体 ===== */
typedef struct {
    uint32_t write_count;       /* 写操作次数 */
    uint32_t read_count;        /* 读操作次数 */
    uint32_t error_count;       /* 错误次数 */
    uint32_t timeout_count;     /* 超时次数 */
} EEPROM_Stats;

/* ===== 公共API函数 ===== */

/**
 * @brief  初始化EEPROM
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval EEPROM_Status状态码
 */
EEPROM_Status EEPROM_Init(EEPROM_ErrorInfo *err_info);

/**
 * @brief  检查EEPROM是否就绪
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval EEPROM_Status状态码
 */
EEPROM_Status EEPROM_IsReady(EEPROM_ErrorInfo *err_info);

/**
 * @brief  写入单个字节
 * @param  addr: 写入地址(0~255)
 * @param  data: 要写入的数据
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval EEPROM_Status状态码
 */
EEPROM_Status EEPROM_WriteByte(uint8_t addr, uint8_t data, EEPROM_ErrorInfo *err_info);

/**
 * @brief  写入页数据(单页写入,不跨页)
 * @param  addr: 写入起始地址
 * @param  pBuffer: 数据缓冲区指针
 * @param  len: 写入长度(不超过EEPROM_PAGESIZE)
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval EEPROM_Status状态码
 */
EEPROM_Status EEPROM_WritePage(uint8_t addr, const uint8_t *pBuffer, uint8_t len, EEPROM_ErrorInfo *err_info);

/**
 * @brief  写入任意长度数据(自动处理跨页)
 * @param  addr: 写入起始地址
 * @param  pBuffer: 数据缓冲区指针
 * @param  len: 写入长度
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval EEPROM_Status状态码
 */
EEPROM_Status EEPROM_Write(uint8_t addr, const uint8_t *pBuffer, uint16_t len, EEPROM_ErrorInfo *err_info);

/**
 * @brief  读取任意长度数据
 * @param  addr: 读取起始地址
 * @param  pBuffer: 数据缓冲区指针
 * @param  len: 读取长度
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval EEPROM_Status状态码
 */
EEPROM_Status EEPROM_Read(uint8_t addr, uint8_t *pBuffer, uint16_t len, EEPROM_ErrorInfo *err_info);

/**
 * @brief  擦除整个EEPROM(写入0xFF)
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval EEPROM_Status状态码
 */
EEPROM_Status EEPROM_EraseChip(EEPROM_ErrorInfo *err_info);

/**
 * @brief  擦除指定区域(写入0xFF)
 * @param  addr: 起始地址
 * @param  len: 擦除长度
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval EEPROM_Status状态码
 */
EEPROM_Status EEPROM_Erase(uint8_t addr, uint16_t len, EEPROM_ErrorInfo *err_info);

/**
 * @brief  获取错误描述字符串
 * @param  status: 错误码
 * @retval 错误描述字符串
 */
const char* EEPROM_GetErrorString(EEPROM_Status status);

/**
 * @brief  获取操作统计信息
 * @param  stats: 统计信息输出缓冲区
 */
void EEPROM_GetStats(EEPROM_Stats *stats);

/**
 * @brief  重置统计信息
 */
void EEPROM_ResetStats(void);

#endif /* __EEPROM_H */