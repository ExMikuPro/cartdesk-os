/**
 * @file    flash.c
 * @brief   STM32H7 QSPI Flash (W25Q256JV) 驱动实现
 * @details 支持单片/双片模式，提供完整的读写擦除和内存映射功能
 */

#include "flash.h"
#include <string.h>

/* 默认超时时间 (毫秒) */
#ifndef FLASH_TIMEOUT_DEFAULT
#define FLASH_TIMEOUT_DEFAULT  5000u
#endif

/* ==================== 内部工具函数 ==================== */

/**
 * @brief  判断是否为双片Flash模式
 * @param  h: Flash驱动句柄
 * @retval true=双片模式, false=单片模式
 */
static inline bool flash_is_dual(const FLASH_Handle *h) {
#if defined(QSPI_DUALFLASH_ENABLE)
    return h && h->qspi && (h->qspi->Init.DualFlash == QSPI_DUALFLASH_ENABLE);
#else
    (void)h;
    return false;
#endif
}

/**
 * @brief  设置错误信息
 * @param  h: Flash驱动句柄
 * @param  code: 错误代码
 * @param  hal: HAL状态
 * @param  step: 错误步骤描述
 * @param  addr: 相关地址
 * @param  len: 相关长度
 * @param  line: 源代码行号
 * @retval 返回错误代码
 */
static FLASH_Status flash_set_err(FLASH_Handle *h, FLASH_Status code,
                                  HAL_StatusTypeDef hal, const char *step,
                                  uint32_t addr, uint32_t len, uint32_t line) {
    if (h) {
        h->last.code = code;
        h->last.hal  = hal;
        h->last.qspi_error = (h->qspi ? HAL_QSPI_GetError(h->qspi) : 0);
        h->last.step = step;
        h->last.line = line;
        h->last.addr = addr;
        h->last.len  = len;
    }
    return code;
}

/* 错误返回宏 - 自动记录行号 */
#define FLASH_FAIL(h, code, hal, step, addr, len) \
    return flash_set_err((h),(code),(hal),(step),(addr),(len),__LINE__)

/**
 * @brief  如果处于Memory-Mapped模式，则中止
 * @param  h: Flash驱动句柄
 * @retval FLASH_Status
 * @note   写入/擦除操作前必须退出映射模式
 */
static FLASH_Status flash_abort_if_mm(FLASH_Handle *h) {
    if (!h || !h->qspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "abort:param", 0, 0);
    if (!h->memory_mapped) return FLASH_OK;
    
    if (HAL_QSPI_Abort(h->qspi) != HAL_OK) {
        FLASH_FAIL(h, FLASH_E_HAL, HAL_ERROR, "HAL_QSPI_Abort", 0, 0);
    }
    h->memory_mapped = false;
    return FLASH_OK;
}

/**
 * @brief  发送QSPI命令
 * @param  h: Flash驱动句柄
 * @param  cmd: 命令结构体
 * @param  timeout: 超时时间
 * @param  step: 步骤描述
 * @param  addr: 相关地址
 * @param  len: 相关长度
 * @retval FLASH_Status
 */
static FLASH_Status flash_cmd(FLASH_Handle *h, QSPI_CommandTypeDef *cmd, uint32_t timeout,
                              const char *step, uint32_t addr, uint32_t len) {
    if (!h || !h->qspi || !cmd) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "cmd:param", addr, len);
    
    /* 如果处于映射模式，先退出 */
    FLASH_Status st = flash_abort_if_mm(h);
    if (st != FLASH_OK) return st;

    if (HAL_QSPI_Command(h->qspi, cmd, timeout) != HAL_OK) {
        FLASH_FAIL(h, FLASH_E_HAL, HAL_ERROR, step, addr, len);
    }
    return FLASH_OK;
}

/**
 * @brief  QSPI发送数据
 */
static FLASH_Status flash_tx(FLASH_Handle *h, const void *buf, uint32_t timeout,
                             const char *step, uint32_t addr, uint32_t len) {
    if (!h || !h->qspi || !buf) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "tx:param", addr, len);
    if (HAL_QSPI_Transmit(h->qspi, (uint8_t*)buf, timeout) != HAL_OK) {
        FLASH_FAIL(h, FLASH_E_HAL, HAL_ERROR, step, addr, len);
    }
    return FLASH_OK;
}

/**
 * @brief  QSPI接收数据
 */
static FLASH_Status flash_rx(FLASH_Handle *h, void *buf, uint32_t timeout,
                             const char *step, uint32_t addr, uint32_t len) {
    if (!h || !h->qspi || !buf) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "rx:param", addr, len);
    if (HAL_QSPI_Receive(h->qspi, (uint8_t*)buf, timeout) != HAL_OK) {
        FLASH_FAIL(h, FLASH_E_HAL, HAL_ERROR, step, addr, len);
    }
    return FLASH_OK;
}

/**
 * @brief  构造寄存器命令
 * @param  c: 命令结构体
 * @param  inst: 指令码
 * @param  n: 数据字节数 (0表示无数据)
 */
static void flash_make_reg_cmd(QSPI_CommandTypeDef *c, uint8_t inst, uint32_t n) {
    memset(c, 0, sizeof(*c));
    c->InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    c->Instruction       = inst;
    c->AddressMode       = QSPI_ADDRESS_NONE;
    c->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    c->DataMode          = (n ? QSPI_DATA_1_LINE : QSPI_DATA_NONE);
    c->DummyCycles       = 0;
    c->NbData            = n;
    c->DdrMode           = QSPI_DDR_MODE_DISABLE;
    c->DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    c->SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
}

/**
 * @brief  自动轮询状态寄存器1
 * @param  h: Flash驱动句柄
 * @param  match: 匹配值
 * @param  mask: 掩码
 * @param  nbytes: 状态字节数 (单片1, 双片2)
 * @param  timeout: 超时时间
 * @param  step: 步骤描述
 * @retval FLASH_Status
 */
static FLASH_Status flash_autopoll_sr1(FLASH_Handle *h, uint32_t match, uint32_t mask,
                                      uint32_t nbytes, uint32_t timeout, const char *step) {
    QSPI_CommandTypeDef cmd;
    QSPI_AutoPollingTypeDef cfg;

    flash_make_reg_cmd(&cmd, READ_STATUS_REG1_CMD, nbytes);

    memset(&cfg, 0, sizeof(cfg));
    cfg.Match           = match;
    cfg.Mask            = mask;
    cfg.MatchMode       = QSPI_MATCH_MODE_AND;
    cfg.StatusBytesSize = nbytes;
    cfg.Interval        = 0x10;
    cfg.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;

    FLASH_Status st = flash_abort_if_mm(h);
    if (st != FLASH_OK) return st;

    if (HAL_QSPI_AutoPolling(h->qspi, &cmd, &cfg, timeout) != HAL_OK) {
        FLASH_FAIL(h, FLASH_E_HAL, HAL_ERROR, step, 0, 0);
    }
    return FLASH_OK;
}

/**
 * @brief  发送写使能命令并等待WREN标志
 * @param  h: Flash驱动句柄
 * @retval FLASH_Status
 */
static FLASH_Status flash_write_enable(FLASH_Handle *h) {
    QSPI_CommandTypeDef cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = WRITE_ENABLE_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    FLASH_Status st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "WRITE_ENABLE", 0, 0);
    if (st != FLASH_OK) return st;

    /* 双片模式时需要检查两个芯片的WREN位 */
    const bool dual = flash_is_dual(h);
    const uint32_t n = dual ? 2u : 1u;
    const uint32_t wren = (uint32_t)W25Q_FSR_WREN;
    const uint32_t match = dual ? (wren | (wren << 8)) : wren;
    const uint32_t mask  = dual ? (wren | (wren << 8)) : wren;

    return flash_autopoll_sr1(h, match, mask, n, FLASH_TIMEOUT_DEFAULT, "AutoPoll:WREN");
}

/**
 * @brief  等待Flash就绪 (BUSY=0)
 * @param  h: Flash驱动句柄
 * @param  timeout: 超时时间
 * @retval FLASH_Status
 */
static FLASH_Status flash_wait_ready(FLASH_Handle *h, uint32_t timeout) {
    const bool dual = flash_is_dual(h);
    const uint32_t n = dual ? 2u : 1u;

    const uint32_t busy = (uint32_t)W25Q_FSR_BUSY;
    const uint32_t match = 0u;  /* 等待BUSY位清零 */
    const uint32_t mask  = dual ? (busy | (busy << 8)) : busy;

    return flash_autopoll_sr1(h, match, mask, n, timeout, "AutoPoll:BUSY");
}

/**
 * @brief  读取状态寄存器2
 * @param  h: Flash驱动句柄
 * @param  out_sr2_a: 输出SR2值 (单片或双片A)
 * @param  out_sr2_b: 输出SR2值 (双片B)
 * @retval FLASH_Status
 */
static FLASH_Status flash_read_sr2(FLASH_Handle *h, uint8_t *out_sr2_a, uint8_t *out_sr2_b) {
    QSPI_CommandTypeDef cmd;
    const bool dual = flash_is_dual(h);
    uint8_t tmp[2] = {0};

    flash_make_reg_cmd(&cmd, READ_STATUS_REG2_CMD, dual ? 2u : 1u);

    FLASH_Status st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "READ_SR2:cmd", 0, 0);
    if (st != FLASH_OK) return st;

    st = flash_rx(h, tmp, FLASH_TIMEOUT_DEFAULT, "READ_SR2:rx", 0, dual ? 2u : 1u);
    if (st != FLASH_OK) return st;

    *out_sr2_a = tmp[0];
    *out_sr2_b = dual ? tmp[1] : tmp[0];
    return FLASH_OK;
}

/**
 * @brief  写入状态寄存器2
 * @param  h: Flash驱动句柄
 * @param  sr2_val: 要写入的SR2值
 * @retval FLASH_Status
 * @note   用于设置QE位等
 */
static FLASH_Status flash_write_sr2(FLASH_Handle *h, uint8_t sr2_val) {
    QSPI_CommandTypeDef cmd;
    const bool dual = flash_is_dual(h);
    uint8_t tx[2];

    tx[0] = sr2_val;
    tx[1] = sr2_val;

    flash_make_reg_cmd(&cmd, WRITE_STATUS_REG2_CMD, dual ? 2u : 1u);

    FLASH_Status st = flash_write_enable(h);
    if (st != FLASH_OK) return st;

    st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "WRITE_SR2:cmd", 0, 0);
    if (st != FLASH_OK) return st;

    st = flash_tx(h, tx, FLASH_TIMEOUT_DEFAULT, "WRITE_SR2:tx", 0, dual ? 2u : 1u);
    if (st != FLASH_OK) return st;

    return flash_wait_ready(h, FLASH_TIMEOUT_DEFAULT);
}

/**
 * @brief  进入4字节地址模式
 * @param  h: Flash驱动句柄
 * @retval FLASH_Status
 * @note   W25Q256需要此命令才能访问>16MB地址
 */
static FLASH_Status flash_enter_4byte(FLASH_Handle *h) {
    QSPI_CommandTypeDef cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = ENTER_4_BYTE_ADDR_MODE_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    FLASH_Status st = flash_write_enable(h);
    if (st != FLASH_OK) return st;

    st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "ENTER_4BYTE", 0, 0);
    if (st != FLASH_OK) return st;

    return flash_wait_ready(h, FLASH_TIMEOUT_DEFAULT);
}

/**
 * @brief  地址范围检查
 * @param  h: Flash驱动句柄
 * @param  addr: 起始地址
 * @param  len: 长度
 * @retval FLASH_Status
 */
static FLASH_Status flash_range_check(FLASH_Handle *h, uint32_t addr, uint32_t len) {
    if (!h) return FLASH_E_PARAM;
    if (addr >= h->total_size_bytes) {
        FLASH_FAIL(h, FLASH_E_RANGE, HAL_ERROR, "addr out of range", addr, len);
    }
    if (len > 0 && (addr + len > h->total_size_bytes)) {
        FLASH_FAIL(h, FLASH_E_RANGE, HAL_ERROR, "addr+len out of range", addr, len);
    }
    return FLASH_OK;
}

/* ==================== 公共API实现 ==================== */

/**
 * @brief  绑定QSPI句柄并初始化驱动
 */
FLASH_Status FLASH_Open(FLASH_Handle *h, QSPI_HandleTypeDef *hqspi,
                        uint32_t total_size_bytes) {
    if (!h || !hqspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "FLASH_Open:param", 0, 0);

    memset(h, 0, sizeof(*h));

    h->qspi = hqspi;
    h->total_size_bytes = total_size_bytes;
    h->erase4k_bytes    = 4096u;
    h->erase64k_bytes   = 65536u;
    h->page_bytes       = 256u;
    h->dummy_cycles_fast_read = 6u;
    h->memory_mapped    = false;
    h->mm_base          = FLASH_MM_BASE;

    return FLASH_OK;
}

/**
 * @brief  Flash芯片上电初始化
 */
FLASH_Status FLASH_BringUp(FLASH_Handle *h) {
    if (!h || !h->qspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "BringUp:param", 0, 0);

    /* 1. 复位芯片 */
    FLASH_Status st = FLASH_Reset(h);
    if (st != FLASH_OK) return st;

    /* 2. 读取并使能QE位 (四线模式) */
    uint8_t sr2_a, sr2_b;
    st = flash_read_sr2(h, &sr2_a, &sr2_b);
    if (st != FLASH_OK) return st;

    const bool dual = flash_is_dual(h);
    bool need_write_qe = false;

    if (!(sr2_a & W25Q_SR2_QE)) need_write_qe = true;
    if (dual && !(sr2_b & W25Q_SR2_QE)) need_write_qe = true;

    if (need_write_qe) {
        st = flash_write_sr2(h, sr2_a | W25Q_SR2_QE);
        if (st != FLASH_OK) return st;
    }

    /* 3. 进入4字节地址模式 */
    return flash_enter_4byte(h);
}

/**
 * @brief  复位Flash芯片
 */
FLASH_Status FLASH_Reset(FLASH_Handle *h) {
    if (!h || !h->qspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "Reset:param", 0, 0);

    QSPI_CommandTypeDef cmd;

    /* 发送使能复位命令 0x66 */
    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = RESET_ENABLE_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    FLASH_Status st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "RESET_ENABLE", 0, 0);
    if (st != FLASH_OK) return st;

    /* 发送复位命令 0x99 */
    cmd.Instruction = RESET_MEMORY_CMD;
    st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "RESET_MEMORY", 0, 0);
    if (st != FLASH_OK) return st;

    /* 等待复位完成 */
    HAL_Delay(1);
    return FLASH_OK;
}

/**
 * @brief  读取JEDEC ID
 */
FLASH_Status FLASH_ReadJEDEC(FLASH_Handle *h, uint32_t *jedec) {
    if (!h || !h->qspi || !jedec) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "ReadJEDEC:param", 0, 0);

    QSPI_CommandTypeDef cmd;
    uint8_t tmp[3] = {0};

    flash_make_reg_cmd(&cmd, READ_JEDEC_ID_CMD, 3u);

    FLASH_Status st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "ReadJEDEC:cmd", 0, 0);
    if (st != FLASH_OK) return st;

    st = flash_rx(h, tmp, FLASH_TIMEOUT_DEFAULT, "ReadJEDEC:rx", 0, 3u);
    if (st != FLASH_OK) return st;

    *jedec = ((uint32_t)tmp[0] << 16) | ((uint32_t)tmp[1] << 8) | tmp[2];
    return FLASH_OK;
}

/**
 * @brief  读取设备ID
 */
FLASH_Status FLASH_ReadDeviceID(FLASH_Handle *h, uint16_t *dev_id) {
    if (!h || !h->qspi || !dev_id) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "ReadDeviceID:param", 0, 0);

    QSPI_CommandTypeDef cmd;
    uint8_t tmp[2] = {0};

    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = READ_ID_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_24_BITS;
    cmd.Address           = 0x000000;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = 2u;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    FLASH_Status st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "ReadDeviceID:cmd", 0, 0);
    if (st != FLASH_OK) return st;

    st = flash_rx(h, tmp, FLASH_TIMEOUT_DEFAULT, "ReadDeviceID:rx", 0, 2u);
    if (st != FLASH_OK) return st;

    *dev_id = ((uint16_t)tmp[0] << 8) | tmp[1];
    return FLASH_OK;
}

/**
 * @brief  普通读取
 */
FLASH_Status FLASH_Read(FLASH_Handle *h, uint32_t addr, void *buf, uint32_t len) {
    if (!h || !h->qspi || !buf) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "FLASH_Read:param", addr, len);
    FLASH_Status st = flash_range_check(h, addr, len);
    if (st != FLASH_OK) return st;
    if (len == 0) return FLASH_OK;

    QSPI_CommandTypeDef cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = READ_CMD_4BYTE;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.Address           = addr;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_1_LINE;
    cmd.DummyCycles       = 0;
    cmd.NbData            = len;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "READ:cmd", addr, len);
    if (st != FLASH_OK) return st;

    return flash_rx(h, buf, FLASH_TIMEOUT_DEFAULT, "READ:rx", addr, len);
}

/**
 * @brief  四线快速读取
 */
FLASH_Status FLASH_ReadFastQuad(FLASH_Handle *h, uint32_t addr, void *buf, uint32_t len) {
    if (!h || !h->qspi || !buf) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "FLASH_FastRead:param", addr, len);
    FLASH_Status st = flash_range_check(h, addr, len);
    if (st != FLASH_OK) return st;
    if (len == 0) return FLASH_OK;

    QSPI_CommandTypeDef cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = QUAD_INOUT_FAST_READ_CMD_4BYTE;
    cmd.AddressMode       = QSPI_ADDRESS_4_LINES;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.Address           = addr;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_4_LINES;
    cmd.DummyCycles       = h->dummy_cycles_fast_read;
    cmd.NbData            = len;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "FASTREAD:cmd", addr, len);
    if (st != FLASH_OK) return st;

    return flash_rx(h, buf, FLASH_TIMEOUT_DEFAULT, "FASTREAD:rx", addr, len);
}

/**
 * @brief  4KB扇区擦除
 */
FLASH_Status FLASH_Erase4K(FLASH_Handle *h, uint32_t addr) {
    if (!h || !h->qspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "Erase4K:param", addr, 0);
    if (addr % h->erase4k_bytes) FLASH_FAIL(h, FLASH_E_ALIGN, HAL_ERROR, "Erase4K:align", addr, h->erase4k_bytes);
    FLASH_Status st = flash_range_check(h, addr, h->erase4k_bytes);
    if (st != FLASH_OK) return st;

    QSPI_CommandTypeDef cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = SECTOR_ERASE_CMD_4BYTE;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.Address           = addr;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    st = flash_write_enable(h);
    if (st != FLASH_OK) return st;

    st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "Erase4K:cmd", addr, 0);
    if (st != FLASH_OK) return st;

    return flash_wait_ready(h, 2000u); /* 4K擦除约45-800ms */
}

/**
 * @brief  64KB块擦除
 */
FLASH_Status FLASH_Erase64K(FLASH_Handle *h, uint32_t addr) {
    if (!h || !h->qspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "Erase64K:param", addr, 0);
    if (addr % h->erase64k_bytes) FLASH_FAIL(h, FLASH_E_ALIGN, HAL_ERROR, "Erase64K:align", addr, h->erase64k_bytes);
    FLASH_Status st = flash_range_check(h, addr, h->erase64k_bytes);
    if (st != FLASH_OK) return st;

    QSPI_CommandTypeDef cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = BLOCK64K_ERASE_CMD_4BYTE;
    cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.Address           = addr;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    st = flash_write_enable(h);
    if (st != FLASH_OK) return st;

    st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "Erase64K:cmd", addr, 0);
    if (st != FLASH_OK) return st;

    return flash_wait_ready(h, 10000u); /* 64K擦除约150-2000ms */
}

/**
 * @brief  整片擦除
 */
FLASH_Status FLASH_EraseChip(FLASH_Handle *h) {
    if (!h || !h->qspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "EraseChip:param", 0, 0);

    QSPI_CommandTypeDef cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = CHIP_ERASE_CMD;
    cmd.AddressMode       = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode          = QSPI_DATA_NONE;
    cmd.DummyCycles       = 0;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    FLASH_Status st = flash_write_enable(h);
    if (st != FLASH_OK) return st;

    st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "EraseChip:cmd", 0, 0);
    if (st != FLASH_OK) return st;

    return flash_wait_ready(h, 300000u); /* 整片擦除约40-200s */
}

/**
 * @brief  页编程 (自动处理跨页)
 */
FLASH_Status FLASH_Prog(FLASH_Handle *h, uint32_t addr, const void *buf, uint32_t len) {
    if (!h || !h->qspi || !buf) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "Prog:param", addr, len);
    FLASH_Status st = flash_range_check(h, addr, len);
    if (st != FLASH_OK) return st;
    if (len == 0) return FLASH_OK;

    const uint8_t *p = (const uint8_t*)buf;

    /* 按页边界自动分割 */
    while (len) {
        uint32_t page_off = addr % h->page_bytes;
        uint32_t chunk = h->page_bytes - page_off;
        if (chunk > len) chunk = len;

        QSPI_CommandTypeDef cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
        cmd.Instruction       = QUAD_INPUT_PAGE_PROG_CMD_4BYTE;
        cmd.AddressMode       = QSPI_ADDRESS_1_LINE;
        cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
        cmd.Address           = addr;
        cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
        cmd.DataMode          = QSPI_DATA_4_LINES;
        cmd.DummyCycles       = 0;
        cmd.NbData            = chunk;
        cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
        cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
        cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

        st = flash_write_enable(h);
        if (st != FLASH_OK) return st;

        st = flash_cmd(h, &cmd, FLASH_TIMEOUT_DEFAULT, "PROG:cmd", addr, chunk);
        if (st != FLASH_OK) return st;

        st = flash_tx(h, p, FLASH_TIMEOUT_DEFAULT, "PROG:tx", addr, chunk);
        if (st != FLASH_OK) return st;

        st = flash_wait_ready(h, FLASH_TIMEOUT_DEFAULT);
        if (st != FLASH_OK) return st;

        addr += chunk;
        p    += chunk;
        len  -= chunk;
    }
    return FLASH_OK;
}

/**
 * @brief  使能Memory-Mapped模式
 */
FLASH_Status FLASH_EnableMemoryMapped(FLASH_Handle *h) {
    if (!h || !h->qspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "MM:enable:param", 0, 0);

    /* 先确保不在mm状态 */
    (void)FLASH_DisableMemoryMapped(h);

    QSPI_CommandTypeDef cmd;
    QSPI_MemoryMappedTypeDef mm;

    memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction       = QUAD_INOUT_FAST_READ_CMD_4BYTE;
    cmd.AddressMode       = QSPI_ADDRESS_4_LINES;
    cmd.AddressSize       = QSPI_ADDRESS_32_BITS;
    cmd.DataMode          = QSPI_DATA_4_LINES;
    cmd.DummyCycles       = h->dummy_cycles_fast_read;
    cmd.DdrMode           = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

    memset(&mm, 0, sizeof(mm));
    mm.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    mm.TimeOutPeriod     = 0;

    if (HAL_QSPI_MemoryMapped(h->qspi, &cmd, &mm) != HAL_OK) {
        FLASH_FAIL(h, FLASH_E_HAL, HAL_ERROR, "HAL_QSPI_MemoryMapped", 0, 0);
    }

    h->memory_mapped = true;
    return FLASH_OK;
}

/**
 * @brief  禁用Memory-Mapped模式
 */
FLASH_Status FLASH_DisableMemoryMapped(FLASH_Handle *h) {
    if (!h || !h->qspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "MM:disable:param", 0, 0);
    return flash_abort_if_mm(h);
}

/**
 * @brief  获取最后一次错误信息
 */
const FLASH_ErrorInfo *FLASH_LastError(FLASH_Handle *h) {
    return h ? &h->last : NULL;
}

/* ==================== 兼容层 API 实现（可选） ==================== */

FLASH_Status FLASH_Init(FLASH_Handle *h, QSPI_HandleTypeDef *hqspi,
                        uint32_t flash_size_bytes, uint32_t dummy_cycles_fast_read) {
    FLASH_Status st = FLASH_Open(h, hqspi, flash_size_bytes);
    if (st != FLASH_OK) return st;

    /* dummy_cycles_fast_read 在 W25Q256 上通常是 6~10，用户自定义 */
    if (dummy_cycles_fast_read > 255u) dummy_cycles_fast_read = 255u;
    h->dummy_cycles_fast_read = (uint8_t)dummy_cycles_fast_read;
    return FLASH_OK;
}

FLASH_Status FLASH_ReadJedecId(FLASH_Handle *h, uint8_t out_id3[3]) {
    if (!out_id3) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "ReadJedecId:param", 0, 0);

    uint32_t jedec = 0;
    FLASH_Status st = FLASH_ReadJEDEC(h, &jedec);
    if (st != FLASH_OK) return st;

    out_id3[0] = (uint8_t)((jedec >> 16) & 0xFFu);
    out_id3[1] = (uint8_t)((jedec >> 8)  & 0xFFu);
    out_id3[2] = (uint8_t)((jedec >> 0)  & 0xFFu);
    return FLASH_OK;
}

FLASH_Status FLASH_EraseRange(FLASH_Handle *h, uint32_t addr, uint32_t len) {
    if (!h || !h->qspi) FLASH_FAIL(h, FLASH_E_PARAM, HAL_ERROR, "EraseRange:param", addr, len);
    FLASH_Status st = flash_range_check(h, addr, len);
    if (st != FLASH_OK) return st;
    if (len == 0) return FLASH_OK;

    /* 向上对齐到 4K（擦除必须覆盖整个范围） */
    const uint32_t erase4k = h->erase4k_bytes;
    const uint32_t erase64k = h->erase64k_bytes;

    uint32_t end = addr + len;
    uint32_t end_aligned = (end + erase4k - 1u) / erase4k * erase4k;

    while (addr < end_aligned) {
        /* 优先使用 64K：对齐 + 剩余 >= 64K */
        if ((addr % erase64k) == 0u && (end_aligned - addr) >= erase64k) {
            st = FLASH_Erase64K(h, addr);
            if (st != FLASH_OK) return st;
            addr += erase64k;
            continue;
        }

        /* 否则使用 4K */
        if ((addr % erase4k) != 0u) {
            /* 这里理论上不会发生，因为 end_aligned 是 4K 对齐，而 addr 可能不是 4K 对齐。
             * 为了安全，向下对齐到 4K（覆盖更大范围）。 */
            addr = (addr / erase4k) * erase4k;
        }
        st = FLASH_Erase4K(h, addr);
        if (st != FLASH_OK) return st;
        addr += erase4k;
    }

    return FLASH_OK;
}

FLASH_Status FLASH_Program(FLASH_Handle *h, uint32_t addr, const void *buf, uint32_t len) {
    return FLASH_Prog(h, addr, buf, len);
}