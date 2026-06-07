# DMA2D 适配逻辑

本文档描述当前 `cartdesk-os` 工程中的 DMA2D 接入方式，以及后续把 LVGL 官方 DMA2D backend 打开的正确路径。

## 1. 当前工程的 DMA2D 分工

当前工程里，DMA2D 实际分成两条链路：

1. `LCD` 驱动自用 DMA2D
2. `LVGL` 官方 `draw/dma2d` backend

这两条链路现在的状态不一样：

1. `LCD` 驱动链路已经在工作，主要用于 framebuffer 级别的矩形填充和双缓冲提交。
2. `LVGL` 官方 backend 目前仍然关闭，见 [lv_conf.h](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/APPS/LVGL/lv_conf.h:115)：

```c
#define LV_USE_DRAW_DMA2D           0
#define LV_DRAW_DMA2D_HAL_INCLUDE   "stm32h7xx_hal.h"
#define LV_USE_DRAW_DMA2D_INTERRUPT 0
```

也就是说，当前 UI 显示路径是：

1. LVGL 主要走软件渲染
2. 显示扫描和翻页走 LTDC
3. 局部底层图元填充、buffer 操作由 `lcd.c` 直接调用 DMA2D

## 2. DMA2D 硬件初始化

DMA2D 的底层初始化来自 CubeMX 生成文件 [dma2d.c](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/Src/dma2d.c:27)。

当前配置是：

1. `Mode = DMA2D_M2M`
2. 输出色彩格式 `DMA2D_OUTPUT_ARGB8888`
3. 输入层格式 `DMA2D_INPUT_ARGB8888`
4. 中断 `DMA2D_IRQn` 已打开

这个初始化本身更像“通用默认配置”。真正业务里很多传输参数不是靠 `HAL_DMA2D_Start()` 高层接口灌进去，而是在 `lcd.c` 里直接写 DMA2D 寄存器完成。

## 3. LCD 驱动里的 DMA2D 适配逻辑

### 3.1 使用目标

在 [lcd.c](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/Driver/LCD/lcd.c:269) 中，当前 DMA2D 主要承担一件事：

1. 用 `R2M` 模式向 ARGB8888 framebuffer 填充矩形

核心函数是 [DMA2D_FillRect](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/Driver/LCD/lcd.c:279)。

它的执行步骤是：

1. `WaitDMA2D()` 轮询等待上一次 DMA2D 任务结束
2. 计算目标 framebuffer 区域起始地址
3. 对目标区域做一次 `DCache clean`
4. 直接配置 DMA2D 寄存器
5. 置位 `DMA2D_CR_START` 启动传输

对应寄存器写入逻辑：

1. `CR = DMA2D_R2M | DMA2D_CR_TCIE`
2. `OCOLR = color`
3. `OMAR = region_start`
4. `OOR = LCD_W - w`
5. `NLR = width/height`
6. `OPFCCR = DMA2D_OUTPUT_ARGB8888`

这表示当前工程假设主显示 buffer 是：

1. 像素格式 `ARGB8888`
2. 一行 stride 按整屏 `LCD_W`
3. DMA2D 写入目标在 SDRAM framebuffer

### 3.2 为什么先 clean DCache

这一步是当前适配里最重要的稳定性逻辑之一，见 [LCD_DCacheClean](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/Driver/LCD/lcd.c:249) 和 [DMA2D_FillRect](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/Driver/LCD/lcd.c:287)。

原因是：

1. Cortex-M7 开了 D-Cache
2. DMA2D 是外设写内存，不会自动维护 CPU cache
3. 如果目标区域在 cache 中有旧脏数据，后续 CPU 回写可能把 DMA2D 的结果覆盖掉

所以这里采用的策略是：

1. DMA2D 写目标区域前，先 clean 目标区域 cache line
2. clean 时按 `32-byte` cache line 对齐

这和 [lv_conf.h](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/APPS/LVGL/lv_conf.h:27) 里把内存和 draw buffer 对齐到 `32` 字节的思路是一致的。

## 4. 双缓冲与 DMA2D 的配合

当前显示驱动的真正核心，不是“DMA2D 画完就直接上屏”，而是：

1. CPU 或 DMA2D 都只往 `back buffer` 画
2. `LCD_Refresh()` 提交 dirty 区
3. `HAL_LTDC_LineEventCallback()` 在 VBlank 时机执行 front/back 翻页

关键代码：

1. [LCD_Refresh](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/Driver/LCD/lcd.c:1208)
2. [LCD_DoubleBufferInit](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/Driver/LCD/lcd.c:1239)
3. [HAL_LTDC_LineEventCallback](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/Driver/LCD/lcd.c:1305)

这条链路里 DMA2D 的职责是“加速写 back buffer”，而不是负责 page flip。

page flip 的职责是 LTDC：

1. `LCD_Refresh()` 只把 `pending_swap = 1`
2. 真正交换 `front_addr/back_addr` 的动作发生在 `HAL_LTDC_LineEventCallback()`
3. 然后更新 `CFBAR`
4. 再用 `HAL_LTDC_Reload(..., LTDC_RELOAD_VERTICAL_BLANKING)` 保证无撕裂生效

## 5. DMA2D 中断接入点

DMA2D 中断在 [stm32h7xx_it.c](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/Src/stm32h7xx_it.c:250) 已经留好接口。

当前逻辑是：

1. 先保存 `DMA2D->ISR`
2. 调 `HAL_DMA2D_IRQHandler(&hdma2d)`
3. 如果启用了 LVGL DMA2D 中断模式，则在 `TCIF` 到来时调用 `lv_draw_dma2d_transfer_complete_interrupt_handler()`

现在因为 `LV_USE_DRAW_DMA2D = 0`，这段逻辑不会真正参与 LVGL 绘制。

换句话说：

1. 中断框架已经在
2. 只是 LVGL 官方 DMA2D backend 还没打开

## 6. LVGL 官方 DMA2D backend 为什么现在没开

从配置和现象看，之前关闭它的原因是：

1. 开启后出现花屏
2. VSync / flush / page flip / DMA2D 完成时序没有完全接好

在当前工程里，容易出问题的点主要有四个：

1. LVGL direct render + LTDC 双缓冲本身已经在做 buffer 轮换
2. `lcd.c` 也有自己的一套 front/back 管理
3. DMA2D 完成中断如果没正确通知 LVGL，draw task 会卡住或时序错乱
4. DCache 对 clean/invalidate 的边界要求很严格

所以当前工程实际上采用的是更保守的方案：

1. 先保证 LTDC 双缓冲稳定
2. 再用 `lcd.c` 层面的 DMA2D 做局部加速
3. 暂不让 LVGL 自己调度 DMA2D draw unit

## 7. 后续如果要打开 LVGL DMA2D backend，建议按这个顺序

### 第一步：打开配置

在 [lv_conf.h](/Volumes/Sector0/AppData/Clion/cartdesk-os/Core/APPS/LVGL/lv_conf.h:115) 里改成：

```c
#define LV_USE_DRAW_DMA2D           1
#define LV_DRAW_DMA2D_HAL_INCLUDE   "stm32h7xx_hal.h"
#define LV_USE_DRAW_DMA2D_INTERRUPT 1
```

如果后续仍是裸机 `LV_OS_NONE`，建议先确认 LVGL 当前版本的 DMA2D backend 是否以同步方式工作；否则中断虽然开了，但并不会真正形成异步收益。

### 第二步：只保留一套 buffer 所有权

这是最关键的一步。

当前工程有两套“显示 buffer 管理思想”：

1. LVGL `lv_st_ltdc_create_direct(...)`
2. `lcd.c` 自己维护 `front_addr/back_addr/pending_swap`

后续必须二选一为主：

1. 要么完全以 LVGL direct render 为主，`lcd.c` 只做底层工具函数
2. 要么以 `lcd.c` 的双缓冲系统为主，LVGL flush 只提交到它的 draw buffer

如果两套系统同时各自认为自己拥有 buffer 交换权，最容易出现：

1. 花屏
2. 撕裂
3. 偶发旧帧回闪

### 第三步：确认 DMA2D 完成通知闭环

中断闭环应当是：

1. DMA2D 传输完成
2. 进入 `DMA2D_IRQHandler`
3. 调 `lv_draw_dma2d_transfer_complete_interrupt_handler()`
4. LVGL 对应 draw task 标记完成
5. flush / render pipeline 继续推进

如果缺失第 3 步，LVGL 会认为 DMA2D 任务还没结束。

### 第四步：补齐 cache 维护

原则上要区分：

1. CPU 写、DMA2D 读：先 clean
2. DMA2D 写、CPU 读：后 invalidate
3. LTDC 读 framebuffer：在提交显示前保证内存已 clean

当前 `lcd.c` 已经做了“DMA2D 写前 clean 目标区域”和“提交前 clean dirty 区域”。

如果后续改为 LVGL 官方 DMA2D backend 主导，需要再确认：

1. LVGL 自己的 cache clean/invalidate 与本工程 MPU/DCache 配置是否一致
2. 不要让 `lcd.c` 和 LVGL 重复维护同一块区域的 cache

## 8. 当前工程里 DMA2D 适配的结论

一句话总结：

1. 当前工程已经有一套稳定的“LTDC 双缓冲 + VBlank 翻页 + DMA2D 局部加速”方案
2. 但还没有完全切到“LVGL 官方 DMA2D draw unit 驱动渲染”

所以现在的 DMA2D 适配逻辑，本质上是：

1. DMA2D 作为 `lcd.c` 的硬件填充引擎
2. LTDC 负责显示时序与翻页
3. LVGL 目前仍以软件渲染为主
4. 等 buffer ownership、VSync 通知和 cache 维护完全统一后，再考虑打开 LVGL 官方 DMA2D backend

