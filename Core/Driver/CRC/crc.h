#pragma once
#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stddef.h>

/* ========= 断言策略：你可以替换成 Error_Handler / configASSERT ========= */
#ifndef CRC_ASSERT
  #define CRC_ASSERT(x) do { if (!(x)) { __BKPT(0); for(;;){} } } while (0)
#endif

/* ========= 基础操作 ========= */
static inline void CRC_Begin(CRC_HandleTypeDef *h)
{
    __HAL_CRC_DR_RESET(h);
}

static inline uint32_t CRC_Final(CRC_HandleTypeDef *h)
{
    return h->Instance->DR;
}

/* ========= 显式喂入：强制格式 ========= */
static inline void CRC_UpdateBytes(CRC_HandleTypeDef *h, const void *data, size_t nBytes)
{
    volatile uint8_t *dr8 = (volatile uint8_t *)&h->Instance->DR;
    const uint8_t *p = (const uint8_t *)data;
    while (nBytes--) *dr8 = *p++;
}

static inline void CRC_UpdateU16(CRC_HandleTypeDef *h, const uint16_t *data, size_t nHalfwords)
{
    CRC_ASSERT((((uintptr_t)data) & 1u) == 0u);
    volatile uint16_t *dr16 = (volatile uint16_t *)&h->Instance->DR;
    while (nHalfwords--) *dr16 = *data++;
}

static inline void CRC_UpdateU32(CRC_HandleTypeDef *h, const uint32_t *data, size_t nWords)
{
    CRC_ASSERT((((uintptr_t)data) & 3u) == 0u);
    volatile uint32_t *dr32 = (volatile uint32_t *)&h->Instance->DR;
    while (nWords--) *dr32 = *data++;
}

/* one-shot 显式 */
static inline uint32_t CRC_CalcBytes(CRC_HandleTypeDef *h, const void *data, size_t nBytes)
{
    CRC_Begin(h);
    CRC_UpdateBytes(h, data, nBytes);
    return CRC_Final(h);
}
static inline uint32_t CRC_CalcU16(CRC_HandleTypeDef *h, const uint16_t *data, size_t nHalfwords)
{
    CRC_Begin(h);
    CRC_UpdateU16(h, data, nHalfwords);
    return CRC_Final(h);
}
static inline uint32_t CRC_CalcU32(CRC_HandleTypeDef *h, const uint32_t *data, size_t nWords)
{
    CRC_Begin(h);
    CRC_UpdateU32(h, data, nWords);
    return CRC_Final(h);
}

/* ========= 自动选择格式的内部适配（统一签名：void* + units） ========= */
static inline void _CRC_Update_asBytes(CRC_HandleTypeDef *h, const void *p, size_t units)
{ CRC_UpdateBytes(h, p, units); }

static inline void _CRC_Update_asU16(CRC_HandleTypeDef *h, const void *p, size_t units)
{ CRC_UpdateU16(h, (const uint16_t *)p, units); }

static inline void _CRC_Update_asU32(CRC_HandleTypeDef *h, const void *p, size_t units)
{ CRC_UpdateU32(h, (const uint32_t *)p, units); }

static inline uint32_t _CRC_Calc_asBytes(CRC_HandleTypeDef *h, const void *p, size_t units)
{ return CRC_CalcBytes(h, p, units); }

static inline uint32_t _CRC_Calc_asU16(CRC_HandleTypeDef *h, const void *p, size_t units)
{ return CRC_CalcU16(h, (const uint16_t *)p, units); }

static inline uint32_t _CRC_Calc_asU32(CRC_HandleTypeDef *h, const void *p, size_t units)
{ return CRC_CalcU32(h, (const uint32_t *)p, units); }

/* ========= 通用工具 ========= */
#define CRC_COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))

/* int 在 STM32H743 上通常是 32-bit，建议保留断言 */
#define CRC_ASSERT_INT32() _Static_assert(sizeof(int) == 4, "Expected 32-bit int on STM32H743")

/* ========= “必须是数组”检测：语句版 & 表达式版 =========
   - 语句版：用于 do{...}while(0) 的宏
   - 表达式版：用于返回值表达式（CRC_CalcA）里
*/
#define CRC__EXPECT_ARRAY_STMT(arr) \
    _Static_assert( \
        !__builtin_types_compatible_p(__typeof__(arr), __typeof__(&(arr)[0])), \
        "Expected an array here. Use CRC_XXX(h, ptr, n) for pointer+length." \
    )

#define CRC__EXPECT_ARRAY_EXPR(arr) \
    ((void)sizeof(char[ \
        (!__builtin_types_compatible_p(__typeof__(arr), __typeof__(&(arr)[0]))) ? 1 : -1 \
    ]))

/* ========= 自动格式选择：按指针类型决定 bytes/u16/u32 ========= */
#define CRC__SELECT_UPDATE_FN(ptr) \
    _Generic((ptr), \
        uint8_t*:  _CRC_Update_asBytes,  const uint8_t*:  _CRC_Update_asBytes, \
        int8_t*:   _CRC_Update_asBytes,  const int8_t*:   _CRC_Update_asBytes, \
        uint16_t*: _CRC_Update_asU16,    const uint16_t*: _CRC_Update_asU16, \
        int16_t*:  _CRC_Update_asU16,    const int16_t*:  _CRC_Update_asU16, \
        uint32_t*: _CRC_Update_asU32,    const uint32_t*: _CRC_Update_asU32, \
        int32_t*:  _CRC_Update_asU32,    const int32_t*:  _CRC_Update_asU32, \
        int*:      _CRC_Update_asU32,    const int*:      _CRC_Update_asU32  \
    )

#define CRC__SELECT_CALC_FN(ptr) \
    _Generic((ptr), \
        uint8_t*:  _CRC_Calc_asBytes,  const uint8_t*:  _CRC_Calc_asBytes, \
        int8_t*:   _CRC_Calc_asBytes,  const int8_t*:   _CRC_Calc_asBytes, \
        uint16_t*: _CRC_Calc_asU16,    const uint16_t*: _CRC_Calc_asU16, \
        int16_t*:  _CRC_Calc_asU16,    const int16_t*:  _CRC_Calc_asU16, \
        uint32_t*: _CRC_Calc_asU32,    const uint32_t*: _CRC_Calc_asU32, \
        int32_t*:  _CRC_Calc_asU32,    const int32_t*:  _CRC_Calc_asU32, \
        int*:      _CRC_Calc_asU32,    const int*:      _CRC_Calc_asU32  \
    )

/* ========= 统一入口：同名支持(数组)和(指针+长度) =========
   - CRC_Update(h, arr)         自动长度
   - CRC_Update(h, ptr, n)      指定长度（单位=元素个数，随 ptr 类型）
   - CRC_Calc  同理
*/
#define CRC_Update(...) CRC__DISPATCH_UPDATE(__VA_ARGS__, CRC_UpdateN, CRC_UpdateA)(__VA_ARGS__)
#define CRC__DISPATCH_UPDATE(_1,_2,_3,NAME,...) NAME

#define CRC_Calc(...)   CRC__DISPATCH_CALC(__VA_ARGS__, CRC_CalcN, CRC_CalcA)(__VA_ARGS__)
#define CRC__DISPATCH_CALC(_1,_2,_3,NAME,...) NAME

/* ========= 数组版本：自动长度 + 编译期强制必须是数组 ========= */
#define CRC_UpdateA(h, arr) \
    do { \
        CRC__EXPECT_ARRAY_STMT(arr); \
        CRC__SELECT_UPDATE_FN(&(arr)[0])((h), &(arr)[0], CRC_COUNT_OF(arr)); \
    } while (0)

/* 这里必须用 EXPR 版断言，否则会出现你之前那个 expected expression before _Static_assert */
#define CRC_CalcA(h, arr) \
    ( CRC__EXPECT_ARRAY_EXPR(arr), \
      CRC__SELECT_CALC_FN(&(arr)[0])((h), &(arr)[0], CRC_COUNT_OF(arr)) )

/* ========= 指针+长度版本：长度 n 由你指定（单位=元素个数，随 ptr 类型） ========= */
#define CRC_UpdateN(h, ptr, n) \
    do { \
        CRC__SELECT_UPDATE_FN(ptr)((h), (ptr), (n)); \
    } while (0)

#define CRC_CalcN(h, ptr, n) \
    ( CRC__SELECT_CALC_FN(ptr)((h), (ptr), (n)) )
