#pragma once
#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== W25Q256JV (4字节地址模式) 指令定义 ===== */
#define RESET_ENABLE_CMD                     0x66
#define RESET_MEMORY_CMD                     0x99

#define READ_ID_CMD                          0x90
#define READ_JEDEC_ID_CMD                    0x9F

#define READ_STATUS_REG1_CMD                 0x05
#define READ_STATUS_REG2_CMD                 0x35
#define READ_STATUS_REG3_CMD                 0x15

#define WRITE_STATUS_REG1_CMD                0x01
#define WRITE_STATUS_REG2_CMD                0x31
#define WRITE_STATUS_REG3_CMD                0x11

#define WRITE_ENABLE_CMD                     0x06
#define WRITE_DISABLE_CMD                    0x04

#define READ_CMD_4BYTE                       0x03      /* 普通读(4字节地址) */
#define QUAD_INOUT_FAST_READ_CMD_4BYTE       0xEC      /* 四线快速读 */
#define QUAD_INPUT_PAGE_PROG_CMD_4BYTE       0x34      /* 四线页编程 */

#define SECTOR_ERASE_CMD_4BYTE               0x21      /* 4KB擦除 */
#define BLOCK64K_ERASE_CMD_4BYTE             0xDC      /* 64KB块擦除 */
#define CHIP_ERASE_CMD                       0xC7      /* 整片擦除 */

#define ENTER_4_BYTE_ADDR_MODE_CMD           0xB7      /* 进入4字节地址模式 */

#define W25Q_FSR_BUSY                        ((uint8_t)0x01)  /* 状态寄存器忙标志 */
#define W25Q_FSR_WREN                        ((uint8_t)0x02)  /* 写使能标志 */
#define W25Q_SR2_QE                          ((uint8_t)0x02)  /* 四线使能标志 */

/* ===== QSPI Memory-Mapped 模式基地址(STM32H7默认) ===== */
#ifndef FLASH_MM_BASE
#define FLASH_MM_BASE 0x90000000u
#endif

/* ===== 错误码定义 ===== */
typedef enum {
    FLASH_OK = 0,              /* 操作成功 */

    FLASH_E_PARAM      = 1,    /* 参数错误 */
    FLASH_E_RANGE      = 2,    /* 地址范围错误 */
    FLASH_E_ALIGN      = 3,    /* 地址对齐错误 */
    FLASH_E_HAL        = 4,    /* HAL层错误 */
    FLASH_E_TIMEOUT    = 5,    /* 操作超时 */
    FLASH_E_ID_MISMATCH= 6,    /* 芯片ID不匹配 */
} FLASH_Status;

/* ===== 错误信息结构体 ===== */
typedef struct {
    FLASH_Status      code;        /* 错误代码 */
    HAL_StatusTypeDef hal;         /* HAL状态 */
    uint32_t          qspi_error;  /* QSPI错误码 */
    const char       *step;        /* 错误发生的步骤描述 */
    uint32_t          line;        /* 源代码行号 */
    uint32_t          addr;        /* 相关地址 */
    uint32_t          len;         /* 相关长度 */
} FLASH_ErrorInfo;

/* ===== FLASH驱动句柄 ===== */
typedef struct {
    /* QSPI外设句柄(由HAL库初始化后传入) */
    QSPI_HandleTypeDef *qspi;

    /* Flash芯片参数(支持单/双Flash配置) */
    uint32_t total_size_bytes;     /* 总容量(字节), 例如: 64MB或128MB(双片) */
    uint32_t erase4k_bytes;        /* 4KB擦除块大小(通常4096) */
    uint32_t erase64k_bytes;       /* 64KB擦除块大小(通常65536) */
    uint32_t page_bytes;           /* 页大小(通常256) */

    uint8_t  dummy_cycles_fast_read; /* 快速读取的空周期数(通常6) */

    /* 运行时状态 */
    bool     memory_mapped;        /* 是否处于Memory-Mapped模式 */
    uint32_t mm_base;              /* Memory-Mapped基地址 */

    FLASH_ErrorInfo last;          /* 最后一次错误信息 */
} FLASH_Handle;

/* ===== 核心API ===== */

/**
 * @brief  绑定QSPI外设句柄并初始化Flash驱动
 * @param  h: Flash驱动句柄
 * @param  hqspi: QSPI外设句柄(需要已通过HAL初始化)
 * @param  total_size_bytes: Flash总容量(单片32MB传32*1024*1024, 双片传64*1024*1024)
 * @retval FLASH_Status
 * @note   此函数仅绑定句柄,不操作硬件
 */
FLASH_Status FLASH_Open(FLASH_Handle *h, QSPI_HandleTypeDef *hqspi,
                        uint32_t total_size_bytes);

/**
 * @brief  Flash芯片上电后配置(复位/使能QE/进入4字节地址模式)
 * @param  h: Flash驱动句柄
 * @retval FLASH_Status
 * @note   系统初始化时调用一次即可
 */
FLASH_Status FLASH_BringUp(FLASH_Handle *h);

/**
 * @brief  复位Flash芯片
 * @param  h: Flash驱动句柄
 * @retval FLASH_Status
 */
FLASH_Status FLASH_Reset(FLASH_Handle *h);

/**
 * @brief  读取JEDEC ID
 * @param  h: Flash驱动句柄
 * @param  jedec: 输出JEDEC ID(例如0xEF4019表示Winbond W25Q256)
 * @retval FLASH_Status
 */
FLASH_Status FLASH_ReadJEDEC(FLASH_Handle *h, uint32_t *jedec);

/**
 * @brief  读取设备ID
 * @param  h: Flash驱动句柄
 * @param  dev_id: 输出设备ID
 * @retval FLASH_Status
 */
FLASH_Status FLASH_ReadDeviceID(FLASH_Handle *h, uint16_t *dev_id);

/**
 * @brief  普通读取(单线读取)
 * @param  h: Flash驱动句柄
 * @param  addr: 读取起始地址
 * @param  buf: 数据缓冲区
 * @param  len: 读取长度
 * @retval FLASH_Status
 */
FLASH_Status FLASH_Read(FLASH_Handle *h, uint32_t addr, void *buf, uint32_t len);

/**
 * @brief  四线快速读取
 * @param  h: Flash驱动句柄
 * @param  addr: 读取起始地址
 * @param  buf: 数据缓冲区
 * @param  len: 读取长度
 * @retval FLASH_Status
 * @note   读取速度比普通读快,需要QE使能
 */
FLASH_Status FLASH_ReadFastQuad(FLASH_Handle *h, uint32_t addr, void *buf, uint32_t len);

/**
 * @brief  页编程(自动处理跨页)
 * @param  h: Flash驱动句柄
 * @param  addr: 编程起始地址
 * @param  buf: 数据缓冲区
 * @param  len: 编程长度
 * @retval FLASH_Status
 * @note   会自动分页处理,无需手动对齐
 */
FLASH_Status FLASH_Prog(FLASH_Handle *h, uint32_t addr, const void *buf, uint32_t len);

/**
 * @brief  4KB扇区擦除
 * @param  h: Flash驱动句柄
 * @param  addr: 擦除地址(必须4KB对齐)
 * @retval FLASH_Status
 */
FLASH_Status FLASH_Erase4K(FLASH_Handle *h, uint32_t addr);

/**
 * @brief  64KB块擦除
 * @param  h: Flash驱动句柄
 * @param  addr: 擦除地址(必须64KB对齐)
 * @retval FLASH_Status
 */
FLASH_Status FLASH_Erase64K(FLASH_Handle *h, uint32_t addr);

/**
 * @brief  整片擦除
 * @param  h: Flash驱动句柄
 * @retval FLASH_Status
 * @warning 此操作会擦除整个Flash,谨慎使用!
 */
FLASH_Status FLASH_EraseChip(FLASH_Handle *h);

/**
 * @brief  使能Memory-Mapped模式(可直接从0x90000000地址读取)
 * @param  h: Flash驱动句柄
 * @retval FLASH_Status
 * @note   进入此模式后无法写入/擦除,需先调用FLASH_DisableMemoryMapped
 */
FLASH_Status FLASH_EnableMemoryMapped(FLASH_Handle *h);

/**
 * @brief  禁用Memory-Mapped模式
 * @param  h: Flash驱动句柄
 * @retval FLASH_Status
 */
FLASH_Status FLASH_DisableMemoryMapped(FLASH_Handle *h);

/**
 * @brief  获取最后一次错误信息
 * @param  h: Flash驱动句柄
 * @retval 错误信息指针
 */
const FLASH_ErrorInfo *FLASH_LastError(FLASH_Handle *h);

#ifdef __cplusplus
}
#endif