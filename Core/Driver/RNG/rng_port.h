/**
 ******************************************************************************
 * @file    rng_port.h
 * @brief   STM32 RNG硬件随机数生成器驱动头文件 (优化版本)
 * @note    支持STM32H7等系列芯片
 ******************************************************************************
 */

#ifndef __RNG_PORT_H
#define __RNG_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "main.h"
/* ===== 配置选项 ===== */
#ifndef RNG_MAX_RETRY
#define RNG_MAX_RETRY           8       /* 单次操作最大重试次数 */
#endif

#ifndef RNG_TIMEOUT_MS
#define RNG_TIMEOUT_MS          100     /* 单次操作超时时间(ms) */
#endif

#ifndef RNG_ENABLE_STATS
#define RNG_ENABLE_STATS        1       /* 启用统计功能 */
#endif

#ifndef RNG_DEBUG_ON
#define RNG_DEBUG_ON            0       /* 调试开关 */
#endif

/* ===== 调试宏 ===== */
#if RNG_DEBUG_ON
#include <stdio.h>
#define RNG_INFO(fmt, ...)      printf("<<-RNG-INFO->> " fmt "\n", ##__VA_ARGS__)
#define RNG_ERROR(fmt, ...)     printf("<<-RNG-ERROR->> " fmt "\n", ##__VA_ARGS__)
#define RNG_DEBUG(fmt, ...)     printf("<<-RNG-DEBUG->> [%d] " fmt "\n", __LINE__, ##__VA_ARGS__)
#else
#define RNG_INFO(fmt, ...)
#define RNG_ERROR(fmt, ...)
#define RNG_DEBUG(fmt, ...)
#endif

/* ===== 错误码定义 ===== */
typedef enum {
    RNG_OK = 0,                 /* 操作成功 */

    RNG_E_PARAM         = 1,    /* 参数错误(空指针、长度无效等) */
    RNG_E_HAL           = 2,    /* HAL层错误(RNG外设错误) */
    RNG_E_TIMEOUT       = 3,    /* 操作超时 */
    RNG_E_BUSY          = 4,    /* 设备繁忙 */
    RNG_E_NOT_READY     = 5,    /* 设备未就绪 */
    RNG_E_SEED_ERROR    = 6,    /* 种子错误 */
    RNG_E_CLOCK_ERROR   = 7,    /* 时钟错误 */
    RNG_E_RETRY_FAILED  = 8,    /* 重试失败 */
} RNG_Status;

/* ===== 错误信息结构体 ===== */
typedef struct {
    RNG_Status        code;         /* 错误代码 */
    HAL_StatusTypeDef hal;          /* HAL状态码 */
    uint32_t          rng_error;    /* RNG错误标志(从RNG->SR读取) */
    const char       *step;         /* 错误发生的步骤描述 */
    uint32_t          line;         /* 源代码行号 */
    uint32_t          retry_count;  /* 重试次数 */
    uint32_t          requested;    /* 请求的数据量 */
} RNG_ErrorInfo;

/* ===== 统计信息结构体 ===== */
#if RNG_ENABLE_STATS
typedef struct {
    uint32_t total_requests;        /* 总请求次数 */
    uint32_t total_bytes;           /* 总生成字节数 */
    uint32_t error_count;           /* 错误次数 */
    uint32_t retry_count;           /* 总重试次数 */
    uint32_t seed_errors;           /* 种子错误次数 */
    uint32_t clock_errors;          /* 时钟错误次数 */
    uint32_t timeout_count;         /* 超时次数 */
    uint32_t max_retry_in_request;  /* 单次请求最大重试次数 */
} RNG_Stats;
#endif

/* ===== 核心API函数 ===== */

/**
 * @brief  初始化RNG模块
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 * @note   检查RNG外设状态,清空统计信息
 */
RNG_Status RNG_Init(RNG_ErrorInfo *err_info);

/**
 * @brief  反初始化RNG模块
 * @retval RNG_Status状态码
 */
RNG_Status RNG_DeInit(void);

/**
 * @brief  检查RNG是否就绪
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 */
RNG_Status RNG_IsReady(RNG_ErrorInfo *err_info);

/**
 * @brief  生成单个32位随机数
 * @param  out: 输出缓冲区指针
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 * @note   自动重试、错误恢复
 */
RNG_Status RNG_GetU32(uint32_t *out, RNG_ErrorInfo *err_info);

/**
 * @brief  生成单个16位随机数
 * @param  out: 输出缓冲区指针
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 */
RNG_Status RNG_GetU16(uint16_t *out, RNG_ErrorInfo *err_info);

/**
 * @brief  生成单个8位随机数
 * @param  out: 输出缓冲区指针
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 */
RNG_Status RNG_GetU8(uint8_t *out, RNG_ErrorInfo *err_info);

/**
 * @brief  填充任意长度随机字节
 * @param  buf: 输出缓冲区指针
 * @param  len: 要填充的字节数
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 * @note   自动处理非4字节对齐的长度
 */
RNG_Status RNG_Fill(void *buf, size_t len, RNG_ErrorInfo *err_info);

/**
 * @brief  生成指定范围内的随机数 [min, max]
 * @param  min: 最小值(包含)
 * @param  max: 最大值(包含)
 * @param  out: 输出随机数
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 * @note   使用无偏差算法,确保均匀分布
 */
RNG_Status RNG_GetRange(uint32_t min, uint32_t max, uint32_t *out, RNG_ErrorInfo *err_info);

/**
 * @brief  生成指定范围内的随机浮点数 [0.0, 1.0)
 * @param  out: 输出随机浮点数
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 */
RNG_Status RNG_GetFloat(float *out, RNG_ErrorInfo *err_info);

/**
 * @brief  生成随机布尔值
 * @param  out: 输出布尔值
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 */
RNG_Status RNG_GetBool(bool *out, RNG_ErrorInfo *err_info);

/**
 * @brief  打乱数组(Fisher-Yates洗牌算法)
 * @param  array: 数组指针
 * @param  elem_size: 单个元素大小(字节)
 * @param  elem_count: 元素个数
 * @param  err_info: 错误信息输出(可为NULL)
 * @retval RNG_Status状态码
 */
RNG_Status RNG_Shuffle(void *array, size_t elem_size, size_t elem_count, RNG_ErrorInfo *err_info);

/* ===== 工具函数 ===== */

/**
 * @brief  获取错误描述字符串
 * @param  status: 错误码
 * @retval 错误描述字符串
 */
const char* RNG_GetErrorString(RNG_Status status);

/**
 * @brief  软复位RNG外设
 * @retval RNG_Status状态码
 * @note   用于从错误状态恢复
 */
RNG_Status RNG_SoftReset(void);

/* ===== 统计功能 ===== */
#if RNG_ENABLE_STATS

/**
 * @brief  获取统计信息
 * @param  stats: 统计信息输出缓冲区
 */
void RNG_GetStats(RNG_Stats *stats);

/**
 * @brief  重置统计信息
 */
void RNG_ResetStats(void);

/**
 * @brief  打印统计信息(需要启用调试)
 */
void RNG_PrintStats(void);

#endif /* RNG_ENABLE_STATS */

#ifdef __cplusplus
}
#endif

#endif /* __RNG_PORT_H */