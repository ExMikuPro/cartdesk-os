# STM32CubeMX 配置同步说明

本文记录 `cartdesk-os.ioc` 与当前固件实现的同步边界。硬件事实以
`cartdesk-os.ioc`、`Core/Src` 初始化文件及实际参与 CMake 构建的自定义移植文件为准。

## 本次核对范围

已核对主时钟和外设时钟、GPIO/复用、NVIC、MDMA、DMA2D、FMC SDRAM、LTDC、
QSPI、SDMMC1、RTC、I2C1/2、USART1、TIM2/3/17、CRC、RNG、USB Device、FatFs、
FreeRTOS，以及 `Core`、`FATFS`、`USB_DEVICE` 下会二次配置 HAL 外设的项目代码。

当前工程没有启用 ADC、DAC、SPI、FDCAN/CAN、HRTIM、COMP、OPAMP、独立/窗口看门狗；
源码中也没有发现需要反推到 IOC 的对应 HAL 初始化实例，因此未添加这些外设。

## 已确认并同步的差异

| 项目 | 修改前 IOC | 源码实际行为 | 处理结果 |
| --- | --- | --- | --- |
| EXTI3 优先级 | 抢占优先级 5 | `MX_GPIO_Init()` 设置为 0 | IOC 改为 0 |
| 生成函数列表 | 多个条目带非标准 `false-` 前缀，并把生成标志保存为 `true` | 临时生成时会删除大部分外设源文件 | 规范化 `ProjectManager.functionlistsort`；外设文件可正常重生成 |
| Fault Handler | IOC 允许生成 C Handler | `Core/Debug/fault_entry.S` 提供 HardFault、MemManage、BusFault、UsageFault 入口 | 关闭四个 C Handler 的生成，保留汇编入口 |
| SDMMC1 NVIC | IOC 已启用，优先级 5 | NVIC 初始化原来手写在 USER 区 | 恢复为 CubeMX 可生成位置，USER 区只保留板级 GPIO 操作 |
| LSI/RTC 时钟 | LSI 开启，RTC 选 LSI | LSI 原来在 RTC MSP 中延后开启 | `SystemClock_Config()` 与 CubeMX 生成结果一致地开启 LSI |

除上表外，当前生成初始化与 IOC 的可表达参数一致。已重点确认：

- 系统使用 HSE + PLL，CPU 480 MHz；HSI48 用于 USB/RNG，RTC 使用 LSI。
- USART1 为 115200、8N1、收发模式、无硬件流控。
- I2C1/I2C2 timing 均为 `0x307075B1`。
- SDMMC1 为 4-bit、上升沿、ClockDiv 8，IRQ 优先级 5。
- TIM2/TIM3 为 Prescaler 239、Period 999；TIM17 为 Prescaler 119、Period 65535。
- QSPI ClockPrescaler 1、FlashSize 25。
- LTDC、FMC SDRAM、MDMA、DMA2D、CRC、RNG 的实例、引脚、时钟和初始化参数与 IOC 生成结果一致。

## 无法安全同步到 IOC 的内容

以下行为不是 CubeMX 硬件配置，或 CubeMX 不能完整表达，因而保留在项目代码中：

- `Core/Driver/GPIO/board_gpio.c` 的运行时 GPIO 所有权和模式切换。
- `Core/Driver/GPIO/board_pwm.c` 对暴露引脚的运行时复用，以及 TIM2/TIM3 动态 PWM 配置。
  IOC 中这些 PWM 通道保持 `No Output`，基础引脚保持输入，避免启动时改变外部电平。
- `Core/Driver/SDRAM/sdram.c` 的 SDRAM JEDEC 命令序列和 refresh 编程。
- `Core/Src/rtc_custom.c` 的备份域保护：只有 RTCSEL 确实需要变化时才调用
  `HAL_RCCEx_PeriphCLKConfig()`，避免普通软件复位破坏 RTC 备份域。
- SDMMC DMA 可访问地址检查、bounce buffer、队列复位、写完成事件修正和性能统计，位于
  `FATFS/Target/sd_diskio_custom.c`。
- USB CDC/MSC 运行时切换、MSC 分配区和动态描述符，位于
  `USB_DEVICE/App/usb_device_custom.c`、`USB_DEVICE/App/usbd_desc_custom.c` 和
  `USB_DEVICE/Target/usbd_conf_custom.c`。
- `io` 与 `background` 两个业务任务。当前 CubeMX 6.18.1 使用该工程锁定的 DB 6.0.170
  时，即使 IOC 保存了这两个任务也不会生成它们；因此它们由 `freertos.c` USER 区创建，
  IOC 只保留能稳定生成的 `app` 与 `audio` 任务。

## Generate Code 保护策略

- 崩溃记录初始化放在 `usart.c` 的 USER 区，仍保持 USART1 初始化完成后执行。
- SDMMC 初始化耗时统计放在 `sdmmc.c` 的 Init USER 区。
- IRQ 性能统计及其头文件引用全部位于 `stm32h7xx_it.c` USER 区。
- `io`/`background` 任务的句柄、属性、创建和入口全部位于 `freertos.c` USER 区。
- CubeMX 仍可重建标准 `rtc.c`、`sd_diskio.c`、`usb_device.c`、`usbd_desc.c` 和
  `usbd_conf.c`；顶层 `CMakeLists.txt` 将这些标准翻译单元标记为不编译，并选择相应
  `*_custom.c`。不要把项目专用逻辑重新移回标准生成文件。
- 可选 MSC 的 include/source 接线位于顶层 `CMakeLists.txt`，不再修改 CubeMX 管理的
  `cmake/stm32cubemx/CMakeLists.txt`。

## 验证结果

使用 STM32CubeMX 6.18.1、工程锁定数据库 DB 6.0.170，在 `/tmp` 的完整工程副本中执行
Generate Code，随后完成以下构建：

```sh
cmake --preset Debug
cmake --build --preset Debug -j 6
cmake --preset Debug-USB-SD-MSC
cmake --build --preset Debug-USB-SD-MSC -j 6
```

两种配置均成功链接。CubeMX 日志仍会报告 USB CDC `DEVICE_SERIALx_CDC_HS` 的旧数据库
RefParameter 警告，以及未使用外设的派生频率/第三方包警告；这些警告没有阻止代码生成，
也未改变已启用外设的生成结果。

## 后续修改约束

修改硬件初始化时，优先先改 IOC，再生成并比较；运行时复用、协议、存储保护和业务任务
继续留在 USER 区或独立模块。若某项不能由当前 CubeMX 版本稳定重生成，应在本文件记录，
不要通过猜测 IOC 字段来逼近。
