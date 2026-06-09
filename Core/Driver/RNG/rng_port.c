/**
 ******************************************************************************
 * @file    rng_port.c
 * @brief   STM32 RNG硬件随机数生成器驱动实现 (优化版本)
 * @note    完善的错误处理、统计功能、多种随机数生成方法
 ******************************************************************************
 */

#include "rng_port.h"

#include "rng.h"                /* CubeMX生成: extern RNG_HandleTypeDef hrng; */
#include "stm32h7xx_hal.h"
#include <string.h>

/* ===== 私有宏定义 ===== */
#define RNG_SHUFFLE_STACK_SCRATCH_SIZE 64u

#define SET_ERROR(info, err_code, hal_status, step_str) \
    do { \
        if ((info) != NULL) { \
            (info)->code = (err_code); \
            (info)->hal = (hal_status); \
            (info)->rng_error = 0; \
            if (hrng.Instance != NULL) { \
                (info)->rng_error = hrng.Instance->SR; \
            } \
            (info)->step = (step_str); \
            (info)->line = __LINE__; \
            (info)->retry_count = 0; \
            (info)->requested = 0; \
        } \
        RNG_ERROR("%s at line %d: %s", step_str, __LINE__, RNG_GetErrorString(err_code)); \
        UPDATE_ERROR_STATS(err_code); \
    } while(0)

#define SET_ERROR_EX(info, err_code, hal_status, step_str, retries, req_size) \
    do { \
        if ((info) != NULL) { \
            (info)->code = (err_code); \
            (info)->hal = (hal_status); \
            (info)->rng_error = 0; \
            if (hrng.Instance != NULL) { \
                (info)->rng_error = hrng.Instance->SR; \
            } \
            (info)->step = (step_str); \
            (info)->line = __LINE__; \
            (info)->retry_count = (retries); \
            (info)->requested = (req_size); \
        } \
        RNG_ERROR("%s at line %d: %s (retries=%u, req=%u)", \
                  step_str, __LINE__, RNG_GetErrorString(err_code), retries, req_size); \
        UPDATE_ERROR_STATS(err_code); \
    } while(0)

#if RNG_ENABLE_STATS
#define UPDATE_ERROR_STATS(err_code) \
    do { \
        g_stats.error_count++; \
        if ((err_code) == RNG_E_TIMEOUT) g_stats.timeout_count++; \
        if ((err_code) == RNG_E_SEED_ERROR) g_stats.seed_errors++; \
        if ((err_code) == RNG_E_CLOCK_ERROR) g_stats.clock_errors++; \
    } while(0)
#else
#define UPDATE_ERROR_STATS(err_code)
#endif

/* ===== 私有变量 ===== */
#if RNG_ENABLE_STATS
static RNG_Stats g_stats = {0};     /* 统计信息 */
#endif

/* ===== 私有函数声明 ===== */
static RNG_Status CheckRNGError(RNG_ErrorInfo *err_info);
static void ClearRNGError(void);

/* ===== 私有函数实现 ===== */

/**
 * @brief  检查RNG错误标志
 * @param  err_info: 错误信息输出
 * @retval RNG_Status状态码
 */
static RNG_Status CheckRNGError(RNG_ErrorInfo *err_info)
{
    if (hrng.Instance == NULL) {
        SET_ERROR(err_info, RNG_E_NOT_READY, HAL_ERROR, "RNG instance is NULL");
        return RNG_E_NOT_READY;
    }

    uint32_t sr = hrng.Instance->SR;

    /* 检查种子错误 */
    if (sr & RNG_SR_SEIS) {
        SET_ERROR(err_info, RNG_E_SEED_ERROR, HAL_ERROR, "RNG seed error detected");
#if RNG_ENABLE_STATS
        g_stats.seed_errors++;
#endif
        return RNG_E_SEED_ERROR;
    }

    /* 检查时钟错误 */
    if (sr & RNG_SR_CEIS) {
        SET_ERROR(err_info, RNG_E_CLOCK_ERROR, HAL_ERROR, "RNG clock error detected");
#if RNG_ENABLE_STATS
        g_stats.clock_errors++;
#endif
        return RNG_E_CLOCK_ERROR;
    }

    return RNG_OK;
}

/**
 * @brief  清除RNG错误标志
 */
static void ClearRNGError(void)
{
    if (hrng.Instance != NULL) {
        /* 清除种子错误标志 */
        if (hrng.Instance->SR & RNG_SR_SEIS) {
            hrng.Instance->SR &= ~RNG_SR_SEIS;
        }

        /* 清除时钟错误标志 */
        if (hrng.Instance->SR & RNG_SR_CEIS) {
            hrng.Instance->SR &= ~RNG_SR_CEIS;
        }
    }
}

/* ===== 公共函数实现 ===== */

/**
 * @brief  初始化RNG模块
 */
RNG_Status RNG_Init(RNG_ErrorInfo *err_info)
{
    RNG_DEBUG("Initializing RNG...");

    /* 检查RNG句柄 */
    if (hrng.Instance == NULL) {
        SET_ERROR(err_info, RNG_E_NOT_READY, HAL_ERROR, "RNG not initialized by HAL");
        return RNG_E_NOT_READY;
    }

    /* 清除错误标志 */
    ClearRNGError();

    /* 重置统计信息 */
#if RNG_ENABLE_STATS
    memset(&g_stats, 0, sizeof(g_stats));
#endif

    /* 检查RNG状态 */
    RNG_Status status = RNG_IsReady(err_info);

    if (status == RNG_OK) {
        RNG_INFO("RNG initialized successfully");
    }

    return status;
}

/**
 * @brief  反初始化RNG模块
 */
RNG_Status RNG_DeInit(void)
{
    RNG_DEBUG("De-initializing RNG...");

    if (hrng.Instance != NULL) {
        __HAL_RNG_DISABLE(&hrng);
    }

    return RNG_OK;
}

/**
 * @brief  检查RNG是否就绪
 */
RNG_Status RNG_IsReady(RNG_ErrorInfo *err_info)
{
    if (hrng.Instance == NULL) {
        SET_ERROR(err_info, RNG_E_NOT_READY, HAL_ERROR, "RNG instance is NULL");
        return RNG_E_NOT_READY;
    }

    /* 检查RNG错误 */
    RNG_Status status = CheckRNGError(err_info);
    if (status != RNG_OK) {
        return status;
    }

    /* 检查RNG是否使能 */
    if ((hrng.Instance->CR & RNG_CR_RNGEN) == 0u) {
        __HAL_RNG_ENABLE(&hrng);
    }

    return RNG_OK;
}

/**
 * @brief  软复位RNG外设
 */
RNG_Status RNG_SoftReset(void)
{
    RNG_DEBUG("Performing RNG soft reset...");

    if (hrng.Instance == NULL) {
        return RNG_E_NOT_READY;
    }

    /* 禁用RNG */
    __HAL_RNG_DISABLE(&hrng);

    /* 清除错误标志 */
    ClearRNGError();

    /* 短暂延时 */
    for (volatile uint32_t i = 0; i < 100; i++);

    /* 重新使能RNG */
    __HAL_RNG_ENABLE(&hrng);

    return RNG_OK;
}

/**
 * @brief  生成单个32位随机数
 */
RNG_Status RNG_GetU32(uint32_t *out, RNG_ErrorInfo *err_info)
{
    HAL_StatusTypeDef hal_status = HAL_ERROR;
    uint32_t retry;

    /* 参数校验 */
    if (out == NULL) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Output pointer is NULL");
        return RNG_E_PARAM;
    }

    RNG_DEBUG("Generating 32-bit random number...");

    /* 重试循环 */
    for (retry = 0; retry < RNG_MAX_RETRY; retry++) {
        /* 检查RNG错误 */
        RNG_Status status = CheckRNGError(err_info);
        if (status != RNG_OK) {
            RNG_SoftReset();
            continue;
        }

        /* 生成随机数 */
        hal_status = HAL_RNG_GenerateRandomNumber(&hrng, out);

        if (hal_status == HAL_OK) {
            /* 成功 */
#if RNG_ENABLE_STATS
            g_stats.total_requests++;
            g_stats.total_bytes += 4;
            g_stats.retry_count += retry;
            if (retry > g_stats.max_retry_in_request) {
                g_stats.max_retry_in_request = retry;
            }
#endif
            RNG_DEBUG("Generated: 0x%08lX (retry=%lu)", *out, retry);
            return RNG_OK;
        }

        /* 失败,尝试恢复 */
        RNG_DEBUG("HAL_RNG_GenerateRandomNumber failed (status=%d), retry %lu/%d",
                  hal_status, retry + 1, RNG_MAX_RETRY);

        RNG_SoftReset();

#if RNG_ENABLE_STATS
        g_stats.retry_count++;
#endif
    }

    /* 重试失败 */
    SET_ERROR_EX(err_info, RNG_E_RETRY_FAILED, hal_status,
                 "Failed to generate random number after retries", retry, 4);

    return RNG_E_RETRY_FAILED;
}

/**
 * @brief  生成单个16位随机数
 */
RNG_Status RNG_GetU16(uint16_t *out, RNG_ErrorInfo *err_info)
{
    uint32_t rnd32;

    if (out == NULL) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Output pointer is NULL");
        return RNG_E_PARAM;
    }

    RNG_Status status = RNG_GetU32(&rnd32, err_info);
    if (status == RNG_OK) {
        *out = (uint16_t)(rnd32 & 0xFFFF);
    }

    return status;
}

/**
 * @brief  生成单个8位随机数
 */
RNG_Status RNG_GetU8(uint8_t *out, RNG_ErrorInfo *err_info)
{
    uint32_t rnd32;

    if (out == NULL) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Output pointer is NULL");
        return RNG_E_PARAM;
    }

    RNG_Status status = RNG_GetU32(&rnd32, err_info);
    if (status == RNG_OK) {
        *out = (uint8_t)(rnd32 & 0xFF);
    }

    return status;
}

/**
 * @brief  填充任意长度随机字节
 */
RNG_Status RNG_Fill(void *buf, size_t len, RNG_ErrorInfo *err_info)
{
    uint8_t *p;
    RNG_Status status;

    /* 参数校验 */
    if (buf == NULL && len > 0) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Buffer is NULL but len > 0");
        return RNG_E_PARAM;
    }

    if (len == 0) {
        return RNG_OK;  /* 0长度视为成功 */
    }

    RNG_DEBUG("Filling %zu bytes with random data...", len);

    p = (uint8_t *)buf;

    /* 按4字节一次生成 */
    while (len > 0) {
        uint32_t rnd32;

        status = RNG_GetU32(&rnd32, err_info);
        if (status != RNG_OK) {
            return status;
        }

        /* 复制数据 */
        size_t copy_len = (len >= 4) ? 4 : len;
        memcpy(p, &rnd32, copy_len);

        p += copy_len;
        len -= copy_len;
    }

    return RNG_OK;
}

/**
 * @brief  生成指定范围内的随机数 [min, max]
 * @note   使用无偏差算法,确保均匀分布
 */
RNG_Status RNG_GetRange(uint32_t min, uint32_t max, uint32_t *out, RNG_ErrorInfo *err_info)
{
    uint32_t rnd32;
    uint32_t range;
    uint32_t limit;
    RNG_Status status;

    /* 参数校验 */
    if (out == NULL) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Output pointer is NULL");
        return RNG_E_PARAM;
    }

    if (min > max) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "min > max");
        return RNG_E_PARAM;
    }

    /* 特殊情况: min == max */
    if (min == max) {
        *out = min;
        return RNG_OK;
    }

    range = max - min + 1;

    /* 如果range是2的幂,可以直接使用位掩码 */
    if ((range & (range - 1)) == 0) {
        status = RNG_GetU32(&rnd32, err_info);
        if (status == RNG_OK) {
            *out = min + (rnd32 & (range - 1));
        }
        return status;
    }

    /* 使用拒绝采样避免偏差 */
    limit = UINT32_MAX - (UINT32_MAX % range);

    do {
        status = RNG_GetU32(&rnd32, err_info);
        if (status != RNG_OK) {
            return status;
        }
    } while (rnd32 >= limit);

    *out = min + (rnd32 % range);

    return RNG_OK;
}

/**
 * @brief  生成随机浮点数 [0.0, 1.0)
 */
RNG_Status RNG_GetFloat(float *out, RNG_ErrorInfo *err_info)
{
    uint32_t rnd32;

    if (out == NULL) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Output pointer is NULL");
        return RNG_E_PARAM;
    }

    RNG_Status status = RNG_GetU32(&rnd32, err_info);
    if (status == RNG_OK) {
        /* 使用高23位生成[0, 1)的浮点数 */
        *out = (rnd32 >> 9) * (1.0f / 8388608.0f);  /* 2^23 = 8388608 */
    }

    return status;
}

/**
 * @brief  生成随机布尔值
 */
RNG_Status RNG_GetBool(bool *out, RNG_ErrorInfo *err_info)
{
    uint32_t rnd32;

    if (out == NULL) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Output pointer is NULL");
        return RNG_E_PARAM;
    }

    RNG_Status status = RNG_GetU32(&rnd32, err_info);
    if (status == RNG_OK) {
        *out = (rnd32 & 1) ? true : false;
    }

    return status;
}

/**
 * @brief  使用调用方 scratch buffer 打乱数组(Fisher-Yates洗牌算法)
 */
RNG_Status RNG_ShuffleWithScratch(void *array,
                                  size_t elem_size,
                                  size_t elem_count,
                                  void *scratch,
                                  size_t scratch_size,
                                  RNG_ErrorInfo *err_info)
{
    uint8_t *arr;
    uint8_t *temp = (uint8_t *)scratch;
    uint32_t i, j;
    RNG_Status status;

    /* 参数校验 */
    if (array == NULL && elem_count > 0) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Array is NULL but count > 0");
        return RNG_E_PARAM;
    }

    if (elem_size == 0) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Element size is zero");
        return RNG_E_PARAM;
    }

    if (elem_count <= 1) {
        return RNG_OK;  /* 0或1个元素无需打乱 */
    }

    if (elem_count > UINT32_MAX) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Element count exceeds RNG range limit");
        return RNG_E_PARAM;
    }

    if (scratch == NULL || scratch_size < elem_size) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Scratch buffer is too small");
        return RNG_E_PARAM;
    }

    arr = (uint8_t *)array;

    /* Fisher-Yates洗牌 */
    for (i = (uint32_t)elem_count - 1u; i > 0u; i--) {
        /* 生成 [0, i] 范围内的随机索引 */
        status = RNG_GetRange(0, i, &j, err_info);
        if (status != RNG_OK) {
            return status;
        }

        /* 交换 arr[i] 和 arr[j] */
        if (i != j) {
            memcpy(temp, arr + i * elem_size, elem_size);
            memcpy(arr + i * elem_size, arr + j * elem_size, elem_size);
            memcpy(arr + j * elem_size, temp, elem_size);
        }
    }

    return RNG_OK;
}

/**
 * @brief  打乱数组(Fisher-Yates洗牌算法)
 */
RNG_Status RNG_Shuffle(void *array, size_t elem_size, size_t elem_count, RNG_ErrorInfo *err_info)
{
    uint8_t scratch[RNG_SHUFFLE_STACK_SCRATCH_SIZE];

    if (elem_count > 1u && elem_size > RNG_SHUFFLE_STACK_SCRATCH_SIZE) {
        SET_ERROR(err_info, RNG_E_PARAM, HAL_ERROR, "Element size requires caller scratch buffer");
        return RNG_E_PARAM;
    }

    return RNG_ShuffleWithScratch(array,
                                  elem_size,
                                  elem_count,
                                  scratch,
                                  sizeof(scratch),
                                  err_info);
}

/**
 * @brief  获取错误描述字符串
 */
const char* RNG_GetErrorString(RNG_Status status)
{
    switch (status) {
        case RNG_OK:            return "Success";
        case RNG_E_PARAM:       return "Invalid parameter";
        case RNG_E_HAL:         return "HAL layer error";
        case RNG_E_TIMEOUT:     return "Operation timeout";
        case RNG_E_BUSY:        return "Device busy";
        case RNG_E_NOT_READY:   return "Device not ready";
        case RNG_E_SEED_ERROR:  return "Seed error";
        case RNG_E_CLOCK_ERROR: return "Clock error";
        case RNG_E_RETRY_FAILED:return "Retry failed";
        default:                return "Unknown error";
    }
}

/* ===== 统计功能实现 ===== */
#if RNG_ENABLE_STATS

/**
 * @brief  获取统计信息
 */
void RNG_GetStats(RNG_Stats *stats)
{
    if (stats != NULL) {
        memcpy(stats, &g_stats, sizeof(RNG_Stats));
    }
}

/**
 * @brief  重置统计信息
 */
void RNG_ResetStats(void)
{
    memset(&g_stats, 0, sizeof(RNG_Stats));
}

/**
 * @brief  打印统计信息
 */
void RNG_PrintStats(void)
{
#if RNG_DEBUG_ON
    printf("\n=== RNG Statistics ===\n");
    printf("Total requests:       %lu\n", g_stats.total_requests);
    printf("Total bytes:          %lu\n", g_stats.total_bytes);
    printf("Error count:          %lu\n", g_stats.error_count);
    printf("Retry count:          %lu\n", g_stats.retry_count);
    printf("Seed errors:          %lu\n", g_stats.seed_errors);
    printf("Clock errors:         %lu\n", g_stats.clock_errors);
    printf("Timeout count:        %lu\n", g_stats.timeout_count);
    printf("Max retry in request: %lu\n", g_stats.max_retry_in_request);

    if (g_stats.total_requests > 0) {
        float avg_retry = (float)g_stats.retry_count / g_stats.total_requests;
        printf("Average retry/req:    %.2f\n", avg_retry);
    }
    printf("======================\n\n");
#else
    RNG_INFO("Statistics available (enable RNG_DEBUG_ON to print)");
#endif
}

#endif /* RNG_ENABLE_STATS */
