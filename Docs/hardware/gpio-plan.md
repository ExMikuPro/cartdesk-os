# XHGC GPIO and Pin Planning

Status: Experimental / Optional

- Required for boot: `No`
- Required for normal OS operation: `No`
- Fallback storage / content path: `SD / QSPI / eMMC`

- Target MCU: `STM32H743XIH6 / TFBGA240`
- Board: `TBD`
- Last updated: `2026-06-21`

## Scope and Source Coverage

This document defines the experimental GPIO planning policy for the XHGC cartridge interface and related board-level reservations.

The planning in this document is derived from the current repository state, primarily:

- `cartdesk-os.ioc`
- `xih6-display.bak.ioc`
- `cartdesk_io_pinout.csv`
- `Core/Inc/main.h`
- `Core/Src/gpio.c`
- `Core/Src/i2c.c`
- `Core/Src/usart.c`
- `Core/Src/sdmmc.c`
- `Core/Src/quadspi.c`
- `Core/Src/fmc.c`
- `Core/Src/ltdc.c`
- `Core/Driver/GPIO/board_gpio.c`
- `Core/Driver/GPIO/board_pwm.c`
- `README.md`
- `docs/architecture.md`
- `docs/xhgc-cart-gpio-selection.md`

Additional repository searches were performed for `pin`, `gpio`, `board`, `schematic`, `netlist`, `xhgc`, and `cart`.

Artifacts not found in the current repository snapshot:

- Schematic export: not found
- KiCad netlist: not found
- Board-level pin planning spreadsheet beyond `cartdesk_io_pinout.csv`: not found

All pin assignment decisions below MUST be traceable to repository files or to the current XHGC planning report. Proposed external connector planning is explicitly marked as proposed.

## Design Goals

- The design MUST preserve `USB FS` on `PA11/PA12`.
- The design MUST preserve 2-wire `SWD` on `PA13/PA14`.
- The design MUST NOT sacrifice `LTDC`, `FMC / SDRAM`, `QSPI`, `SDMMC`, touch, audio-like board functions, debug UART, or other confirmed core functions for XHGC.
- XHGC MUST be treated as an experimental and optional feature, not as a boot-critical or OS-critical subsystem.
- XHGC Link v1 MUST only require an `8-bit` data bus plus the minimum control set needed for readout.
- The PCB MAY reserve routing and connector positions for future `16-bit` XHGC data mode, but the MCU side does NOT need to populate all expansion signals in v1.
- The firmware SHOULD be able to read the XHGC v1 data bus using register-level `GPIOx->IDR` access.
- The planning SHOULD prefer low system impact over theoretical peak GPIO read performance.
- The planning MUST remain reviewable by hardware, firmware, and documentation maintainers.

## Non-goals

- The v1 design does NOT attempt FMC-style memory-mapped cartridge access.
- The XHGC cartridge port does NOT use `SDMMC`, `SPI`, `QSPI`, or `FMC` as the transport.
- The v1 firmware does NOT require full `16-bit` mode to be enabled immediately.
- The v1 design does NOT require high-speed parallel write support.
- The v1 design does NOT require `IRQ_N`, `WR_N`, `MODE0`, or `MODE1` to be connected to the MCU.
- The project does NOT require full `JTAG`; only 2-wire `SWD` is mandatory.

## Hard Reservations

The following pins are hard reservations and MUST NOT be repurposed for XHGC, generic expansion, or low-priority board features.

| Pin | Reservation | Class | Source | Notes |
| --- | --- | --- | --- | --- |
| `PA11` | `USB_OTG_FS_DM` | `Reserved` | Planning constraint | USB FS MUST be preserved |
| `PA12` | `USB_OTG_FS_DP` | `Reserved` | Planning constraint | USB FS MUST be preserved |
| `PA13` | `SWDIO` | `Reserved` | `cartdesk_io_pinout.csv`, `cartdesk-os.ioc`, `Core/Src/gpio.c` | 2-wire SWD debug |
| `PA14` | `SWCLK` | `Reserved` | `cartdesk_io_pinout.csv`, `cartdesk-os.ioc`, `Core/Src/gpio.c` | 2-wire SWD debug |
| `PH0` | `RCC_OSC_IN` | `Reserved` | `cartdesk_io_pinout.csv`, `cartdesk-os.ioc`, `Core/Src/gpio.c` | HSE crystal input |
| `PH1` | `RCC_OSC_OUT` | `Reserved` | `cartdesk_io_pinout.csv`, `cartdesk-os.ioc`, `Core/Src/gpio.c` | HSE crystal output |
| `PB8` | `I2C1_SCL` | `Reserved` | `cartdesk_io_pinout.csv`, `Core/Src/i2c.c` | EEPROM bus |
| `PB9` | `I2C1_SDA` | `Reserved` | `cartdesk_io_pinout.csv`, `Core/Src/i2c.c` | EEPROM bus |
| `PH4` | `I2C2_SCL` | `Reserved` | `cartdesk_io_pinout.csv`, `Core/Src/i2c.c` | Touch bus |
| `PH5` | `I2C2_SDA` | `Reserved` | `cartdesk_io_pinout.csv`, `Core/Src/i2c.c` | Touch bus |
| `PA9` | `USART1_TX` | `Reserved` | `cartdesk_io_pinout.csv`, `Core/Src/usart.c` | Debug UART |
| `PA10` | `USART1_RX` | `Reserved` | `cartdesk_io_pinout.csv`, `Core/Src/usart.c` | Debug UART |

## Existing Peripheral Allocation

The following interfaces are already allocated by the current firmware and CubeMX configuration.

| Interface | Pins | Source | Notes |
| --- | --- | --- | --- |
| `FMC / SDRAM` | `PC0`, `PD0`, `PD1`, `PD8`, `PD9`, `PD10`, `PD14`, `PD15`, `PE0`, `PE1`, `PE7~PE15`, `PF0~PF5`, `PF11~PF15`, `PG0`, `PG1`, `PG2`, `PG4`, `PG5`, `PG8`, `PG15`, `PH6~PH15`, `PI0~PI7`, `PI9`, `PI10` | `cartdesk_io_pinout.csv`, `Core/Src/fmc.c` | MUST NOT be reused |
| `LTDC` | `PI12`, `PI13`, `PI14`, `PI15`, `PJ0~PJ15`, `PK0~PK7` | `cartdesk_io_pinout.csv`, `Core/Src/ltdc.c` | Display bus |
| `QUADSPI` | `PB2`, `PF6`, `PF7`, `PF8`, `PF9`, `PG6`, `PG9`, `PG14`, `PH2`, `PH3` | `cartdesk_io_pinout.csv`, `Core/Src/quadspi.c` | Dual QSPI flash |
| `SDMMC1` | `PC8`, `PC9`, `PC10`, `PC11`, `PC12`, `PD2` | `cartdesk_io_pinout.csv`, `Core/Src/sdmmc.c` | TF/SD card |
| `USART1 Debug` | `PA9`, `PA10` | `cartdesk_io_pinout.csv`, `Core/Src/usart.c` | Standard output path |
| `I2C1 EEPROM` | `PB8`, `PB9` | `cartdesk_io_pinout.csv`, `Core/Src/i2c.c` | External pull-ups expected |
| `I2C2 Touch` | `PH4`, `PH5` | `cartdesk_io_pinout.csv`, `Core/Src/i2c.c` | GT911 bus |
| `Touch Control` | `PG3`, `PG7` | `cartdesk_io_pinout.csv`, `Core/Src/gpio.c` | `PG3` EXTI, `PG7` reset |
| `LCD Backlight` | `PD13` | `cartdesk_io_pinout.csv`, `Core/Inc/main.h`, `Core/Src/gpio.c` | Output, active level review required |
| `Buzzer` | `PI8` | `cartdesk_io_pinout.csv`, `Core/Inc/main.h`, `Core/Src/gpio.c` | Output |
| `Board GPIO` | `PA0`, `PC2`, `PC13` | `cartdesk_io_pinout.csv`, `Core/Driver/GPIO/board_gpio.c`, `Core/Src/gpio.c` | `PC2` has conflicting init behavior and MUST be treated as used |
| `User GPIO / PWM` | `PA3`, `PA6`, `PA7`, `PB0`, `PB1` | `cartdesk_io_pinout.csv`, `cartdesk-os.ioc`, `Core/Driver/GPIO/board_gpio.c`, `Core/Driver/GPIO/board_pwm.c` | SHOULD remain available to board users unless explicitly re-approved |

## GPIO Risk Classes

This repository uses the following GPIO planning risk classes:

| Class | Meaning |
| --- | --- |
| `Reserved` | Pin is hard-reserved and MUST NOT be repurposed without an explicit design decision |
| `Used` | Pin is already allocated by the current board or firmware and MUST NOT be silently reused |
| `Preferred` | Pin is currently free and is a good default candidate for new planning |
| `Usable with caution` | Pin is available but has debug, trace, boot, clock, electrical, or future-expansion risk |
| `Avoid` | Pin SHOULD NOT be used for high-speed external interfaces |
| `Unknown` | Repository evidence is insufficient; manual review is required before use |

Repository-specific guidance:

- `PA11/PA12/PA13/PA14` are `Reserved`.
- `PB3/PB4/PB5/PA15` are `Usable with caution`.
- `PC14/PC15` SHOULD be treated as `Avoid` for XHGC data signals.
- Any pin already claimed by `FMC`, `LTDC`, `QSPI`, `SDMMC`, board I2C, debug UART, touch, buzzer, or backlight is `Used`.

## XHGC Cartridge Interface

The XHGC cartridge bus is planned as a mixed control/data interface:

- Data plane: parallel GPIO input stream
- Control plane: minimum GPIO control plus optional I2C sideband
- Upgrade path: v1 firmware uses `8-bit`; PCB MAY reserve `16-bit`

Feature classification:

- Status: `Experimental / Optional`
- Required for boot: `No`
- Required for normal OS operation: `No`
- Preferred fallback: `SD / QSPI / eMMC`

v1 required signals:

- `XHGC_D0~XHGC_D7`
- `XHGC_I2C_SCL`
- `XHGC_I2C_SDA`
- `XHGC_RD_CLK`
- `XHGC_READY`
- `XHGC_RESET_N`
- `XHGC_CART_DET`

v1 optional or reserved signals:

- `XHGC_D8~XHGC_D15`
- `XHGC_IRQ_N`
- `XHGC_WR_N`
- `XHGC_MODE0`
- `XHGC_MODE1`

Signal reduction priority when GPIO pressure increases:

1. Drop `XHGC_WR_N`
2. Drop `XHGC_IRQ_N`
3. Drop `XHGC_MODE0` and `XHGC_MODE1`
4. Drop `XHGC_D8~XHGC_D15`

Little-endian policy:

- `D0~D7` MUST represent the low byte
- `D8~D15` MUST represent the high byte
- A future `16-bit` read MUST yield a little-endian `uint16_t`

| Signal | Direction | Required in v1 | Required for 16-bit upgrade | Reset / default state | Electrical note | Firmware note |
| --- | --- | --- | --- | --- | --- | --- |
| `XHGC_D0` | Input to MCU | Yes | Yes | Input, no driven default from MCU | Parallel data bus | Low-byte bit 0 |
| `XHGC_D1` | Input to MCU | Yes | Yes | Input | Parallel data bus | Low-byte bit 1 |
| `XHGC_D2` | Input to MCU | Yes | Yes | Input | Parallel data bus | Low-byte bit 2 |
| `XHGC_D3` | Input to MCU | Yes | Yes | Input | Parallel data bus | Low-byte bit 3 |
| `XHGC_D4` | Input to MCU | Yes | Yes | Input | Parallel data bus | Low-byte bit 4 |
| `XHGC_D5` | Input to MCU | Yes | Yes | Input | Parallel data bus | Low-byte bit 5 |
| `XHGC_D6` | Input to MCU | Yes | Yes | Input | Parallel data bus | Low-byte bit 6 |
| `XHGC_D7` | Input to MCU | Yes | Yes | Input | Parallel data bus | Low-byte bit 7 |
| `XHGC_D8` | Input to MCU | No | Yes | Input | Parallel data bus | High-byte bit 0 |
| `XHGC_D9` | Input to MCU | No | Yes | Input | Parallel data bus | High-byte bit 1 |
| `XHGC_D10` | Input to MCU | No | Yes | Input | Parallel data bus | High-byte bit 2 |
| `XHGC_D11` | Input to MCU | No | Yes | Input | Parallel data bus | High-byte bit 3 |
| `XHGC_D12` | Input to MCU | No | Yes | Input | Parallel data bus | High-byte bit 4 |
| `XHGC_D13` | Input to MCU | No | Yes | Input | Parallel data bus | High-byte bit 5 |
| `XHGC_D14` | Input to MCU | No | Yes | Input | Parallel data bus | High-byte bit 6 |
| `XHGC_D15` | Input to MCU | No | Yes | Input | Parallel data bus | High-byte bit 7 |
| `XHGC_I2C_SCL` | Bidirectional open-drain | Yes | Yes | Deasserted high by pull-up | SHOULD use pull-up, MAY map to hardware I2C | Sideband control/config path |
| `XHGC_I2C_SDA` | Bidirectional open-drain | Yes | Yes | Deasserted high by pull-up | SHOULD use pull-up | Sideband control/config path |
| `XHGC_RD_CLK` | Input to MCU | Yes | Yes | Input | Sampling clock from cartridge side | Firmware SHOULD define edge explicitly |
| `XHGC_READY` | Input to MCU | Yes | Yes | Input, level TBD | External pull policy MUST be reviewed | Handshake / flow-control input |
| `XHGC_RESET_N` | Output from MCU | Yes | Yes | SHOULD default deasserted high | Active low reset | Safe reset sequencing required |
| `XHGC_CART_DET` | Input to MCU | Yes | Yes | Input, polarity TBD | Presence-detect line | Polarity MUST be finalized in schematic |
| `XHGC_CS_N` | Output from MCU | No | Yes | SHOULD default high | Active low chip select | Optional for simple experimental link variants |
| `XHGC_IRQ_N` | Input to MCU | No | No | Input | Active low interrupt | Reserved or optional |
| `XHGC_WR_N` | Output from MCU | No | No | SHOULD default high | Active low write strobe | Reserved or optional |
| `XHGC_MODE0` | Output from MCU or strap | No | No | SHOULD be static at boot | Mode select | Reserved or optional |
| `XHGC_MODE1` | Output from MCU or strap | No | No | SHOULD be static at boot | Mode select | Reserved or optional |

## Recommended XHGC Pin Assignment

Primary recommendation: reuse the current report’s `方案 C` as a low-impact base, but only treat the v1 minimum-signal subset as populated by default.

This is the preferred assignment because it preserves `USB FS`, preserves 2-wire `SWD`, avoids current user GPIO/PWM, does not disturb confirmed core peripherals, and allows the XHGC feature to remain optional.

| Signal | MCU Pin | GPIO Port | GPIO Bit | Direction | Active Level | v1 required? | 16-bit required? | Risk class | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `XHGC_D0` | `PE2` | `GPIOE` | `2` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D1` | `PE3` | `GPIOE` | `3` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D2` | `PE4` | `GPIOE` | `4` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D3` | `PE5` | `GPIOE` | `5` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D4` | `PE6` | `GPIOE` | `6` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D5` | `PD3` | `GPIOD` | `3` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D6` | `PD4` | `GPIOD` | `4` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D7` | `PD5` | `GPIOD` | `5` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D8` | `PD6` | `GPIOD` | `6` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D9` | `PD7` | `GPIOD` | `7` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D10` | `PD11` | `GPIOD` | `11` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D11` | `PD12` | `GPIOD` | `12` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D12` | `PC1` | `GPIOC` | `1` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D13` | `PC3` | `GPIOC` | `3` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D14` | `PC4` | `GPIOC` | `4` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D15` | `PC5` | `GPIOC` | `5` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_I2C_SCL` | `PB6` | `GPIOB` | `6` | Bidirectional OD | `high = idle` | Yes | Yes | `Preferred` | Hardware I2C candidate |
| `XHGC_I2C_SDA` | `PB7` | `GPIOB` | `7` | Bidirectional OD | `high = idle` | Yes | Yes | `Preferred` | Hardware I2C candidate |
| `XHGC_RD_CLK` | `PG10` | `GPIOG` | `10` | Input | `edge-triggered` | Yes | Yes | `Preferred` | Sampling edge review required |
| `XHGC_READY` | `PG11` | `GPIOG` | `11` | Input | `TBD` | Yes | Yes | `Preferred` | Pull policy review required |
| `XHGC_RESET_N` | `PG12` | `GPIOG` | `12` | Output | `low = active` | Yes | Yes | `Preferred` | SHOULD default high |
| `XHGC_CART_DET` | `PG13` | `GPIOG` | `13` | Input | `TBD` | Yes | Yes | `Usable with caution` | Trace-related alternate function exists |
| `XHGC_CS_N` | `PB10` | `GPIOB` | `10` | Output | `low = active` | No | Yes | `Preferred` | Optional reserve for richer link variants |
| `XHGC_IRQ_N` | `PB11` | `GPIOB` | `11` | Input | `low = active` | No | No | `Preferred` | Optional / reserved |
| `XHGC_WR_N` | `PB12` | `GPIOB` | `12` | Output | `low = active` | No | No | `Preferred` | Optional / reserved |
| `XHGC_MODE0` | `PA1` | `GPIOA` | `1` | Output | `TBD` | No | No | `Preferred` | Optional / reserved |
| `XHGC_MODE1` | `PA2` | `GPIOA` | `2` | Output | `TBD` | No | No | `Preferred` | Optional / reserved |

Default v1 MCU population set:

- `XHGC_D0~XHGC_D7`
- `XHGC_I2C_SCL`
- `XHGC_I2C_SDA`
- `XHGC_RD_CLK`
- `XHGC_READY`
- `XHGC_RESET_N`
- `XHGC_CART_DET`

Default v1 MCU non-required set:

- `XHGC_D8~XHGC_D15`
- `XHGC_CS_N`
- `XHGC_IRQ_N`
- `XHGC_WR_N`
- `XHGC_MODE0`
- `XHGC_MODE1`

## Alternative Pin Assignment

Backup recommendation: reuse the current report’s `方案 B` as an alternative low-impact layout.

This remains a valid alternative when PCB review prefers visually tighter byte-lane grouping, but it is no longer preferred over the primary plan on performance grounds alone.

| Signal | MCU Pin | GPIO Port | GPIO Bit | Direction | Active Level | v1 required? | 16-bit required? | Risk class | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `XHGC_D0` | `PD3` | `GPIOD` | `3` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D1` | `PD4` | `GPIOD` | `4` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D2` | `PD5` | `GPIOD` | `5` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D3` | `PD6` | `GPIOD` | `6` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D4` | `PD7` | `GPIOD` | `7` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D5` | `PE2` | `GPIOE` | `2` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D6` | `PE3` | `GPIOE` | `3` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D7` | `PE4` | `GPIOE` | `4` | Input | `n/a` | Yes | Yes | `Preferred` | Low byte |
| `XHGC_D8` | `PC3` | `GPIOC` | `3` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D9` | `PC4` | `GPIOC` | `4` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D10` | `PC5` | `GPIOC` | `5` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D11` | `PC6` | `GPIOC` | `6` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D12` | `PC7` | `GPIOC` | `7` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D13` | `PG10` | `GPIOG` | `10` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D14` | `PG11` | `GPIOG` | `11` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_D15` | `PG12` | `GPIOG` | `12` | Input | `n/a` | No | Yes | `Preferred` | High byte |
| `XHGC_I2C_SCL` | `PB6` | `GPIOB` | `6` | Bidirectional OD | `high = idle` | Yes | Yes | `Preferred` | Hardware I2C candidate |
| `XHGC_I2C_SDA` | `PB7` | `GPIOB` | `7` | Bidirectional OD | `high = idle` | Yes | Yes | `Preferred` | Hardware I2C candidate |
| `XHGC_RD_CLK` | `PB10` | `GPIOB` | `10` | Input | `edge-triggered` | Yes | Yes | `Preferred` | |
| `XHGC_READY` | `PB11` | `GPIOB` | `11` | Input | `TBD` | Yes | Yes | `Preferred` | |
| `XHGC_RESET_N` | `PB12` | `GPIOB` | `12` | Output | `low = active` | Yes | Yes | `Preferred` | |
| `XHGC_CART_DET` | `PB13` | `GPIOB` | `13` | Input | `TBD` | Yes | Yes | `Preferred` | |
| `XHGC_CS_N` | `PB14` | `GPIOB` | `14` | Output | `low = active` | No | Yes | `Preferred` | Optional reserve for richer link variants |
| `XHGC_IRQ_N` | `PB15` | `GPIOB` | `15` | Input | `low = active` | No | No | `Preferred` | Optional / reserved |
| `XHGC_WR_N` | `PA1` | `GPIOA` | `1` | Output | `low = active` | No | No | `Preferred` | Optional / reserved |
| `XHGC_MODE0` | `PA2` | `GPIOA` | `2` | Output | `TBD` | No | No | `Preferred` | Optional / reserved |
| `XHGC_MODE1` | `PI11` | `GPIOI` | `11` | Output | `TBD` | No | No | `Preferred` | Optional / reserved |

Advantages:

- Clustered byte-lane grouping is easier to read during schematic and PCB review.
- Signal grouping is visually cleaner for maintainers.
- It still preserves core system functions.

Disadvantages:

- Future `16-bit` reads require more `GPIOx->IDR` accesses than the primary plan.
- Runtime merge cost is higher than the primary plan.
- It consumes more optional routing complexity than the minimum v1 requirement truly needs.

## Rejected Options

### Rejected Option A

The prior performance-oriented layout is rejected for the current board revision.

| Reason | Details |
| --- | --- |
| USB conflict | `PA11` and `PA12` were assigned to `XHGC_D13` and `XHGC_D14` |
| Policy violation | `PA11/PA12` MUST remain reserved for `USB_OTG_FS_DM/DP` |
| Recommendation status | MUST NOT be used on the USB-capable board |
| Future reuse | MAY be reconsidered only on a future board variant that explicitly drops USB FS |

### Other Rejected Classes

- Any assignment using `PA13/PA14` is rejected because it is incompatible with 2-wire SWD.
- Any assignment that degrades `USB`, `SWD`, `LTDC`, `FMC / SDRAM`, `QSPI`, `SDMMC`, touch, debug UART, or existing board-level functions is rejected because XHGC is optional.
- Any assignment reusing active `FMC`, `LTDC`, `QSPI`, `SDMMC`, board I2C, debug UART, or board GPIO pins is rejected unless a separate design decision first removes that interface.

## Data Bus Read Cost

### Primary plan: Scheme C

- `8-bit` read: `2` `GPIOx->IDR` accesses
- `16-bit` read: `3` `GPIOx->IDR` accesses
- Merge cost: `mask + shift + merge`
- Inline suitability: excellent; SHOULD be implemented as a small static inline helper
- Firmware performance: good balance between runtime cost and pin-quality preservation

### Alternative plan: Scheme B

- `8-bit` read: `2` `GPIOx->IDR` accesses
- `16-bit` read: `4` `GPIOx->IDR` accesses
- Merge cost: `mask + shift + merge`
- Inline suitability: still suitable for static inline helpers
- Firmware performance: acceptable, but measurably worse than Scheme C for hot-path reads

## Firmware Naming Convention

CubeMX labels, generated pin macros, and board-level firmware code SHOULD use the following signal names verbatim:

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

Firmware-internal aggregate naming SHOULD follow:

- `XHGC_DATA_LO_*`
- `XHGC_DATA_HI_*`
- `XHGC_CTRL_*`

Recommended helper naming:

- `xhgc_read8()`
- `xhgc_read16()`
- `xhgc_cart_present()`
- `xhgc_ready()`
- `xhgc_reset_assert()`
- `xhgc_reset_release()`

## Connector Planning Notes

The external cartridge connector is planned as a `40P` interface.

- v1 MUST use `D0~D7`.
- The PCB SHOULD reserve `D8~D15` for the future `16-bit` upgrade.
- Power and ground pins MUST NOT be counted as MCU GPIO resources.
- The connector SHOULD provide multiple `GND` pins across the data/control groups.
- `RD_CLK` SHOULD be placed next to or near `GND`.
- Data lines SHOULD be grouped by byte lane.
- The PCB SHOULD reserve series resistor footprints on high-speed control/data lines.
- The external connector SHOULD include ESD protection strategy review.

## Expansion GPIO Policy

Not every free MCU pin SHOULD be consumed by XHGC.

Remaining GPIO budget SHOULD be preserved for:

- Expansion-card communication
- General-purpose header breakout
- User DIY experiments
- Optional debug or trace recovery
- Board-revision flexibility

Specific policy:

- `PA11/PA12/PA13/PA14` MUST remain unavailable to XHGC.
- `PB3/PB4/PB5/PA15` MAY be used only after explicit review.
- Existing user expansion GPIO/PWM pins (`PA3`, `PA6`, `PA7`, `PB0`, `PB1`) SHOULD stay outside the XHGC allocation unless a future revision intentionally reclaims them.

## Review Checklist

- [ ] `PA11/PA12` are still reserved for USB FS
- [ ] `PA13/PA14` are still reserved for SWD
- [ ] XHGC data-line naming matches the schematic
- [ ] CubeMX labels match the planning document
- [ ] `Core/Inc/main.h` macros match the generated labels
- [ ] `Core/Src/gpio.c` initialization directions match the intended control-line behavior
- [ ] `READY` and `CART_DET` pull-up/pull-down policy is finalized
- [ ] `RESET_N`, `CS_N`, and `WR_N` default levels are safe during reset and boot
- [ ] `D8~D15` have defined default behavior in `8-bit` v1 firmware
- [ ] The external connector provides enough `GND`
- [ ] USB, SWD, clock, and active external-memory pins remain untouched

## Maintenance Rules

- Any pin-assignment change MUST update:
  - this planning document
  - CubeMX `.ioc`
  - generated firmware macros
  - schematic symbol / sheet
  - `cartdesk_io_pinout.csv`
- New peripherals MUST be checked against hard reservations before assignment.
- Any change that consumes USB or SWD pins MUST be documented in `Rejected Options` or in a new ADR.
- Maintainers MUST NOT change pin assignments only in source code without updating the planning docs.
