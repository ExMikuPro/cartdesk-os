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

`self` 是每个脚本实例的私有状态表。UI 放在 `self.children`，脚本自己的计数器、业务状态等放在 `self.state`。`start()` 仍保留兼容，但新代码推荐使用 `init(self)`、`update(self, dt)` 和 `final(self)`。

当前宿主实现里，`dt` 单位为秒：`update`/`late_update` 接收本帧耗时秒数，`fixed_update` 接收固定步长秒数。

输入事件统一走 `on_input(self, action_id, action)`。`action.event` 表示宿主事件名，例如按钮的 `"pressed"`、`"released"`、`"clicked"`，以及滑块的 `"changed"`；`action` 还可包含 `pressed`、`released`、`repeated`、`value`、`x`、`y`、`dx`、`dy`。UI 按钮和滑块通过 config 的 `input` 字段设置脚本侧业务名。

示例：

```lua
function init(self)
    self.state = {
        elapsed = 0,
        ticks = 0,
    }
end

function update(self, dt)
    local s = self.state

    s.elapsed = s.elapsed + dt

    if s.elapsed >= 1.0 then
        s.elapsed = s.elapsed - 1.0
        s.ticks = s.ticks + 1
        print("tick", s.ticks)
    end
end

function final(self)
end
```

## self 使用规范

`self` 顶层只推荐使用：

```lua
self.state
self.children
```

`self.children` 用于 UI / Drawable 树，由宿主管理生命周期。`self.state` 用于脚本跨帧状态。硬件资源句柄如果必须跨帧保存，放入 `self.state.resources`。固定 pin 和常量使用文件级 `local`，临时变量使用函数内 `local`。

不推荐把以下字段直接挂到 `self` 顶层：

```lua
self.elapsed
self.count
self.button
self.slider
self.label
self.file
self.log
self.led
self.pwm
self.timer
```

推荐分类：

| 内容 | 推荐位置 |
| --- | --- |
| UI 对象 | `self.children` |
| 跨帧业务状态 | `self.state.xxx` |
| 跨帧硬件资源句柄 | `self.state.resources.xxx` |
| 固定 pin / 常量 / 配置 | 文件级 `local` |
| 临时变量 | 函数内 `local` |
| 一次性硬件调用结果 | 函数内 `local` |

## API 风格契约

- 模块名使用小写名词，例如 `gpio`、`pwm`、`tim`、`rng`、`crc`。
- 新函数名使用 snake_case，例如 `pwm.set_freq()`、`rng.u32()`、`crc.crc32_hex()`。
- 常量使用全大写，例如 `gpio.HIGH`、`gpio.LOW`、`pwm.DEFAULT_FREQ`。
- 查询类 API 成功返回值，失败返回 `nil, err`。
- 修改类 API 成功返回 `true`，失败返回 `nil, err`。
- 参数错误可以抛 Lua error。
- 运行时失败返回 `nil, err`。
- 旧非 UI API 仅作为兼容别名保留，新脚本优先使用推荐 API。旧 UI API 不保留兼容层。
- Lua 层隐藏 STM32 板级细节，不暴露 HAL handle、寄存器、timer/channel 等对象。
- 非 UI 资源应在 `final(self)` 中释放。UI 资源由宿主管理 `self.children` 生命周期。

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
    self.state = {
        ready = false,
    }
    delay(100)
    self.state.ready = true
end
```

## 硬件外设与 self.state

GPIO、PWM、TIM、RNG、CRC 不属于 `self.children`。`self.children` 只放 UI / Drawable。

硬件外设使用规则：

| 内容 | 推荐位置 |
| --- | --- |
| 固定硬件编号 | 文件级 `local` |
| 固定配置常量 | 文件级 `local` |
| 临时硬件读数 | 函数内 `local` |
| 跨帧业务状态 | `self.state.xxx` |
| 跨帧资源句柄 | `self.state.resources.xxx` |
| 资源释放 | `final(self)` |

不要新增 `self.hardware`、`self.resources`、`self.gpio`、`self.pwm` 等顶层字段。只有真正需要跨帧保存资源句柄时才创建 `self.state.resources`。

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
local BLINK_INTERVAL = 0.5

function init(self)
    self.state = {
        elapsed = 0,
        level = gpio.LOW,
    }

    gpio.setup(LED_PIN, {
        mode = gpio.OUTPUT,
        initial = self.state.level,
        speed = gpio.SPEED_LOW,
    })
end

function update(self, dt)
    local s = self.state

    s.elapsed = s.elapsed + dt

    if s.elapsed >= BLINK_INTERVAL then
        s.elapsed = s.elapsed - BLINK_INTERVAL
        s.level = s.level == gpio.LOW and gpio.HIGH or gpio.LOW
        gpio.write(LED_PIN, s.level)
    end
end

function final(self)
    gpio.write(LED_PIN, gpio.LOW)
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
local STEP_INTERVAL = 0.02

function init(self)
    self.state = {
        elapsed = 0,
        duty = pwm.MIN,
        direction = 1,
    }

    pwm.setup(PWM_PIN, {
        freq = pwm.DEFAULT_FREQ,
        duty = self.state.duty,
        start = true,
    })
end

function update(self, dt)
    local s = self.state

    s.elapsed = s.elapsed + dt

    while s.elapsed >= STEP_INTERVAL do
        s.elapsed = s.elapsed - STEP_INTERVAL
        s.duty = s.duty + s.direction

        if s.duty >= pwm.MAX then
            s.duty = pwm.MAX
            s.direction = -1
        elseif s.duty <= pwm.MIN then
            s.duty = pwm.MIN
            s.direction = 1
        end
    end

    pwm.write(PWM_PIN, s.duty)
end

function final(self)
    pwm.stop(PWM_PIN)
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

## UI Children

`self.children` 是宿主管理的 UI / Drawable 树，可以是单个 Drawable，也可以是 Drawable 数组。宿主会在 `final(self)` 返回后自动递归删除 `self.children`，并清空该字段。

脚本自己的状态建议放在 `self.state`。不推荐把 UI 句柄挂到 `self.button`、`self.slider`、`self.label` 等顶层字段。

通用 config 字段：

| 字段 | 说明 |
| --- | --- |
| `id` | UI 查询和 patch 使用的字符串 ID |
| `x` / `y` / `w` / `h` | 坐标和尺寸；显式字段优先于 `rect`、`pos`、`size` |
| `rect = { x, y, w, h }` | 一次设置坐标和尺寸 |
| `pos = { x, y }` | 一次设置坐标 |
| `size = { w, h }` | 一次设置尺寸 |
| `hidden` | 设置或清除隐藏状态 |
| `input` | 投递到 `on_input(self, action_id, action)` 的动作 ID |
| `style` | 控件样式表 |

### 单按钮

```lua
function init(self)
    self.children = ui.button({
        id = "run",
        text = "Run",
        rect = { 24, 24, 120, 48 },
        input = "run",
        style = {
            bg = 0x2D8CFF,
            text = 0xFFFFFF,
            radius = 8,
        },
    })
end

function on_input(self, action_id, action)
    if action_id == "run" then
        print("run", action.event)
    end
end
```

### 按钮 + 滑块

```lua
function init(self)
    self.state = {
        volume = 128,
    }

    self.children = {
        ui.button({
            id = "run",
            text = "Run",
            rect = { 24, 24, 120, 48 },
            input = "run",
        }),

        ui.slider({
            id = "volume",
            rect = { 24, 90, 220, 24 },
            range = { 0, 255 },
            value = self.state.volume,
            input = "volume",
        }),
    }
end

function on_input(self, action_id, action)
    if action_id == "volume" and action.event == "changed" then
        self.state.volume = action.value
        print("volume", self.state.volume)
    end
end
```

### 通过 id 更新 UI

```lua
function on_input(self, action_id, action)
    if action_id == "run" and action.event == "clicked" then
        ui.patch(self, "run", {
            text = "Running",
            style = {
                bg = 0x00AA00,
            },
        })
    end
end
```

### API

| API | 说明 |
| --- | --- |
| `ui.button(config)` | 创建按钮 Drawable |
| `ui.slider(config)` | 创建滑块 Drawable |
| `ui.find(self, id)` | 在 `self.children` 中递归查找 Drawable；找不到返回 `nil` |
| `ui.patch(self, id, patch)` | 用 config 同名字段更新 Drawable；找不到返回 `nil, "ui id not found"` |

Button config 支持 `text`、`input`，以及 `style.bg`、`style.bg_alpha`、`style.text`、`style.border.color`、`style.border.width`、`style.radius`。

Slider config 支持 `range = { min, max }`、`value`、`input`，以及 `style.bg`、`style.bg_alpha`、`style.indicator`、`style.indicator_alpha`、`style.knob`、`style.knob_alpha`、`style.border.color`、`style.border.width`、`style.radius`。

按钮事件为 `"pressed"`、`"released"`、`"clicked"`。滑块事件为 `"changed"`，`action.value` 为当前滑块值。

`final(self)` 中通常不需要删除 UI：

```lua
function final(self)
    -- UI children are deleted by the host after final(self).
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

脚本示例和应用脚本不要依赖未打开的标准库。Lua 侧不提供文件访问 API。

## 暂未实现与兼容说明

- `start()` 保留旧脚本兼容，新脚本推荐 `init/update/final`。
- `gpio.on`、`gpio.off` 当前未实现 GPIO 中断，只检查参数后返回 `nil, err`。
- `gpio.ANALOG` 当前只是接口层常量。
- ADC 本轮未实现。
- GPIO interrupt 本轮未实现。
