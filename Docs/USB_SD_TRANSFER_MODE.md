# USB SD 传输模式

USB SD 传输模式把板载 SD 卡作为 USB Mass Storage Class（MSC）的 LUN 0 暴露给电脑。电脑读写的是整张 SD 卡，不是内部 Flash、RAM Disk 或 `cart.bin` 的单文件镜像。

## 构建

该功能只在专用调试 preset 中启用：

```sh
cmake --preset Debug-USB-SD-MSC
cmake --build --preset Debug-USB-SD-MSC -j8
```

产物位于：

```text
build/Debug-USB-SD-MSC/cartdesk-os.elf
```

该 preset 使用 `Debug` 构建类型，因此编译参数保持 `-Og -g3`，适合使用 ST-Link、OpenOCD 和 GDB 设置断点、查看局部变量及单步调试。默认 `Debug` preset 不编入 MSC，仍保持原来的 USB VCP 行为。

## 使用

1. 烧录 `Debug-USB-SD-MSC` 固件并进入 Launcher。
2. 点击 `A 起动` 旁边的 `Y 传输`。
3. 固件等待当前 SD 请求结束，卸载 FatFs，并把 USB 从 CDC/VCP 重新枚举为 MSC。
4. 在电脑上正常复制、替换或删除 SD 卡文件。
5. 先在电脑操作系统中安全弹出该磁盘。
6. 点击 Launcher 中的 `Y 退出传输`，USB 会恢复为 CDC/VCP，Launcher 随后重新扫描 SD 卡。

USB 重枚举时 VCP 会暂时消失；ST-Link GDB 链路不使用这条 USB Device 接口，因此仍可继续调试。
为避免触摸抖动在重枚举期间立即触发相反操作，传输按钮会忽略前一次切换后 1 秒内的重复点击。

## 存储所有权

传输模式使用互斥所有权，防止 FatFs 和电脑同时访问同一 FAT 卷：

- `CartIoService_BeginSdExclusive()` 只在 SD 请求计数为零时授予独占权。
- 获得独占权后，新的 Launcher、cart 和 crash-log SD 请求会被拒绝。
- `SD_FATFS_Unmount()` 在 MSC 启动前解除固件侧挂载。
- Launcher 在 MSC 活动期间停止周期性 `cart.bin` 探测。
- 退出时先停止 MSC，再中止残留 SD DMA、重置完成队列并重新初始化 SDMMC。
- SDMMC 恢复后释放独占权，下一次 Launcher 探测会重新挂载 FatFs。

如果界面显示 `SD is busy`，等待当前卡带探测完成后再次点击 `Y 传输`。

## 数据安全限制

- 退出传输模式前必须先在电脑上安全弹出磁盘，否则电脑仍可能有未写回的数据。
- 传输模式期间不能启动依赖 SD 卡的 Lua 应用。
- 不要在电脑写盘时复位、断电或拔出 SD 卡。
- 当前实现是 CDC 与 MSC 运行时切换，不是 CDC+MSC 同时工作的复合设备。

## GDB 观察点

建议断点：

```text
UsbSdTransferMode_Enter
UsbSdTransferMode_Exit
USB_Device_SetSdMscMode
storage_read
storage_write
CartIoService_BeginSdExclusive
```

关键状态：

```text
USB_Device_IsSdMscMode()
CartIoService_IsSdExclusive()
```
