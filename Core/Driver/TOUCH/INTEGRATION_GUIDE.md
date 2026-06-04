# GT911触摸屏 + LVGL 9.5 集成指南

## 📋 文件说明

### 已优化的文件
1. **touch.h** - GT911驱动头文件
2. **touch.c** - GT911驱动实现
3. **lv_port_indev.h** - LVGL输入设备接口头文件
4. **lv_port_indev.c** - LVGL输入设备接口实现

## 🔧 代码优化点

### 1. touch.c/h 优化

#### ✅ 改进之处:
- **结构化数据类型**: 引入 `GT911_TouchPoint_t` 和 `GT911_TouchData_t` 结构体
- **bool返回值**: 所有函数使用布尔返回值表示成功/失败,更清晰
- **移除中文注释**: 原代码中的乱码中文注释已全部替换为英文
- **简化I2C操作**: 移除了不必要的 `i2c_msg` 结构体,直接使用HAL库
- **边界检查**: 增加数组越界保护
- **宏定义优化**: 使用明确的寄存器地址宏定义
- **代码复用**: 提取公共I2C读写函数

#### 🔍 主要API:
```c
// 初始化
void GT911_Init(void);

// 扫描触摸事件
bool GT911_Scan(void);

// 获取触摸点数量
uint8_t GT911_Get_TouchNum(void);

// 获取指定触摸点坐标
bool GT911_Get_Point(uint8_t index, uint16_t *x, uint16_t *y);

// 读取完整触摸数据
bool GT911_Read_TouchData(GT911_TouchData_t *touch_data);
```

### 2. lv_port_indev.c/h 优化 (LVGL 9.5)

#### ✅ 改进之处:
- **LVGL 9.x API兼容**: 使用新的 `lv_indev_create()` 和 `lv_indev_set_*` API
- **坐标转换支持**: 添加宏开关支持XY交换、镜像翻转
- **边界检查**: 防止坐标超出屏幕范围
- **单点触摸优化**: 针对LVGL优化为单点模式(多点可扩展)
- **开关控制**: `TOUCHSCREEN_ENABLED` 宏方便启用/禁用
- **getter函数**: 提供 `lv_port_indev_get_touchpad()` 获取设备指针

## 📦 集成步骤

### 第1步: 替换文件
```bash
# 在你的项目中替换以下文件:
Core/BSP/touch.h          → 使用新的 touch.h
Core/BSP/touch.c          → 使用新的 touch.c
Core/APPS/LVGL/lv_port_indev.h → 使用新的 lv_port_indev.h
Core/APPS/LVGL/lv_port_indev.c → 使用新的 lv_port_indev.c
```

### 第2步: 配置参数

#### lv_port_indev.c 中需要调整:
```c
/* 根据你的屏幕修改分辨率 */
#define DISP_HOR_RES  800  // 你的屏幕宽度
#define DISP_VER_RES  480  // 你的屏幕高度

/* 根据屏幕方向调整坐标转换 */
#define TOUCH_SWAP_XY        0  // 横竖屏切换时设为1
#define TOUCH_INVERT_X       0  // X轴镜像
#define TOUCH_INVERT_Y       0  // Y轴镜像
```

#### touch.c 中确认I2C句柄:
```c
// 确保使用正确的I2C外设
HAL_I2C_Master_Transmit(&hi2c2, ...);  // 如果你用的是I2C1,改为hi2c1
```

### 第3步: main.c 初始化顺序
```c
int main(void)
{
    /* HAL初始化 */
    HAL_Init();
    SystemClock_Config();
    
    /* 外设初始化 */
    MX_GPIO_Init();
    MX_I2C2_Init();     // 确保I2C已初始化
    MX_LTDC_Init();     // 显示初始化
    
    /* LVGL初始化 */
    lv_init();
    lv_port_disp_init();     // 显示端口初始化
    lv_port_indev_init();    // ← 输入设备初始化
    
    /* 创建UI */
    // lv_demo_widgets();
    // ui_screen_launcher();  // 你的UI
    
    while (1) {
        lv_timer_handler();  // LVGL任务处理
        HAL_Delay(5);
    }
}
```

### 第4步: 中断处理(可选,提升响应速度)

如果你想使用中断方式而不是轮询,在 `stm32h7xx_it.c` 中:

```c
/* EXTI中断处理 */
void EXTI15_10_IRQHandler(void)  // 根据你的INT引脚调整
{
    HAL_GPIO_EXTI_IRQHandler(TOUCH_INT_Pin);
}

/* EXTI回调 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == TOUCH_INT_Pin) {
        // 触摸中断发生,可以设置标志位
        // touch_irq_flag = 1;
    }
}
```

## 🎯 还需要优化的部分

### 1. 中断模式实现
**当前**: 使用轮询模式 (在 `lv_timer_handler` 中调用)  
**建议**: 实现中断触发读取,降低CPU占用

```c
// 在lv_port_indev.c中添加:
static volatile bool touch_irq_pending = false;

void TOUCH_IRQ_Handler(void) {
    touch_irq_pending = true;
}

static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data) {
    if (touch_irq_pending) {
        touch_irq_pending = false;
        GT911_Scan();
    }
    // 其余代码...
}
```

### 2. 多点触摸支持
**当前**: 只使用第一个触摸点  
**建议**: 如果需要手势识别,扩展为多点模式

```c
// 在touchpad_read中处理多点:
uint8_t touch_num = GT911_Get_TouchNum();
for (uint8_t i = 0; i < touch_num; i++) {
    // 处理每个触摸点
}
```

### 3. 性能优化

#### I2C速度优化
在 `i2c.c` 的MX_I2C2_Init()中:
```c
hi2c2.Init.Timing = 0x00702991;  // 400kHz
// 或者
hi2c2.Init.Timing = 0x00300619;  // 1MHz (如果GT911支持)
```

#### DMA传输(高级优化)
```c
// 使用DMA进行I2C传输,降低CPU占用
HAL_I2C_Master_Transmit_DMA(&hi2c2, ...);
HAL_I2C_Master_Receive_DMA(&hi2c2, ...);
```

### 4. 功耗优化

如果设备需要低功耗模式:
```c
// 添加GT911休眠/唤醒功能
void GT911_Sleep(void);
void GT911_Wakeup(void);

// 在无触摸时进入低功耗
if (no_touch_for_long_time) {
    GT911_Sleep();
}
```

### 5. 校准功能

添加触摸屏校准:
```c
typedef struct {
    int16_t offset_x;
    int16_t offset_y;
    float   scale_x;
    float   scale_y;
} TouchCalibration_t;

void Touch_Calibrate(void);
void Touch_Apply_Calibration(int16_t *x, int16_t *y);
```

### 6. 错误处理增强

```c
// 添加重试机制
#define I2C_RETRY_COUNT  3

static bool GT911_I2C_Read_Retry(uint16_t reg, uint8_t *buf, uint16_t len) {
    for (int i = 0; i < I2C_RETRY_COUNT; i++) {
        if (GT911_I2C_Read(reg, buf, len)) {
            return true;
        }
        HAL_Delay(1);
    }
    return false;
}
```

### 7. 调试接口

```c
// 添加调试信息输出
void GT911_Debug_Print(void) {
    uint8_t product_id[4];
    uint16_t fw_version;
    
    if (GT911_Read_ProductID(product_id)) {
        printf("Product ID: %c%c%c\n", 
               product_id[0], product_id[1], product_id[2]);
    }
    
    if (GT911_Read_FirmwareVersion(&fw_version)) {
        printf("FW Version: 0x%04X\n", fw_version);
    }
}
```

## 📁 建议的文件组织

```
Core/
├── BSP/                    # 板级支持包
│   ├── GT911/              # GT911触摸驱动
│   │   ├── touch.h
│   │   └── touch.c
│   └── LCD/                # 显示驱动
│       └── ...
├── APPS/
│   └── LVGL/
│       ├── lvgl/           # LVGL库
│       ├── lv_port_disp.c  # 显示适配
│       ├── lv_port_indev.c # 输入设备适配 ←
│       └── lv_conf.h       # LVGL配置
└── Screen/                 # UI界面
    └── ...
```

## ✅ 验证测试

### 1. 硬件测试
```c
void Test_GT911_Hardware(void) {
    uint8_t id[4];
    if (GT911_Read_ProductID(id)) {
        printf("✓ GT911 detected: %c%c%c\n", id[0], id[1], id[2]);
    } else {
        printf("✗ GT911 not found!\n");
    }
}
```

### 2. 触摸测试
```c
void Test_Touch_Response(void) {
    if (GT911_Scan()) {
        uint16_t x, y;
        if (GT911_Get_Point(0, &x, &y)) {
            printf("Touch at (%d, %d)\n", x, y);
        }
    }
}
```

### 3. LVGL集成测试
在LVGL中显示坐标:
```c
lv_obj_t *label = lv_label_create(lv_screen_active());
lv_label_set_text_fmt(label, "X:%d Y:%d", x, y);
```

## 🐛 常见问题排查

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| 无触摸响应 | I2C未初始化 | 检查`MX_I2C2_Init()`是否调用 |
| 坐标不准 | 分辨率不匹配 | 调整`DISP_HOR_RES`和`DISP_VER_RES` |
| 坐标颠倒 | XY未交换 | 设置`TOUCH_SWAP_XY = 1` |
| 镜像错误 | 屏幕方向 | 调整`TOUCH_INVERT_X/Y` |
| I2C通信失败 | 地址错误 | 确认`GT911_I2C_ADDR = 0xBA` |
| 读取ProductID失败 | 复位时序 | 检查RST和INT引脚配置 |

## 📚 参考资料

- [GT911 数据手册](https://github.com/goodix)
- [LVGL 9.5 文档](https://docs.lvgl.io/9.5/)
- [LVGL 输入设备](https://docs.lvgl.io/9.5/main-modules/indev/index.html)
- STM32H7 HAL库文档

## 📝 版本历史

- v2.1 - LVGL 9.5兼容版本,完全重构
- v1.0 - 原始版本(包含中文乱码)
