# LVGL 9.4 移植文件包 - STM32H743

## 文件列表

### 核心驱动文件 (放在 Core/APPS/LVGL/port/)
```
lv_port_disp.c        # 显示驱动实现
lv_port_disp.h        # 显示驱动头文件
lv_port_tick.c        # 时钟驱动实现
lv_port_tick.h        # 时钟驱动头文件
lv_port_indev.c       # 输入设备驱动(触摸屏框架,暂未启用)
lv_port_indev.h       # 输入设备驱动头文件
lvgl_init.c           # LVGL初始化总入口
lvgl_init.h           # 初始化头文件
```

### 配置文件 (放在 Core/APPS/LVGL/)
```
lv_conf.h             # LVGL配置文件
```

## 使用说明

### 1. 文件放置位置

根据你的CMakeLists.txt,文件应放置在:
```
Core/APPS/LVGL/port/lv_port_disp.c
Core/APPS/LVGL/port/lv_port_disp.h
Core/APPS/LVGL/port/lv_port_tick.c
Core/APPS/LVGL/port/lv_port_tick.h  
Core/APPS/LVGL/port/lv_port_indev.c
Core/APPS/LVGL/port/lv_port_indev.h
Core/APPS/LVGL/port/lvgl_init.c
Core/APPS/LVGL/port/lvgl_init.h
Core/APPS/LVGL/lv_conf.h
```

### 2. main.c中的调用示例

```c
#include "main.h"
#include "lvgl_init.h"
#include "lvgl.h"

int main(void)
{
    /* MCU初始化 */
    HAL_Init();
    SystemClock_Config();
    
    /* 外设初始化 */
    MX_GPIO_Init();
    MX_LTDC_Init();
    MX_DMA2D_Init();
    MX_FMC_Init();  // SDRAM初始化
    
    /* LVGL初始化 */
    lvgl_init();
    
    /* 创建UI示例 */
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello LVGL 9.4!");
    lv_obj_center(label);
    
    /* 主循环 */
    while (1)
    {
        lvgl_task_handler();
        HAL_Delay(5);  // 5ms调用一次
    }
}
```

### 3. 关键特性说明

#### 双缓冲配置
- **Layer0**: 单缓冲 (暂不使用,可用于背景)
- **Layer1**: 双缓冲 (LVGL使用)
  - Front buffer: 0xD0000000 + FB_SIZE
  - Back buffer:  0xD0000000 + FB_SIZE*2
  
#### 颜色格式
- LCD驱动使用: ARGB8888 (0xAARRGGBB)
- LVGL配置为: LV_COLOR_FORMAT_ARGB8888
- 驱动已自动处理字节序转换

#### VBlank同步
- LCD驱动提供VBlank计数器
- 显示驱动会等待pending swap完成
- 支持获取帧率信息

#### 缓冲区策略
- LVGL使用部分缓冲(50行 = 1/10屏幕)
- 双缓冲减少内存占用
- buf_1和buf_2各占 800*50*4 = 160KB

### 4. 触摸屏启用方法

当你准备好触摸屏驱动后:

1. 在 `lv_port_indev.c` 中设置:
```c
#define TOUCHPAD_ENABLED    1
```

2. 实现触摸屏读取函数:
```c
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    // 调用你的触摸屏驱动
    // 示例: TouchScreen_Read(&x, &y, &pressed);
}
```

3. 初始化触摸屏硬件:
```c
static void touchpad_init(void)
{
    // 初始化I2C/SPI
    // 复位触摸IC
    // 配置中断
}
```

### 5. 性能优化建议

#### 使用DMA2D加速 (可选)
如果需要进一步提升性能,可以考虑让LVGL直接使用DMA2D:
- 修改 `lv_port_disp.c` 中的flush函数
- 使用DMA2D的内存到内存传输
- 参考你的LCD驱动中的DMA2D_FillRect实现

#### 内存优化
当前配置使用了:
- LVGL内存池: 128KB (SRAM)
- 显示缓冲: 320KB (buf_1 + buf_2, SRAM)
- LCD Framebuffer: 4.5MB (SDRAM)

如果SRAM紧张,可以:
```c
// 将buf_1和buf_2放到SDRAM
static uint32_t buf_1[DISP_BUF_SIZE] __attribute__((section(".sdram_bss")));
```

### 6. 调试方法

#### 启用LVGL日志
在 `lv_conf.h` 中:
```c
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_TRACE
#define LV_LOG_PRINTF 1
```

#### 查看帧率
```c
uint32_t fps = lv_port_disp_get_fps_delta();
printf("FPS delta: %lu\n", fps);
```

#### 检查VBlank
```c
uint32_t vblank_count = LCD_GetVBlankCount();
printf("VBlank count: %lu\n", vblank_count);
```

## 常见问题

### Q1: 屏幕没有显示
检查:
1. LCD_DoubleBufferInit()是否正确调用
2. Layer1是否设置为可见
3. 背光是否打开
4. LTDC和DMA2D是否正确初始化

### Q2: 显示撕裂
检查:
1. VBlank中断是否正常工作
2. LCD_IsPendingSwap()是否正确返回
3. LCD_Refresh()是否在flush中调用

### Q3: 性能不佳
优化:
1. 减少DISP_BUF_LINES (牺牲内存换速度)
2. 使用DMA2D加速flush
3. 降低LV_DEF_REFR_PERIOD
4. 禁用不需要的控件和特效

### Q4: 内存不足
解决:
1. 减少LV_MEM_SIZE
2. 减少DISP_BUF_SIZE
3. 将缓冲区放到SDRAM
4. 禁用不需要的字体

## 版本信息

- LVGL版本: 9.4
- MCU: STM32H743
- 屏幕: 800x480 ARGB8888
- 作者: Claude
- 日期: 2025

## 下一步

1. 从LVGL官方仓库下载LVGL 9.4源码
2. 将本移植文件包放到对应目录
3. 在main.c中调用lvgl_init()
4. 开始创建UI界面

祝你移植顺利! 🎉
