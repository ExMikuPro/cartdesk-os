# SDMMC DMA RAM 放置

FatFs 在启用 FreeRTOS 后使用 CubeMX 的 SDMMC DMA+RTOS 模板。STM32H743
的 SDMMC1 IDMA 无法访问 `0x20000000` 开始的 DTCMRAM，因此 FatFs 文件系统
对象和扇区窗口必须放在 DMA 可访问的 SRAM。

当前工程通过 `cmake/sdmmc_ram.ld` 将 `fatfs.c` 和 `sd_diskio.c` 的静态
工作区放入 D1 AXI SRAM 的 `.sdmmc_ram` 段。启动时由 `main.c` 在
`USER CODE BEGIN Init` 中清零该段。SDMMC1 全局中断使用优先级 5，
以便完成回调能够调用 CMSIS-RTOS2 消息队列。

FatFs 对象、路径、返回状态、消息队列和 512 字节 scratch buffer 均显式标记为
`.sdmmc_ram_data`。这是 `Release-MinSize` 的 LTO 正确性要求：LTO 会把源目标
合并为临时 `ltrans` 对象，使链接脚本中的 `*fatfs.c.obj` 文件名匹配失效。
每次修改后应使用 `arm-none-eabi-objdump -h` 检查 `.sdmmc_ram` 非零且位于
AXI SRAM；加入 Cart 预览 staging buffer 后，当前完整段为 1,952 字节。

Cart 预览读取使用的 800 字节逐行 staging buffer 也属于 SD 数据路径，必须显式
放在 `.sdmmc_ram_data`，不能依赖链接器碰巧将普通 `.bss` 放到 DMA 可访问区。
SD 驱动的直接 DMA 路径还会同时检查 32 字节对齐和 DTCMRAM 地址范围；即使未来
有调用者传入恰好对齐的 DTCM buffer，也会自动改走 AXI scratch buffer。

SDMMC DMA 读写启用了 DCache clean/invalidate。新增直接传给
`disk_read()` 或 `disk_write()` 的大块缓冲区时，缓冲区也必须位于
SDMMC1 IDMA 可访问的 SRAM 或外部 SDRAM，不能位于 DTCMRAM。

FatFs 内部扇区窗口不保证 32 字节对齐，因此同时启用了 CubeMX 模板的
`ENABLE_SCRATCH_BUFFER`。非对齐传输会先经过 AXI SRAM 中独立、32 字节
对齐的 512 字节 scratch buffer，避免缓存行维护破坏相邻的文件系统状态。
