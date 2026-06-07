/**
 * @file    lfs_port.c
 * @brief   littlefs 文件系统移植实现
 * @details 为STM32H7 QSPI Flash提供littlefs文件系统支持
 *          支持单片/双片Flash配置
 */

#include "lfs.h"
#include "flash.h"
#include "sdram_cold_pool.h"
#include <string.h>
#include <stdint.h>

/* ==================== 分区配置 ==================== */

/**
 * littlefs分区配置说明:
 * - 外部QSPI Flash线性地址空间: 0x00000000 ~ 0x03FFFFFF (64MiB双片)
 * - littlefs分区: 使用后48MiB空间
 * - 前16MiB可用于其他用途(bootloader、应用程序、资源文件等)
 */

#ifndef LFS_PART_BASE
#define LFS_PART_BASE   0x01000000u  /* littlefs分区起始地址 (16MB偏移) */
#endif

#ifndef LFS_PART_SIZE
#define LFS_PART_SIZE   0x03000000u  /* littlefs分区大小 (48MB) */
#endif

/**
 * 块设备参数配置:
 * - 双片Flash模式下:
 *   - 擦除块 = 4KB×2 = 8KB (两片同时擦除)
 *   - 编程页 = 256B×2 = 512B (两片同时编程)
 * - 单片Flash模式下修改为:
 *   - LFS_BLOCK_SIZE  = 4096
 *   - LFS_PROG_SIZE   = 256
 */
#ifndef LFS_BLOCK_SIZE
#define LFS_BLOCK_SIZE  8192u  /* 擦除块大小 (双片: 4KB×2) */
#endif

#ifndef LFS_PROG_SIZE
#define LFS_PROG_SIZE   512u   /* 编程粒度 (双片: 256B×2) */
#endif

#ifndef LFS_READ_SIZE
#define LFS_READ_SIZE   64u    /* 读取粒度 (建议32-256字节) */
#endif

#ifndef LFS_CACHE_SIZE
#define LFS_CACHE_SIZE  LFS_PROG_SIZE  /* 缓存大小 (建议等于编程粒度) */
#endif

#ifndef LFS_LOOKAHEAD_SIZE
#define LFS_LOOKAHEAD_SIZE  64u  /* 前瞻缓冲区大小 (字节, 必须是8的倍数) */
#endif

/* ==================== 全局对象 ==================== */

/** littlefs文件系统实例 */
lfs_t g_lfs;

/**
 * 缓冲区说明:
 * - 从 SDRAM cold pool 分配，32字节对齐 (对Cortex-M7 cache友好)
 * - read_buf: 读缓存
 * - prog_buf: 编程缓存
 * - lookahead: 块分配前瞻缓冲区
 */
static uint8_t *g_read_buf = NULL;
static uint8_t *g_prog_buf = NULL;
static uint8_t *g_lookahead = NULL;

/** Flash驱动句柄 (由外部绑定) */
static FLASH_Handle *s_flash = NULL;

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief  计算Flash物理地址
 * @param  c: littlefs配置
 * @param  block: 块号
 * @param  off: 块内偏移
 * @retval Flash物理地址
 */
static inline uint32_t lfs_addr(const struct lfs_config *c, lfs_block_t block, lfs_off_t off)
{
    (void)c;
    return (uint32_t)(LFS_PART_BASE + (uint32_t)block * (uint32_t)LFS_BLOCK_SIZE + (uint32_t)off);
}

/* ==================== littlefs 块设备回调函数 ==================== */

/**
 * @brief  读取块设备数据
 * @param  c: littlefs配置
 * @param  block: 块号
 * @param  off: 块内偏移
 * @param  buffer: 数据缓冲区
 * @param  size: 读取大小
 * @retval 0=成功, LFS_ERR_IO=失败
 */
static int lfs_bd_read(const struct lfs_config *c,
                       lfs_block_t block, lfs_off_t off,
                       void *buffer, lfs_size_t size)
{
    FLASH_Handle *h = (FLASH_Handle *)c->context;
    uint32_t addr = lfs_addr(c, block, off);

    /* 如果已开启Memory-Mapped模式，直接从映射地址读取 (速度更快) */
    if (h && h->memory_mapped) {
        memcpy(buffer, (const void *)(uintptr_t)(h->mm_base + addr), size);
        return 0;
    }

    /* 否则使用间接读取 */
    return (FLASH_Read(h, addr, buffer, (uint32_t)size) == FLASH_OK) ? 0 : LFS_ERR_IO;
}

/**
 * @brief  编程块设备数据
 * @param  c: littlefs配置
 * @param  block: 块号
 * @param  off: 块内偏移
 * @param  buffer: 数据缓冲区
 * @param  size: 编程大小
 * @retval 0=成功, LFS_ERR_IO=失败, LFS_ERR_INVAL=参数错误
 * @note   littlefs保证size和off按prog_size对齐
 */
static int lfs_bd_prog(const struct lfs_config *c,
                       lfs_block_t block, lfs_off_t off,
                       const void *buffer, lfs_size_t size)
{
    FLASH_Handle *h = (FLASH_Handle *)c->context;
    uint32_t addr = lfs_addr(c, block, off);

    /* 检查对齐 (可选，littlefs已保证) */
    if ((off % LFS_PROG_SIZE) || (size % LFS_PROG_SIZE)) {
        return LFS_ERR_INVAL;
    }

    return (FLASH_Prog(h, addr, buffer, (uint32_t)size) == FLASH_OK) ? 0 : LFS_ERR_IO;
}

/**
 * @brief  擦除块设备
 * @param  c: littlefs配置
 * @param  block: 块号
 * @retval 0=成功, LFS_ERR_IO=失败
 * @note   双片模式: 擦除8KB块 = 两次4KB擦除
 *         单片模式: 擦除4KB块 = 一次4KB擦除
 */
static int lfs_bd_erase(const struct lfs_config *c, lfs_block_t block)
{
    FLASH_Handle *h = (FLASH_Handle *)c->context;
    uint32_t addr = lfs_addr(c, block, 0);

    /* 双片模式: 线性8KB块需要两次4KB擦除 */
#if (LFS_BLOCK_SIZE == 8192)
    if (FLASH_Erase4K(h, addr) != FLASH_OK) return LFS_ERR_IO;
    if (FLASH_Erase4K(h, addr + 4096u) != FLASH_OK) return LFS_ERR_IO;
    return 0;
#else
    /* 单片模式: 直接擦除4KB */
    return (FLASH_Erase4K(h, addr) == FLASH_OK) ? 0 : LFS_ERR_IO;
#endif
}

/**
 * @brief  同步块设备 (将缓存写入Flash)
 * @param  c: littlefs配置
 * @retval 0=成功
 * @note   NOR Flash无需额外同步操作
 */
static int lfs_bd_sync(const struct lfs_config *c)
{
    (void)c;
    /* NOR Flash写入即完成，无需额外flush操作 */
    return 0;
}

/* ==================== littlefs 配置对象 ==================== */

static struct lfs_config g_cfg = {
    .context        = NULL,  /* 将在绑定时设置为Flash句柄 */

    /* 块设备操作回调 */
    .read           = lfs_bd_read,
    .prog           = lfs_bd_prog,
    .erase          = lfs_bd_erase,
    .sync           = lfs_bd_sync,

    /* 块设备几何参数 */
    .read_size      = LFS_READ_SIZE,      /* 读取粒度 */
    .prog_size      = LFS_PROG_SIZE,      /* 编程粒度 */
    .block_size     = LFS_BLOCK_SIZE,     /* 擦除块大小 */
    .block_count    = (LFS_PART_SIZE / LFS_BLOCK_SIZE),  /* 块数量 */
    .block_cycles   = 500,                /* 磨损均衡周期 (可根据需要调整) */

    /* 缓存配置 */
    .cache_size     = LFS_CACHE_SIZE,
    .lookahead_size = LFS_LOOKAHEAD_SIZE,

    /* 缓冲区 */
    .read_buffer    = NULL,
    .prog_buffer    = NULL,
    .lookahead_buffer = NULL,
};

static int lfs_cold_buffers_init(void)
{
    if (g_read_buf != NULL && g_prog_buf != NULL && g_lookahead != NULL) {
        return 0;
    }

    g_read_buf = (uint8_t *)cold_calloc(1u, LFS_CACHE_SIZE, 32u);
    g_prog_buf = (uint8_t *)cold_calloc(1u, LFS_CACHE_SIZE, 32u);
    g_lookahead = (uint8_t *)cold_calloc(1u, LFS_LOOKAHEAD_SIZE, 32u);

    if (g_read_buf == NULL || g_prog_buf == NULL || g_lookahead == NULL) {
        return -1;
    }

    g_cfg.read_buffer = g_read_buf;
    g_cfg.prog_buffer = g_prog_buf;
    g_cfg.lookahead_buffer = g_lookahead;

    return 0;
}

/* ==================== 外部接口实现 ==================== */

/**
 * @brief  绑定Flash驱动
 */
int LFS_PortBind(FLASH_Handle *flash)
{
    if (!flash) return -1;
    if (lfs_cold_buffers_init() != 0) return -1;

    s_flash = flash;
    g_cfg.context = flash;

    return 0;
}

/**
 * @brief  挂载或格式化文件系统
 */
int LFS_MountOrFormat(void)
{
    if (!s_flash) return LFS_ERR_INVAL;

    /* 尝试挂载 */
    int err = lfs_mount(&g_lfs, &g_cfg);
    if (err) {
        /* 挂载失败，可能是首次使用或文件系统损坏 */
        /* 执行格式化 */
        err = lfs_format(&g_lfs, &g_cfg);
        if (err) return err;

        /* 重新挂载 */
        err = lfs_mount(&g_lfs, &g_cfg);
        if (err) return err;
    }

    return 0;
}

/**
 * @brief  卸载文件系统
 */
int LFS_Unmount(void)
{
    return lfs_unmount(&g_lfs);
}

/**
 * @brief  使能/禁用Memory-Mapped读取
 */
int LFS_EnableMappedRead(int enable)
{
    if (!s_flash) return -1;

    if (enable) {
        return (FLASH_EnableMemoryMapped(s_flash) == FLASH_OK) ? 0 : -1;
    } else {
        return (FLASH_DisableMemoryMapped(s_flash) == FLASH_OK) ? 0 : -1;
    }
}

/**
 * @brief  获取配置信息
 */
const struct lfs_config* LFS_Config(void)
{
    return &g_cfg;
}
