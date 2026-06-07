# Lua 脚本 API

本文档面向掌机脚本作者，描述当前 Lua VM 暴露给脚本的 API。示例代码默认运行在受限 Lua 环境中，不依赖 `io`、`os`、`debug` 或 `package`。

## 生命周期

推荐新脚本实现以下生命周期函数：

```lua
function init(self)
end

function fixed_update(self, dt)
end

function update(self, dt)
end

function late_update(self, dt)
end

function final(self)
end
```

可用生命周期函数如下：

| 函数 | 触发时机 |
| --- | --- |
| `init(self)` | 脚本实例创建并加载完成后调用一次 |
| `fixed_update(self, dt)` | 固定步长更新 |
| `update(self, dt)` | 常规帧更新 |
| `late_update(self, dt)` | 常规帧更新之后 |
| `on_input(self, action_id, action)` | 输入事件触发 |
| `on_message(self, message_id, message, sender)` | 消息事件触发 |
| `on_reload(self)` | 热重载时触发 |
| `final(self)` | 脚本销毁或 VM 关闭前清理 |
| `start()` | 兼容旧脚本的入口 |

`self` 是每个脚本实例的私有状态表，推荐把计数器、句柄、UI 对象等状态保存在 `self` 上。`start()` 仍保留兼容，但新代码推荐使用 `init(self)`、`update(self, dt)` 和 `final(self)`。

当前宿主实现里，`dt` 单位为秒：`update`/`late_update` 接收本帧耗时秒数，`fixed_update` 接收固定步长秒数。

输入事件统一走 `on_input(self, action_id, action)`。`action.event` 表示宿主事件名，例如 `"pressed"`、`"released"`、`"clicked"`、`"long_pressed"`、`"value_changed"`；`action` 还可包含 `pressed`、`released`、`repeated`、`value`、`x`、`y`、`dx`、`dy`。UI 按钮和滑块默认 `action_id` 为 `"button"` / `"slider"`，可通过控件的 `set_input_id()` 改成脚本自己的业务名。

示例：

```lua
function init(self)
    self.elapsed = 0
end

function update(self, dt)
    self.elapsed = self.elapsed + dt
end

function final(self)
    print("elapsed", self.elapsed)
end
```

## API 风格契约

- 模块名使用小写名词，例如 `gpio`、`pwm`、`tim`、`sd`、`rng`、`crc`。
- 新函数名使用 snake_case，例如 `pwm.set_freq()`、`rng.u32()`、`crc.crc32_hex()`。
- 常量使用全大写，例如 `gpio.HIGH`、`gpio.LOW`、`pwm.DEFAULT_FREQ`。
- 查询类 API 成功返回值，失败返回 `nil, err`。
- 修改类 API 成功返回 `true`，失败返回 `nil, err`。
- 参数错误可以抛 Lua error。
- 运行时失败返回 `nil, err`；SD 旧接口仍有部分历史行为会抛 Lua error，未来建议统一。
- 旧 API 仅作为兼容别名保留，新脚本优先使用推荐 API。
- Lua 层隐藏 STM32 板级细节，不暴露 HAL handle、寄存器、timer/channel 等对象。
- 资源应在 `final(self)` 中释放。

## 时间

全局时间函数：

| API | 说明 |
| --- | --- |
| `delay(ms)` | 当前协程延迟 `ms` 毫秒。当前实现为可调用表 |
| `delay.ms(ms)` | `delay(ms)` 的模块风格别名 |

`delay` 会让当前脚本执行流等待，不建议在帧更新里频繁使用长延迟。周期逻辑优先使用 `update(self, dt)` 累计时间。

示例：

```lua
function init(self)
    self.ready = false
    delay(100)
    self.ready = true
end
```

## GPIO

全局 Arduino 风格别名：

| API | 说明 |
| --- | --- |
| `pinMode(pin, mode)` | 设置 GPIO 模式 |
| `digitalRead(pin)` | 读取 GPIO 电平 |
| `digitalWrite(pin, value)` | 写 GPIO 电平 |

`gpio` 模块：

| API | 说明 |
| --- | --- |
| `gpio.count()` | 返回可用 GPIO 数量 |
| `gpio.list()` | 返回 GPIO 列表 |
| `gpio.info(pin)` | 返回指定 GPIO 信息 |
| `gpio.setup(pin, config)` | 设置 GPIO，`config` 可为模式常量或配置表 |
| `gpio.read(pin)` | 读取电平 |
| `gpio.write(pin, value)` | 写电平 |
| `gpio.toggle(pin)` | 翻转输出电平 |
| `gpio.release(pin)` | 释放 GPIO |
| `gpio.pinMode(pin, mode)` | `pinMode` 的模块别名 |
| `gpio.digitalRead(pin)` | `digitalRead` 的模块别名 |
| `gpio.digitalWrite(pin, value)` | `digitalWrite` 的模块别名 |
| `gpio.on(pin, edge, callback)` | 当前未实现中断，返回 `nil, err` |
| `gpio.off(pin)` | 当前未实现中断，返回 `nil, err` |

`gpio.setup(pin, config)` 的配置表至少需要 `mode`：

```lua
gpio.setup(3, {
    mode = gpio.OUTPUT,
    initial = gpio.LOW,
    speed = gpio.SPEED_LOW,
})
```

GPIO 常量：

| 常量 | 说明 |
| --- | --- |
| `gpio.LOW` / `gpio.HIGH` | 低/高电平 |
| `gpio.OUTPUT` | 推挽输出 |
| `gpio.INPUT` | 输入 |
| `gpio.INPUT_PULLUP` | 上拉输入 |
| `gpio.INPUT_PULLDOWN` | 下拉输入 |
| `gpio.OUTPUT_OPEN_DRAIN` | 开漏输出 |
| `gpio.ANALOG` | 当前只是接口层常量，本轮未实现 ADC |
| `gpio.RISING` / `gpio.FALLING` / `gpio.CHANGE` | 中断边沿常量，当前中断未实现 |
| `gpio.LOW_LEVEL` / `gpio.HIGH_LEVEL` | 电平触发常量，当前中断未实现 |
| `gpio.SPEED_LOW` / `gpio.SPEED_MEDIUM` / `gpio.SPEED_HIGH` | GPIO 速度 |

注意：`gpio.ANALOG` 当前只是接口层常量；ADC 本轮未实现；GPIO 中断本轮未实现。

示例：

```lua
local LED_PIN = 3

function init(self)
    gpio.setup(LED_PIN, { mode = gpio.OUTPUT, initial = gpio.LOW })
end

function update(self, dt)
    self.elapsed = (self.elapsed or 0) + dt
    if self.elapsed >= 0.5 then
        self.elapsed = self.elapsed - 0.5
        gpio.toggle(LED_PIN)
    end
end

function final(self)
    gpio.release(LED_PIN)
end
```

## PWM

`pwm` 模块：

| API | 说明 |
| --- | --- |
| `pwm.count()` | 返回可用 PWM 通道数量 |
| `pwm.list()` | 返回 PWM 通道列表 |
| `pwm.info(pin)` | 返回指定 PWM 信息 |
| `pwm.setup(pin, config)` | 设置 PWM，`config` 可为频率数字或配置表 |
| `pwm.write(pin, duty)` | 写占空比 |
| `pwm.read(pin)` | 读占空比 |
| `pwm.set_freq(pin, freq)` | 设置频率 |
| `pwm.get_freq(pin)` | 读取频率 |
| `pwm.stop(pin)` | 停止输出 |
| `pwm.release(pin)` | 释放 PWM |
| `pwm.setFreq(pin, freq)` | legacy 兼容别名，等同 `pwm.set_freq` |
| `pwm.getFreq(pin)` | legacy 兼容别名，等同 `pwm.get_freq` |

`pwm.setup(pin, config)` 支持：

```lua
pwm.setup(0, pwm.DEFAULT_FREQ)
pwm.setup(0, { freq = pwm.DEFAULT_FREQ, duty = 0, start = true })
```

PWM 常量：

| 常量 | 说明 |
| --- | --- |
| `pwm.MIN` | 最小占空比 |
| `pwm.MAX` | 最大占空比 |
| `pwm.DEFAULT_FREQ` | 默认频率 |
| `pwm.POLARITY_HIGH` | 高有效极性常量 |
| `pwm.POLARITY_LOW` | 低有效极性常量 |

示例：

```lua
local PWM_PIN = 0

function init(self)
    self.duty = pwm.MIN
    pwm.setup(PWM_PIN, { freq = pwm.DEFAULT_FREQ, duty = self.duty, start = true })
end

function update(self, dt)
    self.duty = math.min(pwm.MAX, self.duty + 1)
    pwm.write(PWM_PIN, self.duty)
end

function final(self)
    pwm.release(PWM_PIN)
end
```

## RNG

`rng` 模块使用 STM32 RNG 硬件。`MX_RNG_Init()` 需要在 Lua VM 注册前完成，当前工程初始化顺序已经满足。

| API | 说明 |
| --- | --- |
| `rng.u32()` | 返回 `0..0xFFFFFFFF` 的整数，硬件失败返回 `nil, err` |
| `rng.bytes(n)` | 返回长度为 `n` 的 Lua string，`n` 上限为 4096 |

示例：

```lua
function init(self)
    local n, err = rng.u32()
    if not n then
        print("rng failed", err)
        return
    end

    local bytes, err = rng.bytes(16)
    if not bytes then
        print("rng bytes failed", err)
        return
    end

    print("rng", n, #bytes)
end
```

## CRC

`crc` 模块使用 STM32 CRC 外设计算标准 IEEE CRC32。当前参数模式为多项式 `0x04C11DB7`、初始值 `0xFFFFFFFF`、输入按字节反转、输出反转、最终异或 `0xFFFFFFFF`。`crc.crc32_hex("hello")` 应返回 `"3610A686"`。

| API | 说明 |
| --- | --- |
| `crc.crc32(data)` | `data` 为 Lua string，成功返回 CRC32 数值，硬件失败返回 `nil, err` |
| `crc.crc32_hex(data)` | 成功返回 8 位大写十六进制字符串 |

示例：

```lua
function init(self)
    local h, err = crc.crc32_hex("hello")
    if not h then
        print("crc failed", err)
        return
    end

    print("crc32", h)
end
```

## TIM

`tim` 模块：

| API | 说明 |
| --- | --- |
| `tim.us()` | 返回微秒计数 |
| `tim.delay_us(us)` | 微秒级延迟 |

示例：

```lua
function init(self)
    self.start_us = tim.us()
end

function update(self, dt)
    self.now_us = tim.us()
end
```

## SD

`sd` 模块：

| API | 说明 |
| --- | --- |
| `sd.open(path, mode)` | 打开文件 |
| `sd.close(f)` | 关闭文件 |
| `sd.write(f, data)` | 写入数据 |
| `sd.wirte(f, data)` | 拼写错误兼容别名，不推荐新代码使用 |
| `sd.read(f, n)` | 读取最多 `n` 字节 |
| `sd.seek(f, pos)` | 移动文件位置 |
| `sd.size(f)` | 返回文件大小 |
| `sd.mount()` | 挂载 SD 文件系统 |
| `sd.umount()` | 卸载 SD 文件系统 |

文件对象方法：

| API | 说明 |
| --- | --- |
| `f:close()` | 关闭文件 |
| `f:write(data)` | 写入数据 |
| `f:wirte(data)` | 拼写错误兼容别名，不推荐新代码使用 |
| `f:read(n)` | 读取最多 `n` 字节 |
| `f:seek(pos)` | 移动文件位置 |
| `f:size()` | 返回文件大小 |

`sd.wirte` 和 `f:wirte` 是历史拼写错误兼容别名，只为旧脚本保留；新代码使用 `write`。

当前实现中，部分 SD 错误会直接抛出 Lua 错误。需要处理失败时，可以使用 `pcall`：

```lua
function init(self)
    local ok, err = pcall(function()
        sd.mount()
        local f = sd.open("/hello.txt", "w+")
        f:write("hello\n")
        f:seek(0)
        print(f:read(f:size()))
        f:close()
        sd.umount()
    end)

    if not ok then
        print("sd failed", err)
    end
end
```

## UI Button

`ui.button` 模块：

| API | 说明 |
| --- | --- |
| `ui.button.create(parent)` | 创建按钮 |
| `ui.button.draw(parent, x, y, width, height, text)` | 创建并设置位置、大小、文本 |
| `ui.button.get_screen()` | 获取当前屏幕对象 |
| `btn:set_text(text)` | 设置文本 |
| `btn:set_size(width, height)` | 设置尺寸 |
| `btn:set_pos(x, y)` | 设置位置 |
| `btn:align(align, x_offset, y_offset)` | 对齐 |
| `btn:set_style_bg_color(color, alpha)` | 设置背景色和透明度 |
| `btn:set_style_text_color(color)` | 设置文本颜色 |
| `btn:set_style_border(color, width)` | 设置边框。当前实现参数顺序为 `color, width` |
| `btn:set_style_radius(radius)` | 设置圆角 |
| `btn:add_flag(flag)` | 添加 LVGL 标志字符串 |
| `btn:clear_flag(flag)` | 清除 LVGL 标志字符串 |
| `btn:set_checkable(enable)` | 设置是否可选中 |
| `btn:is_checked()` | 返回是否选中 |
| `btn:set_input_id(action_id)` | 设置投递到 `on_input` 的动作 ID |
| `btn:set_callback(function(btn, event) end)` | legacy 兼容事件回调，新脚本优先使用 `on_input` |
| `btn:delete()` | 删除按钮 |

常用 `align` 字符串包括 `center`、`top_left`、`top_mid`、`top_right`、`bottom_left`、`bottom_mid`、`bottom_right`、`left_mid`、`right_mid`。常用 `flag` 字符串包括 `hidden`、`clickable`、`checkable`、`scrollable`、`press_lock`。

示例：

```lua
function init(self)
    local screen = ui.button.get_screen()
    self.btn = ui.button.create(screen)
    self.btn:set_text("Start")
    self.btn:set_pos(20, 20)
    self.btn:set_size(120, 48)
    self.btn:set_style_bg_color(0x2D8CFF, 255)
    self.btn:set_style_text_color(0xFFFFFF)
    self.btn:set_style_radius(8)
    self.btn:set_input_id("start")
end

function on_input(self, action_id, action)
    if action_id == "start" and action.event == "clicked" then
        print("button clicked")
    end
end

function final(self)
    if self.btn then
        self.btn:delete()
        self.btn = nil
    end
end
```

## UI Slider

`ui.slider` 模块：

| API | 说明 |
| --- | --- |
| `ui.slider.create(parent)` | 创建滑块 |
| `ui.slider.draw(parent, x, y, width, height)` | 创建并设置位置、大小 |
| `ui.slider.get_screen()` | 获取当前屏幕对象 |
| `slider:set_size(width, height)` | 设置尺寸 |
| `slider:set_pos(x, y)` | 设置位置 |
| `slider:align(align, x_offset, y_offset)` | 对齐 |
| `slider:set_value(value)` | 设置当前值 |
| `slider:get_value()` | 读取当前值 |
| `slider:set_range(min, max)` | 设置范围 |
| `slider:set_style_bg_color(color, alpha)` | 设置背景色和透明度 |
| `slider:set_style_indicator_color(color, alpha)` | 设置进度条颜色和透明度 |
| `slider:set_style_knob_color(color, alpha)` | 设置旋钮颜色和透明度 |
| `slider:set_style_border(color, width)` | 设置边框。当前实现参数顺序为 `color, width` |
| `slider:set_style_radius(radius)` | 设置圆角 |
| `slider:set_input_id(action_id)` | 设置投递到 `on_input` 的动作 ID |
| `slider:set_callback(function(slider, event) end)` | legacy 兼容事件回调，新脚本优先使用 `on_input` |
| `slider:delete()` | 删除滑块 |

示例：

```lua
function init(self)
    local screen = ui.slider.get_screen()
    self.slider = ui.slider.draw(screen, 20, 90, 220, 24)
    self.slider:set_range(0, pwm.MAX)
    self.slider:set_value(0)
    self.slider:set_input_id("duty")
end

function on_input(self, action_id, action)
    if action_id == "duty" and action.event == "value_changed" then
        print("slider", action.value)
    end
end

function final(self)
    if self.slider then
        self.slider:delete()
        self.slider = nil
    end
end
```

## Lua 标准库

当前打开：

- `_G` / base
- `coroutine`
- `table`
- `string`
- `math`
- `utf8`

当前未打开：

- `io`
- `os`
- `debug`
- `package`

脚本示例和应用脚本不要依赖未打开的标准库。文件访问请使用 `sd` 模块。

## 暂未实现与兼容说明

- `start()` 保留旧脚本兼容，新脚本推荐 `init/update/final`。
- `gpio.on`、`gpio.off` 当前未实现 GPIO 中断，只检查参数后返回 `nil, err`。
- `gpio.ANALOG` 当前只是接口层常量。
- ADC 本轮未实现。
- GPIO interrupt 本轮未实现。
- `sd.wirte`、`f:wirte` 是拼写错误兼容别名，不推荐新代码使用。
