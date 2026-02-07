/**
 ******************************************************************************
 * @file    eeprom.c
 * @brief   AT24Cxx EEPROM驱动实现 (优化版本)
 * @note    完善的错误处理、参数校验、统计功能
 ******************************************************************************
 */

#include "eeprom.h"
#include "i2c.h"
#include <string.h>

/* ===== 私有宏定义 ===== */
#define SET_ERROR(info, err_code, hal_status, step_str) \
    do { \
        if ((info) != NULL) { \
            (info)->code = (err_code); \
            (info)->hal = (hal_status); \
            (info)->i2c_error = HAL_I2C_GetError(&hi2c1); \
            (info)->step = (step_str); \
            (info)->line = __LINE__; \
            (info)->addr = 0; \
            (info)->len = 0; \
        } \
        EEPROM_ERROR("%s at line %d: %s", step_str, __LINE__, EEPROM_GetErrorString(err_code)); \
        g_stats.error_count++; \
    } while(0)

#define SET_ERROR_EX(info, err_code, hal_status, step_str, address, length) \
    do { \
        if ((info) != NULL) { \
            (info)->code = (err_code); \
            (info)->hal = (hal_status); \
            (info)->i2c_error = HAL_I2C_GetError(&hi2c1); \
            (info)->step = (step_str); \
            (info)->line = __LINE__; \
            (info)->addr = (address); \
            (info)->len = (length); \
        } \
        EEPROM_ERROR("%s at line %d: %s (addr=0x%02X, len=%u)", \
                     step_str, __LINE__, EEPROM_GetErrorString(err_code), address, length); \
        g_stats.error_count++; \
    } while(0)

/* ===== 私有变量 ===== */
static EEPROM_Stats g_stats = {0}; /* 操作统计 */

/* ===== 私有函数声明 ===== */
static EEPROM_Status WaitI2CReady(EEPROM_ErrorInfo *err_info);

static EEPROM_Status WaitDeviceReady(EEPROM_ErrorInfo *err_info);

/* ===== 私有函数实现 ===== */

/**
 * @brief  等待I2C总线就绪
 * @param  err_info: 错误信息输出
 * @retval EEPROM_Status状态码
 */
static EEPROM_Status WaitI2CReady(EEPROM_ErrorInfo *err_info) {
  uint32_t timeout = EEPROM_TIMEOUT_MS;
  uint32_t tickstart = HAL_GetTick();

  while (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY) {
    if ((HAL_GetTick() - tickstart) > timeout) {
      SET_ERROR(err_info, EEPROM_E_TIMEOUT, HAL_TIMEOUT, "Wait I2C ready timeout");
      g_stats.timeout_count++;
      return EEPROM_E_TIMEOUT;
    }
  }

  return EEPROM_OK;
}

/**
 * @brief  等待EEPROM设备就绪
 * @param  err_info: 错误信息输出
 * @retval EEPROM_Status状态码
 */
static EEPROM_Status WaitDeviceReady(EEPROM_ErrorInfo *err_info) {
  HAL_StatusTypeDef hal_status;

  /* 检查设备是否就绪 */
  hal_status = HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_ADDRESS,
                                     EEPROM_MAX_TRIALS,
                                     EEPROM_READY_TIMEOUT_MS);

  if (hal_status == HAL_TIMEOUT) {
    SET_ERROR(err_info, EEPROM_E_TIMEOUT, hal_status, "Device not ready (timeout)");
    g_stats.timeout_count++;
    return EEPROM_E_TIMEOUT;
  } else if (hal_status != HAL_OK) {
    SET_ERROR(err_info, EEPROM_E_HAL, hal_status, "Device ready check failed");
    return EEPROM_E_HAL;
  }

  /* 等待I2C总线就绪 */
  return WaitI2CReady(err_info);
}

/* ===== 公共函数实现 ===== */

/**
 * @brief  初始化EEPROM
 */
EEPROM_Status EEPROM_Init(EEPROM_ErrorInfo *err_info) {
  EEPROM_DEBUG("Initializing EEPROM...");

  /* 重置统计信息 */
  memset(&g_stats, 0, sizeof(g_stats));

  /* 检查设备是否存在 */
  EEPROM_Status status = EEPROM_IsReady(err_info);

  if (status == EEPROM_OK) {
    EEPROM_INFO("EEPROM initialized successfully");
  }

  return status;
}

/**
 * @brief  检查EEPROM是否就绪
 */
EEPROM_Status EEPROM_IsReady(EEPROM_ErrorInfo *err_info) {
  HAL_StatusTypeDef hal_status;

  hal_status = HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_ADDRESS,
                                     EEPROM_MAX_TRIALS,
                                     EEPROM_READY_TIMEOUT_MS);

  if (hal_status == HAL_TIMEOUT) {
    SET_ERROR(err_info, EEPROM_E_TIMEOUT, hal_status, "Device not present or not ready");
    return EEPROM_E_TIMEOUT;
  } else if (hal_status != HAL_OK) {
    SET_ERROR(err_info, EEPROM_E_HAL, hal_status, "Device check failed");
    return EEPROM_E_HAL;
  }

  return EEPROM_OK;
}

/**
 * @brief  写入单个字节
 */
EEPROM_Status EEPROM_WriteByte(uint8_t addr, uint8_t data, EEPROM_ErrorInfo *err_info) {
  HAL_StatusTypeDef hal_status;

  /* 参数校验 */
  if (addr >= EEPROM_SIZE) {
    SET_ERROR_EX(err_info, EEPROM_E_RANGE, HAL_ERROR,
                 "Address out of range", addr, 1);
    return EEPROM_E_RANGE;
  }

  EEPROM_DEBUG("Write byte: addr=0x%02X, data=0x%02X", addr, data);

  /* 执行写操作 */
  hal_status = HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDRESS, addr,
                                 I2C_MEMADD_SIZE_8BIT, &data, 1,
                                 EEPROM_TIMEOUT_MS);

  if (hal_status != HAL_OK) {
    SET_ERROR_EX(err_info, EEPROM_E_HAL, hal_status,
                 "Byte write failed", addr, 1);
    return EEPROM_E_HAL;
  }

  /* 等待I2C就绪 */
  EEPROM_Status status = WaitI2CReady(err_info);
  if (status != EEPROM_OK) {
    return status;
  }

  /* 等待设备就绪 */
  status = WaitDeviceReady(err_info);
  if (status != EEPROM_OK) {
    return status;
  }

  g_stats.write_count++;
  return EEPROM_OK;
}

/**
 * @brief  写入页数据(单页写入,不跨页)
 */
EEPROM_Status EEPROM_WritePage(uint8_t addr, const uint8_t *pBuffer,
                               uint8_t len, EEPROM_ErrorInfo *err_info) {
  HAL_StatusTypeDef hal_status;

  /* 参数校验 */
  if (pBuffer == NULL) {
    SET_ERROR(err_info, EEPROM_E_PARAM, HAL_ERROR, "Buffer is NULL");
    return EEPROM_E_PARAM;
  }

  if (len == 0) {
    SET_ERROR(err_info, EEPROM_E_PARAM, HAL_ERROR, "Length is zero");
    return EEPROM_E_PARAM;
  }

  if (len > EEPROM_PAGESIZE) {
    SET_ERROR_EX(err_info, EEPROM_E_PARAM, HAL_ERROR,
                 "Length exceeds page size", addr, len);
    return EEPROM_E_PARAM;
  }

  if (addr >= EEPROM_SIZE || (addr + len) > EEPROM_SIZE) {
    SET_ERROR_EX(err_info, EEPROM_E_RANGE, HAL_ERROR,
                 "Address range out of bounds", addr, len);
    return EEPROM_E_RANGE;
  }

  /* 检查是否跨页 */
  uint8_t page_start = addr / EEPROM_PAGESIZE;
  uint8_t page_end = (addr + len - 1) / EEPROM_PAGESIZE;
  if (page_start != page_end) {
    SET_ERROR_EX(err_info, EEPROM_E_PARAM, HAL_ERROR,
                 "Page write crosses page boundary", addr, len);
    return EEPROM_E_PARAM;
  }

  EEPROM_DEBUG("Write page: addr=0x%02X, len=%u", addr, len);

  /* 执行写操作 */
  hal_status = HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDRESS, addr,
                                 I2C_MEMADD_SIZE_8BIT, (uint8_t *) pBuffer, len,
                                 EEPROM_TIMEOUT_MS);

  if (hal_status != HAL_OK) {
    SET_ERROR_EX(err_info, EEPROM_E_HAL, hal_status,
                 "Page write failed", addr, len);
    return EEPROM_E_HAL;
  }

  /* 等待I2C就绪 */
  EEPROM_Status status = WaitI2CReady(err_info);
  if (status != EEPROM_OK) {
    return status;
  }

  /* 等待设备就绪 */
  status = WaitDeviceReady(err_info);
  if (status != EEPROM_OK) {
    return status;
  }

  g_stats.write_count++;
  return EEPROM_OK;
}

/**
 * @brief  写入任意长度数据(自动处理跨页)
 */
EEPROM_Status EEPROM_Write(uint8_t addr, const uint8_t *pBuffer,
                           uint16_t len, EEPROM_ErrorInfo *err_info) {
  EEPROM_Status status;
  uint8_t page_offset;
  uint8_t first_write_len;
  uint16_t remaining_len;
  uint8_t current_addr;
  const uint8_t *current_buffer;

  /* 参数校验 */
  if (pBuffer == NULL) {
    SET_ERROR(err_info, EEPROM_E_PARAM, HAL_ERROR, "Buffer is NULL");
    return EEPROM_E_PARAM;
  }

  if (len == 0) {
    SET_ERROR(err_info, EEPROM_E_PARAM, HAL_ERROR, "Length is zero");
    return EEPROM_E_PARAM;
  }

  if (addr >= EEPROM_SIZE || (addr + len) > EEPROM_SIZE) {
    SET_ERROR_EX(err_info, EEPROM_E_RANGE, HAL_ERROR,
                 "Address range out of bounds", addr, len);
    return EEPROM_E_RANGE;
  }

  EEPROM_INFO("Write buffer: addr=0x%02X, len=%u", addr, len);

  current_addr = addr;
  current_buffer = pBuffer;
  remaining_len = len;

  /* 计算第一次写入的长度(对齐到页边界) */
  page_offset = current_addr % EEPROM_PAGESIZE;

  if (page_offset != 0) {
    /* 地址未对齐,先写入到页边界 */
    first_write_len = EEPROM_PAGESIZE - page_offset;
    if (first_write_len > remaining_len) {
      first_write_len = remaining_len;
    }

    status = EEPROM_WritePage(current_addr, current_buffer, first_write_len, err_info);
    if (status != EEPROM_OK) {
      return status;
    }

    current_addr += first_write_len;
    current_buffer += first_write_len;
    remaining_len -= first_write_len;
  }

  /* 写入完整的页 */
  while (remaining_len >= EEPROM_PAGESIZE) {
    status = EEPROM_WritePage(current_addr, current_buffer, EEPROM_PAGESIZE, err_info);
    if (status != EEPROM_OK) {
      return status;
    }

    current_addr += EEPROM_PAGESIZE;
    current_buffer += EEPROM_PAGESIZE;
    remaining_len -= EEPROM_PAGESIZE;
  }

  /* 写入剩余的字节 */
  if (remaining_len > 0) {
    status = EEPROM_WritePage(current_addr, current_buffer, remaining_len, err_info);
    if (status != EEPROM_OK) {
      return status;
    }
  }

  EEPROM_INFO("Write completed successfully");
  return EEPROM_OK;
}

/**
 * @brief  读取任意长度数据
 */
EEPROM_Status EEPROM_Read(uint8_t addr, uint8_t *pBuffer,
                          uint16_t len, EEPROM_ErrorInfo *err_info) {
  HAL_StatusTypeDef hal_status;

  /* 参数校验 */
  if (pBuffer == NULL) {
    SET_ERROR(err_info, EEPROM_E_PARAM, HAL_ERROR, "Buffer is NULL");
    return EEPROM_E_PARAM;
  }

  if (len == 0) {
    SET_ERROR(err_info, EEPROM_E_PARAM, HAL_ERROR, "Length is zero");
    return EEPROM_E_PARAM;
  }

  if (addr >= EEPROM_SIZE || (addr + len) > EEPROM_SIZE) {
    SET_ERROR_EX(err_info, EEPROM_E_RANGE, HAL_ERROR,
                 "Address range out of bounds", addr, len);
    return EEPROM_E_RANGE;
  }

  EEPROM_DEBUG("Read: addr=0x%02X, len=%u", addr, len);

  /* 执行读操作 */
  hal_status = HAL_I2C_Mem_Read(&hi2c1, EEPROM_ADDRESS, addr,
                                I2C_MEMADD_SIZE_8BIT, pBuffer, len,
                                EEPROM_TIMEOUT_MS * (len / 100 + 1));

  if (hal_status != HAL_OK) {
    SET_ERROR_EX(err_info, EEPROM_E_HAL, hal_status,
                 "Read failed", addr, len);
    return EEPROM_E_HAL;
  }

  g_stats.read_count++;
  return EEPROM_OK;
}

/**
 * @brief  擦除整个EEPROM(写入0xFF)
 */
EEPROM_Status EEPROM_EraseChip(EEPROM_ErrorInfo *err_info) {
  uint8_t erase_buffer[EEPROM_PAGESIZE];
  EEPROM_Status status;
  uint8_t addr;

  EEPROM_INFO("Erasing chip...");

  /* 准备擦除数据(全FF) */
  memset(erase_buffer, 0xFF, EEPROM_PAGESIZE);

  /* 按页擦除 */
  for (addr = 0; addr < EEPROM_SIZE; addr += EEPROM_PAGESIZE) {
    status = EEPROM_WritePage(addr, erase_buffer, EEPROM_PAGESIZE, err_info);
    if (status != EEPROM_OK) {
      EEPROM_ERROR("Chip erase failed at address 0x%02X", addr);
      return status;
    }
  }

  EEPROM_INFO("Chip erased successfully");
  return EEPROM_OK;
}

/**
 * @brief  擦除指定区域(写入0xFF)
 */
EEPROM_Status EEPROM_Erase(uint8_t addr, uint16_t len, EEPROM_ErrorInfo *err_info) {
  uint8_t erase_buffer[EEPROM_PAGESIZE];

  /* 参数校验 */
  if (len == 0) {
    SET_ERROR(err_info, EEPROM_E_PARAM, HAL_ERROR, "Length is zero");
    return EEPROM_E_PARAM;
  }

  if (addr >= EEPROM_SIZE || (addr + len) > EEPROM_SIZE) {
    SET_ERROR_EX(err_info, EEPROM_E_RANGE, HAL_ERROR,
                 "Address range out of bounds", addr, len);
    return EEPROM_E_RANGE;
  }

  EEPROM_INFO("Erasing: addr=0x%02X, len=%u", addr, len);

  /* 准备擦除数据(全FF) */
  memset(erase_buffer, 0xFF, EEPROM_PAGESIZE);

  /* 使用Write函数写入FF */
  return EEPROM_Write(addr, erase_buffer, len > EEPROM_SIZE ? EEPROM_SIZE : len, err_info);
}

/**
 * @brief  获取错误描述字符串
 */
const char *EEPROM_GetErrorString(EEPROM_Status status) {
  switch (status) {
    case EEPROM_OK: return "Success";
    case EEPROM_E_PARAM: return "Invalid parameter";
    case EEPROM_E_RANGE: return "Address out of range";
    case EEPROM_E_ALIGN: return "Address alignment error";
    case EEPROM_E_HAL: return "HAL layer error";
    case EEPROM_E_TIMEOUT: return "Operation timeout";
    case EEPROM_E_BUSY: return "Device busy";
    case EEPROM_E_INIT: return "Initialization error";
    default: return "Unknown error";
  }
}

/**
 * @brief  获取操作统计信息
 */
void EEPROM_GetStats(EEPROM_Stats *stats) {
  if (stats != NULL) {
    memcpy(stats, &g_stats, sizeof(EEPROM_Stats));
  }
}

/**
 * @brief  重置统计信息
 */
void EEPROM_ResetStats(void) {
  memset(&g_stats, 0, sizeof(EEPROM_Stats));
}
