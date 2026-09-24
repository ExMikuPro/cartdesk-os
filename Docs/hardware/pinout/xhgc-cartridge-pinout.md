# XHGC Cartridge Pinout

## Connector Overview

- Connector type: `40P`
- Interface family: `XHGC cartridge`
- v1 mode: `8-bit parallel data + I2C control`
- PCB policy: `16-bit-ready`
- External protocol model: `I2C control + parallel data stream`

This document defines the recommended external connector signal plan. It is a connector-level proposal and MUST NOT be confused with MCU GPIO assignment.

See also:

- MCU-side GPIO plan: `docs/hardware/gpio-plan.md`
- Machine-readable map: `docs/hardware/gpio-map.yaml`

## Logical Signals

The external XHGC connector groups signals into:

- Low byte data lane: `D0~D7`
- High byte data lane: `D8~D15`
- Sideband control: `RD_CLK`, `READY`, `CS_N`, `RESET_N`, `IRQ_N`, `WR_N`, `MODE0`, `MODE1`, `CART_DET`
- Sideband configuration: `I2C_SCL`, `I2C_SDA`
- Power and ground

Little-endian rule:

- `D0~D7` are the low byte
- `D8~D15` are the high byte
- Future `16-bit` reads map to a little-endian `uint16_t`

## Recommended Connector Pinout

This pinout is a proposed external connector arrangement. It is not a statement of current hardware implementation.

| Pin | Signal | Direction | Required in v1 | Reserved for v2 | Notes |
| --- | --- | --- | --- | --- | --- |
| 1 | `GND` | Power | Yes | Yes | Ground |
| 2 | `VCC_CART` | Power | Yes | Yes | Cartridge supply, level TBD |
| 3 | `XHGC_D0` | Cart -> MCU | Yes | Yes | Low byte |
| 4 | `XHGC_D1` | Cart -> MCU | Yes | Yes | Low byte |
| 5 | `XHGC_D2` | Cart -> MCU | Yes | Yes | Low byte |
| 6 | `XHGC_D3` | Cart -> MCU | Yes | Yes | Low byte |
| 7 | `GND` | Power | Yes | Yes | Byte-lane return |
| 8 | `XHGC_D4` | Cart -> MCU | Yes | Yes | Low byte |
| 9 | `XHGC_D5` | Cart -> MCU | Yes | Yes | Low byte |
| 10 | `XHGC_D6` | Cart -> MCU | Yes | Yes | Low byte |
| 11 | `XHGC_D7` | Cart -> MCU | Yes | Yes | Low byte |
| 12 | `GND` | Power | Yes | Yes | Byte-lane return |
| 13 | `XHGC_RD_CLK` | Cart -> MCU | Yes | Yes | Read/sample clock |
| 14 | `XHGC_READY` | Cart -> MCU | Yes | Yes | Handshake |
| 15 | `XHGC_CS_N` | MCU -> Cart | Yes | Yes | Active low |
| 16 | `XHGC_RESET_N` | MCU -> Cart | Yes | Yes | Active low |
| 17 | `GND` | Power | Yes | Yes | Control return |
| 18 | `XHGC_IRQ_N` | Cart -> MCU | No | Yes | Optional in v1 |
| 19 | `XHGC_WR_N` | MCU -> Cart | No | Yes | Optional in v1 |
| 20 | `XHGC_MODE0` | MCU -> Cart | Yes | Yes | Static mode control |
| 21 | `XHGC_MODE1` | MCU -> Cart | Yes | Yes | Static mode control |
| 22 | `XHGC_CART_DET` | Cart -> MCU | Yes | Yes | Presence detect |
| 23 | `GND` | Power | Yes | Yes | Sideband return |
| 24 | `XHGC_I2C_SCL` | Bidirectional OD | Yes | Yes | Sideband I2C |
| 25 | `XHGC_I2C_SDA` | Bidirectional OD | Yes | Yes | Sideband I2C |
| 26 | `RESERVED_AUX0` | TBD | No | Yes | Future debug or mode use |
| 27 | `RESERVED_AUX1` | TBD | No | Yes | Future debug or mode use |
| 28 | `GND` | Power | Yes | Yes | High-byte return |
| 29 | `XHGC_D8` | Cart -> MCU | No | Yes | High byte |
| 30 | `XHGC_D9` | Cart -> MCU | No | Yes | High byte |
| 31 | `XHGC_D10` | Cart -> MCU | No | Yes | High byte |
| 32 | `XHGC_D11` | Cart -> MCU | No | Yes | High byte |
| 33 | `GND` | Power | Yes | Yes | High-byte return |
| 34 | `XHGC_D12` | Cart -> MCU | No | Yes | High byte |
| 35 | `XHGC_D13` | Cart -> MCU | No | Yes | High byte |
| 36 | `XHGC_D14` | Cart -> MCU | No | Yes | High byte |
| 37 | `XHGC_D15` | Cart -> MCU | No | Yes | High byte |
| 38 | `GND` | Power | Yes | Yes | High-byte return |
| 39 | `VCC_CART` | Power | Yes | Yes | Additional supply pin |
| 40 | `GND` | Power | Yes | Yes | Ground |

## Byte Lane Grouping

Recommended grouping:

- Low byte: `XHGC_D0~XHGC_D7`
- High byte: `XHGC_D8~XHGC_D15`

Routing policy:

- The PCB SHOULD keep low-byte routing visually grouped.
- The PCB SHOULD keep high-byte routing visually grouped.
- The connector pinout SHOULD make byte-lane review obvious during schematic and layout review.

## Power and Ground Policy

- Power and ground pins are connector resources, not MCU GPIO resources.
- The connector SHOULD provide multiple `GND` pins distributed across the pin field.
- `RD_CLK` SHOULD have a nearby `GND` return.
- At least two power pins MAY be used when current budget or edge-rate concerns justify it.
- Final supply voltage naming (`3V3`, `5V`, or dual-rail policy) remains `TBD` and MUST be confirmed in schematic review.

## Signal Integrity Notes

- The PCB SHOULD place series resistor footprints on `RD_CLK`, `CS_N`, `WR_N`, and optionally the data bus.
- The PCB SHOULD review trace length matching within each byte lane.
- High-speed return paths SHOULD be considered during connector escape routing.
- The external interface SHOULD include ESD review and protection placement.
- If hot plugging is expected, insertion sequence and protection MUST be reviewed explicitly.

## Unused / Reserved Pin Handling

- Unused connector pins SHOULD default to `GND`, reserved pads, or future-compatible no-connect definitions.
- Reserved auxiliary pins MUST NOT be silently reused without updating:
  - `docs/hardware/gpio-plan.md`
  - `docs/hardware/gpio-map.yaml`
  - the schematic
  - the connector pinout document

## Hot-plug / Insertion Notes

- Hot-plug behavior is not yet defined by the current repository and remains `TBD`.
- Until proven otherwise, the design SHOULD assume controlled insertion only.
- If hot-plug is later supported, the board MUST review:
  - supply sequencing
  - `RESET_N` default behavior
  - `CART_DET` debounce and polarity
  - bus contention during insertion

## Versioning Policy

- v1:
  - `8-bit` data bus active
  - `D8~D15` routed but not required by firmware
  - `IRQ_N` and `WR_N` MAY be left functionally unused in firmware
- v2:
  - `16-bit` data bus enabled
  - `D8~D15` become active firmware signals
- Future:
  - write-path support
  - richer IRQ usage
  - optional debug or manufacturing modes through reserved auxiliary pins

## MCU GPIO Assignment vs Connector Pinout

This document only defines connector-level planning.

- MCU GPIO assignment is defined in `docs/hardware/gpio-plan.md`.
- External connector numbering is defined here.
- Reviewers MUST NOT mix connector pin numbers with MCU GPIO names.
