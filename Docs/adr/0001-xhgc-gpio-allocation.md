# ADR-0001: XHGC GPIO Allocation

## Status

Proposed

## Context

The project explores an experimental XHGC cartridge interface that must coexist with an already dense `STM32H743XIH6 / TFBGA240` board:

- `FMC / SDRAM` is already in use
- `LTDC` is already in use
- `QSPI` is already in use
- `SDMMC` is already in use
- `USB FS` must be preserved
- 2-wire `SWD` must be preserved

The cartridge interface is planned as:

- a parallel GPIO data bus
- a sideband `I2C` control path
- a minimal set of GPIO control and status signals

The design must support:

- v1 `8-bit` reads
- optional PCB reservation for future `16-bit` reads
- little-endian byte-lane semantics

The feature is explicitly non-critical:

- it is not required for boot
- it is not required for normal OS operation
- it must not take priority over `USB`, `SWD`, `LTDC`, `FMC / SDRAM`, `QSPI`, `SDMMC`, touch, debug UART, or comparable core functions
- normal fallback paths remain `SD / QSPI / eMMC`

## Decision

The project adopts the current report’s `方案 C` as the primary low-impact XHGC GPIO base assignment.

The project also decides:

- `USB FS` is preserved on `PA11/PA12`
- 2-wire `SWD` is preserved on `PA13/PA14`
- XHGC is `Experimental / Optional`
- v1 firmware uses only the minimum `8-bit` read subset
- `D8~D15` are future reserve signals, not v1 requirements
- `IRQ_N`, `WR_N`, `MODE0`, and `MODE1` are optional or reserved
- the PCB may keep a `40P` connector and future expansion pads without requiring full MCU population
- `方案 B` remains the documented backup assignment

## Consequences

### Positive

- USB remains available
- SWD remains available
- Existing major peripherals remain undisturbed
- XHGC can be omitted without affecting boot or normal OS operation
- Future `16-bit` expansion remains possible if later justified
- Existing major peripherals remain undisturbed
- The primary plan supports a small v1 implementation footprint

### Negative

- The data bus is not fully contiguous
- Future `16-bit` reads still require `mask/shift/merge`
- Firmware still needs explicit inline bus-read helpers if the feature is implemented
- Connector and schematic review must carefully separate required v1 wiring from optional reserve wiring

## Alternatives Considered

### Scheme A

Rejected.

Reason:

- It uses `PA11/PA12`
- That conflicts with the requirement to preserve `USB_OTG_FS_DM/DP`

### Scheme B

Kept as an alternative.

Reason:

- It preserves USB and SWD
- It is visually cleaner for PCB and schematic review
- It costs more `GPIOx->IDR` accesses for future `16-bit` reads

### Pure SPI / QSPI / SDMMC

Rejected.

Reason:

- Those peripherals are already allocated or conflict with the intended cartridge model
- The project goal is a GPIO-based parallel read bus, not a serial cartridge transport

### FMC

Rejected.

Reason:

- `FMC` is already committed to SDRAM
- The board does not have spare FMC resources for the cartridge bus

## Follow-up

- Keep XHGC documentation marked as optional unless the product goal changes
- Only update CubeMX labels if the feature is explicitly approved for implementation
- Only update generated `main.h` GPIO labels if the feature is explicitly approved for implementation
- Introduce an `xhgc_cart_bus` firmware abstraction later only if the optional feature is implemented
- Update the schematic and connector symbol to distinguish required v1 pins from optional reserve pins
- Update `cartdesk_io_pinout.csv` when hardware wiring is finalized
- Keep `docs/hardware/gpio-plan.md`, `docs/hardware/gpio-map.csv`, and `docs/hardware/gpio-map.yaml` synchronized
