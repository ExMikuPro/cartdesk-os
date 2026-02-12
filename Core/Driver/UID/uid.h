#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

  /* ========= 设备标识：UID（96-bit，12字节） ========= */
  typedef struct {
    uint32_t w0;
    uint32_t w1;
    uint32_t w2;
  } bsp_uid96_t;

  /* 读取 UID（三个 32-bit word） */
  bsp_uid96_t BSP_UID_Read96(void);

  /* 读取 UID 原始 12 字节（小端拷贝 w0,w1,w2） */
  void BSP_UID_ReadBytes(uint8_t out12[12]);

  /* UID 转 24位 HEX 字符串（大写），out 至少 25 bytes（含 '\0'） */
  void BSP_UID_ToHex(char out25[25]);

  /* ========= 芯片信息：DEVID/REVID（不唯一，用于识别型号/修订） ========= */
  uint32_t BSP_Chip_GetDEVID(void);
  uint32_t BSP_Chip_GetREVID(void);

  /* ========= 可选：生成“短序列号”（基于 UID + salt 的 CRC32，再 Base32 编码）
     - 不是加密/防伪，只是“短且稳定的可读ID”
     - out 至少 14 bytes（13字符 + '\0'） => 65-bit Base32 可覆盖 CRC32 + devid/revid/salt
  */
  void BSP_UID_MakeShortID_Base32(char out14[14], uint32_t salt);

  /* ========= 可选：FNV-1a 32-bit hash（如果你要把 string key 映射成整数 key） ========= */
  uint32_t BSP_Hash_FNV1a32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif
