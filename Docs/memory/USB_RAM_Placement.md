# USB RAM 放置

STM32H743 的 USB Device 运行期状态、类静态内存及 CDC 收发缓冲区统一放在
D2 SRAM 的 `.usb_ram` 段，不占用 128 KiB DTCM。

- 链接片段：`cmake/usb_ram.ld`
- 内存区域：`RAM_D2`（起始地址 `0x30000000`）
- 段对齐：32 字节
- 初始化：`MX_USB_DEVICE_Init()` 在启动 USB Device 栈前清零该 NOLOAD 段

当前 USB OTG HS 使用内部 FS PHY，并且 `dma_enable = DISABLE`。选择 D2 SRAM
可保留 DTCM，同时为以后启用 OTG DMA 留出可访问的内存位置；启用 DMA 时仍需
同步检查 MPU 属性以及 DCache clean/invalidate。

`Release-MinSize` 会启用 LTO。LTO 产生的临时 `ltrans` 对象名不再匹配链接脚本
中的源目标文件名，因此 USB 运行期对象同时显式标记为 `.usb_ram_data`；
USB middleware 与 PCD/LL USB 翻译单元在极限体积构建中保留普通 ELF 对象。
不得仅依靠 `*USB_DEVICE*(.bss)` 这类文件名通配规则判断 LTO 构建的放置结果。
每次修改后应使用 `arm-none-eabi-objdump -h` 确认 `.usb_ram` 非零且位于
`0x30000000`。

FreeRTOS 的 96 KiB heap 由 `Core/Src/freertos_heap.c` 放入 AXI SRAM 的
`.ram_runtime` 段，避免它与 USB 状态重新挤占 DTCM。
