# XHGC 卡带接口 GPIO 选型分析

> 状态：历史分析报告 / 已被更高层规划文档吸收
>
> 当前仓库中的规范化 GPIO 规划入口：
>
> - 主规划文档：`docs/hardware/gpio-plan.md`
> - 机读映射：`docs/hardware/gpio-map.csv`
> - 机读映射：`docs/hardware/gpio-map.yaml`
> - 连接器规划：`docs/hardware/pinout/xhgc-cartridge-pinout.md`
> - 设计决策记录：`docs/adr/0001-xhgc-gpio-allocation.md`
>
> 本文档保留原始分析过程、候选方案推导和风险比较，便于回溯。若与规范化文档出现差异，应以 `docs/hardware/` 与 `docs/adr/` 下的文档为准。

## 1. 结论摘要

- 新增硬约束已经纳入本报告最终结论：
  - `USB FS` 使用 `PA11 = USB_OTG_FS_DM`、`PA12 = USB_OTG_FS_DP`
  - 调试使用 `PA13 = SWDIO`、`PA14 = SWCLK`
  - `XHGC` 卡带接口不得占用 `PA11/PA12/PA13/PA14`
- 当前工程在 `STM32H743XIH6 / TFBGA240` 上已经大量占用了 `FMC + LTDC + QSPI + SDMMC + I2C + USART1`，因此：
- 无法在当前已确认的空闲 GPIO 中找到一组 `D0~D15` 落在同一个 GPIO 端口、且 `bit` 连续的 16-bit 总线。
- 也无法在当前已确认且封装可确认的空闲 GPIO 中，找到一组完全落在同一个 GPIO 端口、且 `bit` 连续的 8-bit 总线。
- 但是从“总数量”上看，当前仍有足够空闲 GPIO 可以做 16-bit 卡带接口，只是必须接受 `bit` 不连续、以及 2~4 个端口拼接。
- 在 `USB FS + 双线 SWD 必须保留` 的前提下：
  1. **主推荐方案：方案 C**
  2. **备选方案：方案 B**
  3. **方案 A：废弃候选，不兼容 USB**

## 2. 检查范围

本报告基于以下证据生成：

- CubeMX 配置：`cartdesk-os.ioc`
- 历史备份：`xih6-display.bak.ioc`
- 板级 pin list：`cartdesk_io_pinout.csv`
- GPIO 初始化：`Core/Src/gpio.c`
- GPIO 宏：`Core/Inc/main.h`
- 外设 MSP：
  - `Core/Src/fmc.c`
  - `Core/Src/ltdc.c`
  - `Core/Src/quadspi.c`
  - `Core/Src/sdmmc.c`
  - `Core/Src/i2c.c`
  - `Core/Src/usart.c`
  - `Core/Src/tim.c`
- 板级 GPIO 抽象：
  - `Core/Driver/GPIO/board_gpio.c`
  - `Core/Driver/GPIO/board_pwm.c`
- 项目说明：
  - `README.md`
  - `docs/architecture.md`

补充说明：

- 为确认若干“`.ioc` 未配置、但封装上是否存在”的引脚，我额外参考了 ST 官方 datasheet `DS12110 Rev 11`。
- `PK8~PK15` 没有在 `STM32H743XI` 的 TFBGA240 文本化 pin/ball 定义中被检索到，本报告不把它们作为可用 GPIO 处理。

## 3. 当前已确认占用

### 3.1 已被现有工程明确占用的接口

| 接口 | 引脚 |
| --- | --- |
| USB FS 保留 | `PA11`, `PA12` |
| SWD | `PA13`, `PA14` |
| HSE | `PH0`, `PH1` |
| USART1 Debug | `PA9`, `PA10` |
| I2C1 EEPROM | `PB8`, `PB9` |
| I2C2 Touch | `PH4`, `PH5` |
| GT911 Touch | `PG3`, `PG7` |
| LCD Backlight | `PD13` |
| Buzzer | `PI8` |
| QSPI | `PB2`, `PF6`, `PF7`, `PF8`, `PF9`, `PG6`, `PG9`, `PG14`, `PH2`, `PH3` |
| SDMMC1 | `PC8`, `PC9`, `PC10`, `PC11`, `PC12`, `PD2` |
| FMC / SDRAM | `PC0`, `PD0`, `PD1`, `PD8`, `PD9`, `PD10`, `PD14`, `PD15`, `PE0`, `PE1`, `PE7~PE15`, `PF0~PF5`, `PF11~PF15`, `PG0`, `PG1`, `PG2`, `PG4`, `PG5`, `PG8`, `PG15`, `PH6~PH15`, `PI0~PI7`, `PI9`, `PI10` |
| LTDC | `PI12`, `PI13`, `PI14`, `PI15`, `PJ0~PJ15`, `PK0~PK7` |
| 板级 GPIO | `PA0`, `PC2`, `PC13` |

### 3.1.1 必须保留的调试与 USB 引脚

| 用途 | 引脚 | 是否允许给 XHGC |
| --- | --- | --- |
| `USB_OTG_FS_DM` | `PA11` | 不允许 |
| `USB_OTG_FS_DP` | `PA12` | 不允许 |
| `SWDIO` | `PA13` | 不允许 |
| `SWCLK` | `PA14` | 不允许 |

补充说明：

- 本报告后续所有推荐方案都以“`PA11/PA12/PA13/PA14` 保留”为前提。
- 占用 `PA11/PA12` 的方案一律视为“不兼容 USB”。
- 占用 `PA13/PA14` 的方案一律视为“不兼容 SWD”。

### 3.2 `.ioc` 与源码的实际冲突/差异

1. `PC2`
   - `.ioc` / `MX_GPIO_Init()` 初始为输出高。
   - `HAL_SD_MspInit()` 里又显式拉低。
   - 结论：`PC2` 视为已占用，且上电行为敏感，不建议分配给 XHGC。

2. `PA3 / PA6 / PA7 / PB0 / PB1`
   - `.ioc` 中被配置为 `TIM2/TIM3 PWM`。
   - `cartdesk_io_pinout.csv` 也把它们标成用户扩展 GPIO/PWM。
   - 但当前源码里没有找到对应 `HAL_TIM_MspPostInit()` 或等价 GPIO AF 初始化。
   - 结论：这些脚在“设计意图上”属于用户扩展口，虽然当前固件未完全证实运行时 AF 已启用，但仍不建议优先占用。

## 4. 当前可用 GPIO 分组

以下分组以“`.ioc` 未分配、源码未初始化为已有外设”作为判定依据；其中个别引脚是否在封装上存在，已按 `STM32H743XI / TFBGA240` 官方资料做过必要复核。

| 端口 | 当前空闲位 |
| --- | --- |
| `GPIOA` | `PA1`, `PA2`, `PA4`, `PA5`, `PA8`, `PA11`, `PA12`, `PA15` |
| `GPIOB` | `PB3`, `PB4`, `PB5`, `PB6`, `PB7`, `PB10`, `PB11`, `PB12`, `PB13`, `PB14`, `PB15` |
| `GPIOC` | `PC1`, `PC3`, `PC4`, `PC5`, `PC6`, `PC7`, `PC14`, `PC15` |
| `GPIOD` | `PD3`, `PD4`, `PD5`, `PD6`, `PD7`, `PD11`, `PD12` |
| `GPIOE` | `PE2`, `PE3`, `PE4`, `PE5`, `PE6` |
| `GPIOF` | `PF10` |
| `GPIOG` | `PG10`, `PG11`, `PG12`, `PG13` |
| `GPIOI` | `PI11` |

### 4.1 风险脚额外标记

这些脚虽然“空闲”，但需要分级看待：

- `PB3`, `PB4`, `PB5`
  - 由于你只保留双线 SWD，它们从“高风险 JTAG 保留脚”降级为“中风险备用脚”。
  - 其中 `PB3` 如果未来要 `SWO trace`，仍建议保留；如果确定不用 `SWO`，可作为低优先级候选。
- `PA11`, `PA12`
  - `USB FS` 硬约束保留脚。
  - 不允许分配给 XHGC 数据线、控制线、扩展 GPIO 或普通板级用途。
- `PA13`, `PA14`
  - 双线 `SWD` 硬约束保留脚。
  - 不允许分配给 XHGC。
- `PA15`
  - 由于不要求完整 JTAG，可降级为“中风险备用脚”。
- `PC14`, `PC15`
  - LSE/RTC 域相关低速特性脚，不适合高速总线。
- `PC13`, `PI8`
  - 特殊 I/O 电气特性，且项目里已占用，不适合外部卡带高速接口。
- `PG13`, `PG14`
  - Trace 相关复用。

## 5. 候选方案

---

## 5.1 方案 A：最高性能方案

设计目标：

- 第一版 8-bit 读取只读 1 个 `IDR`
- 未来 16-bit 读取只读 2 个 `IDR`
- 接受稀疏 bit 布局，优先减少端口数量

兼容性结论：

- **不兼容 USB**
- 原因：占用了 `PA11/PA12`
- 因此本方案只保留作理论性能参考，不进入最终推荐

### 5.1.1 信号映射表

#### 数据线

| 信号 | MCU Pin | Port | Bit | 是否连续 | 当前占用状态 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `XHGC_D0` | `PB3` | `GPIOB` | 3 | `GPIOB cluster-0` | 空闲 | JTAG/SWO 风险 |
| `XHGC_D1` | `PB4` | `GPIOB` | 4 | `GPIOB cluster-0` | 空闲 | JTAG 风险 |
| `XHGC_D2` | `PB5` | `GPIOB` | 5 | `GPIOB cluster-0` | 空闲 | 调试复用风险 |
| `XHGC_D3` | `PB6` | `GPIOB` | 6 | `GPIOB cluster-0` | 空闲 | 原生 `I2C1_SCL/I2C4_SCL` 候选 |
| `XHGC_D4` | `PB7` | `GPIOB` | 7 | `GPIOB cluster-0` | 空闲 | 原生 `I2C1_SDA/I2C4_SDA` 候选 |
| `XHGC_D5` | `PB10` | `GPIOB` | 10 | `GPIOB cluster-1` | 空闲 | |
| `XHGC_D6` | `PB11` | `GPIOB` | 11 | `GPIOB cluster-1` | 空闲 | |
| `XHGC_D7` | `PB12` | `GPIOB` | 12 | `GPIOB cluster-1` | 空闲 | |
| `XHGC_D8` | `PA1` | `GPIOA` | 1 | `GPIOA sparse` | 空闲 | |
| `XHGC_D9` | `PA2` | `GPIOA` | 2 | `GPIOA sparse` | 空闲 | |
| `XHGC_D10` | `PA4` | `GPIOA` | 4 | `GPIOA sparse` | 空闲 | |
| `XHGC_D11` | `PA5` | `GPIOA` | 5 | `GPIOA sparse` | 空闲 | |
| `XHGC_D12` | `PA8` | `GPIOA` | 8 | `GPIOA sparse` | 空闲 | |
| `XHGC_D13` | `PA11` | `GPIOA` | 11 | `GPIOA sparse` | 空闲 | 与 USB FS 冲突 |
| `XHGC_D14` | `PA12` | `GPIOA` | 12 | `GPIOA sparse` | 空闲 | 与 USB FS 冲突 |
| `XHGC_D15` | `PA15` | `GPIOA` | 15 | `GPIOA sparse` | 空闲 | JTAG `JTDI` 风险 |

#### 控制线

| 信号 | MCU Pin | Port | Bit | 是否连续 | 当前占用状态 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `XHGC_I2C_SCL` | `PB13` | `GPIOB` | 13 | - | 空闲 | 建议 bitbang；硬件 I2C 未在本项目复核 |
| `XHGC_I2C_SDA` | `PB14` | `GPIOB` | 14 | - | 空闲 | 建议 bitbang |
| `XHGC_RD_CLK` | `PB15` | `GPIOB` | 15 | - | 空闲 | 可做 EXTI 采样触发 |
| `XHGC_READY` | `PD11` | `GPIOD` | 11 | - | 空闲 | |
| `XHGC_RESET_N` | `PD12` | `GPIOD` | 12 | - | 空闲 | |
| `XHGC_CART_DET` | `PC1` | `GPIOC` | 1 | - | 空闲 | |
| `XHGC_CS_N` | `PF10` | `GPIOF` | 10 | - | 空闲 | |
| `XHGC_IRQ_N` | `PG10` | `GPIOG` | 10 | - | 空闲 | |
| `XHGC_WR_N` | `PG11` | `GPIOG` | 11 | - | 空闲 | |
| `XHGC_MODE0` | `PG12` | `GPIOG` | 12 | - | 空闲 | |
| `XHGC_MODE1` | `PG13` | `GPIOG` | 13 | - | 空闲 | Trace 复用风险，尽量仅作静态 strap |

### 5.1.2 数据读取代价评估

- 8-bit：
  - 读取 `GPIOB->IDR` 1 次
  - 需要 `mask + shift + 拼接`
- 16-bit：
  - 读取 `GPIOB->IDR`、`GPIOA->IDR` 共 2 次
  - 需要 `mask + shift + 拼接`

推荐 inline：

```c
static inline uint8_t xhgc_read8_a(void)
{
    uint32_t b = GPIOB->IDR;
    return (uint8_t)(((b >> 3) & 0x1Fu) |
                     ((b >> 5) & 0xE0u));
}

static inline uint16_t xhgc_read16_a(void)
{
    uint32_t b = GPIOB->IDR;
    uint32_t a = GPIOA->IDR;

    uint16_t lo = (uint16_t)(((b >> 3) & 0x1Fu) |
                             ((b >> 5) & 0xE0u));

    uint16_t hi = (uint16_t)(((a >> 1) & 0x03u) |
                             ((a >> 2) & 0x0Cu) |
                             ((a >> 4) & 0x10u) |
                             ((a >> 6) & 0x60u) |
                             ((a >> 8) & 0x80u));

    return (uint16_t)(lo | (hi << 8));
}
```

### 5.1.3 风险评估

- 与现有外设冲突：无直接冲突，但占用了很多未来高价值脚。
- USB 影响：**有，且不可接受**
  - `PA11/PA12` 被数据总线占用，和 `USB FS` 硬约束冲突。
- SWD 影响：无。
- 启动影响：低。
- 调试影响：中。
  - `PB3` 若需 `SWO trace` 仍建议保留。
  - `PB4/PB5/PA15` 由于不要求完整 JTAG，可降级为中风险。
- 高速信号完整性：中。
  - 只用 2 个端口，读寄存器效率高，但 bit 不连续，PCB 走线不如整块 cluster 好看。
- 适合作为外部卡带接口：在“不要 USB”的前提下能用；在当前约束下应废弃。

---

## 5.2 方案 B：折中方案

设计目标：

- 低字节、高字节都按“连续 cluster”来分组
- 尽量避开 USB/JTAG/SWD
- 第一版 8-bit 和未来 16-bit 都是可接受成本

### 5.2.1 信号映射表

#### 数据线

| 信号 | MCU Pin | Port | Bit | 是否连续 | 当前占用状态 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `XHGC_D0` | `PD3` | `GPIOD` | 3 | `GPIOD[3:7]` | 空闲 | |
| `XHGC_D1` | `PD4` | `GPIOD` | 4 | `GPIOD[3:7]` | 空闲 | |
| `XHGC_D2` | `PD5` | `GPIOD` | 5 | `GPIOD[3:7]` | 空闲 | |
| `XHGC_D3` | `PD6` | `GPIOD` | 6 | `GPIOD[3:7]` | 空闲 | |
| `XHGC_D4` | `PD7` | `GPIOD` | 7 | `GPIOD[3:7]` | 空闲 | |
| `XHGC_D5` | `PE2` | `GPIOE` | 2 | `GPIOE[2:4]` | 空闲 | |
| `XHGC_D6` | `PE3` | `GPIOE` | 3 | `GPIOE[2:4]` | 空闲 | |
| `XHGC_D7` | `PE4` | `GPIOE` | 4 | `GPIOE[2:4]` | 空闲 | |
| `XHGC_D8` | `PC3` | `GPIOC` | 3 | `GPIOC[3:7]` | 空闲 | |
| `XHGC_D9` | `PC4` | `GPIOC` | 4 | `GPIOC[3:7]` | 空闲 | |
| `XHGC_D10` | `PC5` | `GPIOC` | 5 | `GPIOC[3:7]` | 空闲 | |
| `XHGC_D11` | `PC6` | `GPIOC` | 6 | `GPIOC[3:7]` | 空闲 | |
| `XHGC_D12` | `PC7` | `GPIOC` | 7 | `GPIOC[3:7]` | 空闲 | |
| `XHGC_D13` | `PG10` | `GPIOG` | 10 | `GPIOG[10:12]` | 空闲 | |
| `XHGC_D14` | `PG11` | `GPIOG` | 11 | `GPIOG[10:12]` | 空闲 | |
| `XHGC_D15` | `PG12` | `GPIOG` | 12 | `GPIOG[10:12]` | 空闲 | |

#### 控制线

| 信号 | MCU Pin | Port | Bit | 是否连续 | 当前占用状态 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `XHGC_I2C_SCL` | `PB6` | `GPIOB` | 6 | - | 空闲 | 官方 AF 可做 `I2C1_SCL/I2C4_SCL` |
| `XHGC_I2C_SDA` | `PB7` | `GPIOB` | 7 | - | 空闲 | 官方 AF 可做 `I2C1_SDA/I2C4_SDA` |
| `XHGC_RD_CLK` | `PB10` | `GPIOB` | 10 | - | 空闲 | |
| `XHGC_READY` | `PB11` | `GPIOB` | 11 | - | 空闲 | |
| `XHGC_RESET_N` | `PB12` | `GPIOB` | 12 | - | 空闲 | |
| `XHGC_CART_DET` | `PB13` | `GPIOB` | 13 | - | 空闲 | |
| `XHGC_CS_N` | `PB14` | `GPIOB` | 14 | - | 空闲 | |
| `XHGC_IRQ_N` | `PB15` | `GPIOB` | 15 | - | 空闲 | |
| `XHGC_WR_N` | `PA1` | `GPIOA` | 1 | - | 空闲 | |
| `XHGC_MODE0` | `PA2` | `GPIOA` | 2 | - | 空闲 | |
| `XHGC_MODE1` | `PI11` | `GPIOI` | 11 | - | 空闲 | |

### 5.2.2 数据读取代价评估

- 8-bit：
  - 读取 `GPIOD->IDR`、`GPIOE->IDR` 共 2 次
  - 低字节分两段，但两段都连续
- 16-bit：
  - 读取 `GPIOD->IDR`、`GPIOE->IDR`、`GPIOC->IDR`、`GPIOG->IDR` 共 4 次
  - 需要 `mask + shift + 拼接`

推荐 inline：

```c
static inline uint8_t xhgc_read8_b(void)
{
    uint32_t d = GPIOD->IDR;
    uint32_t e = GPIOE->IDR;

    return (uint8_t)(((d >> 3) & 0x1Fu) |
                     (((e >> 2) & 0x07u) << 5));
}

static inline uint16_t xhgc_read16_b(void)
{
    uint32_t d = GPIOD->IDR;
    uint32_t e = GPIOE->IDR;
    uint32_t c = GPIOC->IDR;
    uint32_t g = GPIOG->IDR;

    uint16_t lo = (uint16_t)(((d >> 3) & 0x1Fu) |
                             (((e >> 2) & 0x07u) << 5));

    uint16_t hi = (uint16_t)(((c >> 3) & 0x1Fu) |
                             (((g >> 10) & 0x07u) << 5));

    return (uint16_t)(lo | (hi << 8));
}
```

### 5.2.3 风险评估

- 与现有外设冲突：无直接冲突。
- USB 影响：无。
- SWD 影响：无。
- 启动影响：低。
- 调试影响：低。
- 高速信号完整性：中。
  - cluster 很整齐，PCB 走线和分组命名最自然。
  - 但未来 16-bit 需要 4 个端口读取，CPU 读取成本高于方案 A/C。
- 适合作为外部卡带接口：适合“先稳妥布板，再决定固件是否追极限性能”。

---

## 5.3 方案 C：保守方案

设计目标：

- 尽量保留 `GPIOA/GPIOB` 的高价值资源给 USB、调试、未来扩展
- 不碰现有用户扩展 GPIO/PWM
- 16-bit 读取尽量控制在 3 个 `IDR`

### 5.3.1 信号映射表

#### 数据线

| 信号 | MCU Pin | Port | Bit | 是否连续 | 当前占用状态 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `XHGC_D0` | `PE2` | `GPIOE` | 2 | `GPIOE[2:6]` | 空闲 | |
| `XHGC_D1` | `PE3` | `GPIOE` | 3 | `GPIOE[2:6]` | 空闲 | |
| `XHGC_D2` | `PE4` | `GPIOE` | 4 | `GPIOE[2:6]` | 空闲 | |
| `XHGC_D3` | `PE5` | `GPIOE` | 5 | `GPIOE[2:6]` | 空闲 | |
| `XHGC_D4` | `PE6` | `GPIOE` | 6 | `GPIOE[2:6]` | 空闲 | |
| `XHGC_D5` | `PD3` | `GPIOD` | 3 | `GPIOD cluster-0` | 空闲 | |
| `XHGC_D6` | `PD4` | `GPIOD` | 4 | `GPIOD cluster-0` | 空闲 | |
| `XHGC_D7` | `PD5` | `GPIOD` | 5 | `GPIOD cluster-0` | 空闲 | |
| `XHGC_D8` | `PD6` | `GPIOD` | 6 | `GPIOD cluster-0` | 空闲 | |
| `XHGC_D9` | `PD7` | `GPIOD` | 7 | `GPIOD cluster-0` | 空闲 | |
| `XHGC_D10` | `PD11` | `GPIOD` | 11 | `GPIOD cluster-1` | 空闲 | |
| `XHGC_D11` | `PD12` | `GPIOD` | 12 | `GPIOD cluster-1` | 空闲 | |
| `XHGC_D12` | `PC1` | `GPIOC` | 1 | `GPIOC sparse` | 空闲 | |
| `XHGC_D13` | `PC3` | `GPIOC` | 3 | `GPIOC sparse` | 空闲 | |
| `XHGC_D14` | `PC4` | `GPIOC` | 4 | `GPIOC sparse` | 空闲 | |
| `XHGC_D15` | `PC5` | `GPIOC` | 5 | `GPIOC sparse` | 空闲 | |

#### 控制线

| 信号 | MCU Pin | Port | Bit | 是否连续 | 当前占用状态 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `XHGC_I2C_SCL` | `PB6` | `GPIOB` | 6 | - | 空闲 | 官方 AF 可做 `I2C1_SCL/I2C4_SCL` |
| `XHGC_I2C_SDA` | `PB7` | `GPIOB` | 7 | - | 空闲 | 官方 AF 可做 `I2C1_SDA/I2C4_SDA` |
| `XHGC_RD_CLK` | `PG10` | `GPIOG` | 10 | - | 空闲 | |
| `XHGC_READY` | `PG11` | `GPIOG` | 11 | - | 空闲 | |
| `XHGC_RESET_N` | `PG12` | `GPIOG` | 12 | - | 空闲 | |
| `XHGC_CART_DET` | `PG13` | `GPIOG` | 13 | - | 空闲 | Trace 复用风险，适合静态控制 |
| `XHGC_CS_N` | `PB10` | `GPIOB` | 10 | - | 空闲 | |
| `XHGC_IRQ_N` | `PB11` | `GPIOB` | 11 | - | 空闲 | |
| `XHGC_WR_N` | `PB12` | `GPIOB` | 12 | - | 空闲 | |
| `XHGC_MODE0` | `PA1` | `GPIOA` | 1 | - | 空闲 | |
| `XHGC_MODE1` | `PA2` | `GPIOA` | 2 | - | 空闲 | |

### 5.3.2 数据读取代价评估

- 8-bit：
  - 读取 `GPIOE->IDR`、`GPIOD->IDR` 共 2 次
  - 低字节按 `E[2:6] + D[3:5]` 拼接
- 16-bit：
  - 读取 `GPIOE->IDR`、`GPIOD->IDR`、`GPIOC->IDR` 共 3 次
  - 这是三套方案里“在不吃高价值脚”的前提下最省 `IDR` 的实现

推荐 inline：

```c
static inline uint8_t xhgc_read8_c(void)
{
    uint32_t e = GPIOE->IDR;
    uint32_t d = GPIOD->IDR;

    return (uint8_t)(((e >> 2) & 0x1Fu) |
                     (((d >> 3) & 0x07u) << 5));
}

static inline uint16_t xhgc_read16_c(void)
{
    uint32_t e = GPIOE->IDR;
    uint32_t d = GPIOD->IDR;
    uint32_t c = GPIOC->IDR;

    uint16_t lo = (uint16_t)(((e >> 2) & 0x1Fu) |
                             (((d >> 3) & 0x07u) << 5));

    uint16_t hi = (uint16_t)(((d >> 6) & 0x03u) |
                             ((d >> 9) & 0x0Cu) |
                             (((c >> 1) & 0x01u) << 4) |
                             (((c >> 3) & 0x07u) << 5));

    return (uint16_t)(lo | (hi << 8));
}
```

### 5.3.3 风险评估

- 与现有外设冲突：无直接冲突。
- USB 影响：无。
- SWD 影响：无。
- 启动影响：低。
- 调试影响：低。
- 高速信号完整性：中。
  - 低字节和高字节都不是单端口整块，但端口数控制在 3 个，固件读取成本还能接受。
- 适合作为外部卡带接口：最像“平台级保守方案”，便于后续继续保留 USB、调试和用户扩展的空间。

## 6. 三套方案对比

| 方案 | 数据端口数 | 8-bit 读 IDR 次数 | 16-bit 读 IDR 次数 | 主要优点 | 主要问题 |
| --- | --- | --- | --- | --- | --- |
| A | 2 | 1 | 2 | 纯读取性能最好 | 占用 `PA11/PA12`，不兼容 USB，废弃 |
| B | 4 | 2 | 4 | cluster 最整齐，PCB 最顺手 | 16-bit 软件读取成本最高 |
| C | 3 | 2 | 3 | 保留系统扩展能力，性能也不差 | bit 排布不如 B 直观 |

## 7. 推荐排序

本节排序以“`USB FS + 双线 SWD` 必须可用”为硬前提。

### 7.1 主方案：方案 C

推荐原因：

- 不占 `PA11/PA12/PA13/PA14`。
- 不占 `PB3~PB5/PA15`，继续保留更多中风险备用脚。
- 不去抢现有用户扩展 GPIO/PWM。
- 16-bit 只需要 3 个 `IDR`，比方案 B 更省。
- 控制线还能保留 `PB6/PB7` 作为硬件 I2C 选择。

### 7.2 备选方案：方案 B

推荐原因：

- 不占 `PA11/PA12/PA13/PA14`。
- 低字节、高字节都是明显的连续 cluster，最适合布线和原理图审查。
- 非常适合“先做 PCB，后面再优化固件”。

不作为第一推荐的原因：

- 16-bit 读取需要 4 个端口，软件代价偏高。

### 7.3 废弃方案：方案 A

原因：

- 它直接占用 `PA11/PA12`，与 USB FS 硬约束冲突。
- 即使你只保留双线 SWD，这个问题也不会消失。
- 因此本方案不再是“低推荐”，而是“当前约束下不可用”。

## 8. 当前空闲 GPIO 是否满足需求

### 8.1 满足的部分

- GPIO 数量足够做完整 `D0~D15 + 11 根控制线`。
- 8-bit 第一版和 16-bit 未来版都能在当前芯片与现有工程约束下落地。

### 8.2 不满足的部分

- 无法满足“`D0~D15` 全部位于同一 GPIO 端口连续 bit”的理想目标。
- 无法满足“`D0~D7` 全部位于同一 GPIO 端口连续 bit”的理想目标。

### 8.3 缺口性质

缺的不是“GPIO 数量”，而是“高质量连续数据端口资源”。

## 9. 如果还想进一步降风险，可考虑的替代方案

1. 继续维持 8-bit，仅在 PCB 预留 `D8~D15`，第一版固件不启用高字节。
2. `MODE0 / MODE1` 改为通过 XHGC I2C 寄存器读取，减少固定 strap 线。
3. `IRQ_N / WR_N` 第一版先不接，只保留焊盘和测试点。
4. 如果未来一定要实现“1 次读取拿到完整 16-bit little-endian 数据”，考虑：
   - 外部锁存器
   - CPLD/小型 FPGA
   - 让外部逻辑把稀疏 GPIO 重排成 MCU 易读格式
5. 如果未来接口速率继续上升，优先考虑外部并口桥接，而不是继续消耗 MCU 原生 GPIO。

## 10. 建议的下一步

### 10.1 我建议你选哪套

- **主方案：选方案 C**
- **备选方案：选方案 B**
- **不要选方案 A**，因为它和 `USB FS (PA11/PA12)` 硬冲突。

### 10.1.1 明确保留脚

必须保留给 USB 和 SWD 的引脚：

- `PA11` -> `USB_OTG_FS_DM`
- `PA12` -> `USB_OTG_FS_DP`
- `PA13` -> `SWDIO`
- `PA14` -> `SWCLK`

这些脚都不得分配给 XHGC。

### 10.1.2 哪些 JTAG / SWO 相关脚可释放

可从“必须保留”降级为“中风险备用脚”的引脚：

- `PB4`
- `PB5`
- `PA15`

仍建议谨慎使用的引脚：

- `PB3`
  - 如果未来要 `SWO trace`，建议继续保留。
  - 如果明确不会使用 `SWO`，可以作为低优先级候选。

### 10.2 CubeMX 里建议如何命名

建议在 CubeMX 里直接命名为：

- `XHGC_D0` ~ `XHGC_D15`
- `XHGC_RD_CLK`
- `XHGC_READY`
- `XHGC_RESET_N`
- `XHGC_CART_DET`
- `XHGC_CS_N`
- `XHGC_IRQ_N`
- `XHGC_WR_N`
- `XHGC_MODE0`
- `XHGC_MODE1`
- `XHGC_I2C_SCL`
- `XHGC_I2C_SDA`

建议再补一组固件内部聚合命名：

- `XHGC_DATA_LO_*`
- `XHGC_DATA_HI_*`
- `XHGC_CTRL_*`

这样后面写 inline 读取宏时会更清楚。

### 10.3 固件里建议生成哪些宏或 inline

建议至少生成：

```c
#define XHGC_DATA_WIDTH_BOOT     8u
#define XHGC_DATA_WIDTH_FULL     16u

static inline uint8_t  xhgc_read8(void);
static inline uint16_t xhgc_read16(void);

static inline int xhgc_ready(void);
static inline int xhgc_cart_present(void);
static inline void xhgc_reset_assert(void);
static inline void xhgc_reset_release(void);
```

如果你采用方案 B/C，再额外生成：

```c
#define XHGC_LO_PORT_COUNT  2
#define XHGC_HI_PORT_COUNT  1 /* 或 2，取决于最终方案 */
```

以及固定 little-endian 语义注释：

```c
/* D0..D7 = low byte, D8..D15 = high byte, read result is little-endian */
```

## 11. 参考文件

- `cartdesk-os.ioc`
- `cartdesk_io_pinout.csv`
- `Core/Src/gpio.c`
- `Core/Src/i2c.c`
- `Core/Src/usart.c`
- `Core/Src/sdmmc.c`
- `Core/Src/quadspi.c`
- `Core/Src/fmc.c`
- `Core/Src/ltdc.c`
- `Core/Inc/main.h`
- `Core/Driver/GPIO/board_gpio.c`
- `README.md`
- `docs/architecture.md`
