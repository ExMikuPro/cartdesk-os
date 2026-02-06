/**
* @file    lfs_port.h
 * @brief   littlefs 文件系统移植接口头文件
 * @details 为STM32H7 QSPI Flash提供littlefs文件系统支持
 * @author  优化版本
 * @date    2025
 */

#pragma once
#include "lfs.h"
#include "flash.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* ==================== 全局对象 ==================== */

    /** littlefs 文件系统实例 (全局单例) */
    extern lfs_t g_lfs;

    /* ==================== 移植接口API ==================== */

    /**
     * @brief  绑定Flash驱动到littlefs
     * @param  flash: Flash驱动句柄指针
     * @retval 0=成功, -1=失败
     * @note   必须在挂载前调用此函数绑定Flash驱动
     */
    int LFS_PortBind(FLASH_Handle *flash);

    /**
     * @brief  挂载或格式化文件系统
     * @retval 0=成功, 其他=littlefs错误码
     * @note   - 首次使用时会自动格式化
     *         - 如果挂载失败会尝试格式化后重新挂载
     *         - 调用前必须先调用 LFS_PortBind
     */
    int LFS_MountOrFormat(void);

    /**
     * @brief  卸载文件系统
     * @retval 0=成功, 其他=littlefs错误码
     * @note   卸载前会自动同步所有未写入数据
     */
    int LFS_Unmount(void);

    /**
     * @brief  使能/禁用Memory-Mapped读取模式
     * @param  enable: 1=使能, 0=禁用
     * @retval 0=成功, -1=失败
     * @note   - 使能后读取速度更快(直接从0x90000000地址读取)
     *         - 写入/擦除时会自动禁用映射模式
     *         - 仅用于调试或性能优化
     */
    int LFS_EnableMappedRead(int enable);

    /**
     * @brief  获取littlefs配置信息
     * @retval littlefs配置结构体指针
     * @note   可用于调试，查看分区大小、块大小等信息
     */
    const struct lfs_config* LFS_Config(void);

#ifdef __cplusplus
}
#endif