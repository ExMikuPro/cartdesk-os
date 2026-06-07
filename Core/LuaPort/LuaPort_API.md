# LuaPort Lua 端 API 文档

本文档按当前工程源码整理，覆盖 Lua 脚本侧能直接使用的接口。

相关源码：

- `Core/Src/lua_vm.c`：Lua 运行时、脚本实例和 Defold 风格生命周期调度。
- `Core/Inc/lua_vm.h`：运行时对外 C 接口。
- `Core/LuaPort/lua_port.c`：把底层模块绑定到 Lua 全局环境。
- `Core/LuaPort/modules/lua_gpio.c`：GPIO 模块。
- `Core/LuaPort/modules/lua_pwm.c`：PWM 模块。
- `Core/LuaPort/modules/lua_tim.c`：微秒计时与微秒延时。
- `Core/LuaPort/modules/lua_delay.c`：毫秒级协程延时。
- `Core/LuaPort/modules/lua_rng.c`：STM32 RNG 硬件随机数接口。
- `Core/LuaPort/modules/lua_crc.c`：STM32 CRC 硬件 CRC32 接口。
- `Core/LuaPort/modules/lua_ui_button.c`：LVGL 按钮接口。
- `Core/LuaPort/modules/lua_ui_slider.c`：LVGL 滑块接口。

## 1. 运行模型

### 1.1 脚本入口

启动脚本可以按需定义以下函数：

```lua
function init(self) end
function final(self) end
function fixed_update(self, dt) end
function update(self, dt) end
function late_update(self, dt) end
function on_message(self, message_id, message, sender) end
function on_input(self, action_id, action) end
function on_reload(self) end
```

`lua_init()` 会创建 Lua 虚拟机，绑定 LuaPort 模块，加载脚本实例，然后调用一次 `init(self)`。

`lua_update_task()` 需要在主循环或任务里反复调用。每帧依次处理输入、固定更新、普通更新、后置更新和消息。

每个脚本实例拥有独立的环境和 `self` 表。详细行为见
`Docs/lua/lua_lifecycle.md`。

默认调度参数：

| 宏 | 默认值 | 说明 |
| --- | ---: | --- |
| `LUA_RT_PERIOD_MS` | `10` | `update(dt)` 的最小调用间隔，单位 ms |
| `LUA_RT_MAX_DT_MS` | `100` | `dt` 最大钳制值，避免长时间卡顿后传入过大的 dt |

### 1.2 弱符号 Hook

以下函数在 `Core/Src/lua_vm.c` 中是 weak 实现，可在工程中提供同名函数覆盖：

| 函数 | 默认行为 | 用途 |
| --- | --- | --- |
| `lua_rt_time_ms()` | 返回 `HAL_GetTick()` | 给 `lua_update_task()` 提供 ms 时间基准 |
| `lua_rt_log(const char *s)` | 空实现 | 输出 Lua 加载、运行错误 |
| `lua_get_boot_script(size_t *out_len)` | 返回内置测试脚本 | 提供启动脚本内容 |

### 1.3 标准库状态

当前 `lua_init()` 会打开一组适合嵌入式运行的轻量标准库：

| 库 | 说明 |
| --- | --- |
| `_G` / base | `print`、`type`、`pairs` 等基础函数 |
| `coroutine` | 协程基础能力 |
| `table` | 表处理函数 |
| `string` | 字符串处理函数 |
| `math` | 数学函数 |
| `utf8` | UTF-8 辅助函数 |

`io`、`os`、`package`、`debug` 默认不打开。当前脚本侧稳定可用的是这些轻量标准库，以及 `lua_port_bind()` 注册的全局对象和函数。

## 2. 全局对象总览

`lua_port_bind()` 当前会注册这些 Lua 全局接口：

| Lua 名称 | 类型 | 来源 | 说明 |
| --- | --- | --- | --- |
| `gpio` | table | `lua_gpio.c` | GPIO 读写、翻转 |
| `pwm` | table | `lua_pwm.c` | PWM 配置、占空比和频率控制 |
| `tim` | table | `lua_tim.c` | DWT 微秒计时、微秒忙等 |
| `rng` | table | `lua_rng.c` | STM32 RNG 硬件随机数 |
| `crc` | table | `lua_crc.c` | 标准 IEEE CRC32 |
| `ui` | table | `lua_ui_button.c` / `lua_ui_slider.c` | LVGL UI 命名空间 |
| `ui.button` | table | `lua_ui_button.c` | 按钮创建和控制 |
| `ui.slider` | table | `lua_ui_slider.c` | 滑块创建和控制 |
| `delay` | function | `lua_delay.c` | 毫秒级协程延时 |
| `print` | function | Lua base lib | 调试输出函数，输出去向由运行时日志实现决定 |

## API 风格契约

- 模块名使用小写名词，例如 `gpio`、`pwm`、`tim`、`rng`、`crc`。
- 新函数名使用 snake_case，例如 `pwm.set_freq()`、`rng.u32()`、`crc.crc32_hex()`。
- 常量使用全大写，例如 `gpio.HIGH`、`gpio.LOW`、`pwm.DEFAULT_FREQ`。
- 查询类 API 成功时直接返回值，失败时返回 `nil, err`。
- 修改类 API 成功时返回 `true`，失败时返回 `nil, err`。
- 参数数量或类型错误属于脚本错误，可以抛 Lua error。
- 硬件失败、资源冲突、外设不可用等运行时错误应返回 `nil, err`。
- 旧 API 不再保留兼容层，新脚本应使用文档化的当前 API。
- Lua 层隐藏 STM32 端口、pin mask、timer、channel、寄存器和 HAL handle 等板级细节。
- 脚本申请的非 UI 资源应在 `final(self)` 中释放，例如 `gpio.release()`、`pwm.release()`。UI 放在 `self.children`，由宿主在 `final(self)` 返回后自动递归删除。

## 3. Lua 真值约定

Lua 里只有 `false` 和 `nil` 是假值，数字 `0` 也是真值。

当前多个接口使用 `lua_toboolean()` 读取参数，所以建议明确传 `true` / `false`：

```lua
gpio.write("B", 1, true)    -- 高电平
gpio.write("B", 1, false)   -- 低电平
```

不要把 `0` 当作 false：

```lua
gpio.write("B", 1, 0)       -- 注意：0 在 Lua 中是真值，当前实现会写高电平
```

## 4. GPIO API

### 4.1 `gpio.write(port, pin, value)`

设置指定 GPIO 引脚电平。

参数：

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `port` | string | GPIO 端口 |
| `pin` | integer | 引脚编号，范围 `0..15` |
| `value` | boolean | `true` 写高电平，`false` 写低电平 |

返回值：无。

示例：

```lua
gpio.write("B", 1, true)
gpio.write(gpio.PORTB, 1, false)
```

当前实现使用 `BSRR` 写寄存器，置位/复位不会影响同端口其他引脚。

### 4.2 `gpio.toggle(port, pin)`

翻转指定 GPIO 引脚。

```lua
gpio.toggle("B", 1)
```

### 4.3 `gpio.read(port, pin) -> boolean`

读取指定 GPIO 引脚。

```lua
local high = gpio.read("B", 1)
if high then
  -- 当前为高电平
end
```

### 4.4 GPIO 端口常量

模块会导出端口字符串常量：

| 常量 | 值 |
| --- | --- |
| `gpio.PORTA` | `"A"` |
| `gpio.PORTB` | `"B"` |
| `gpio.PORTC` | `"C"` |
| `gpio.PORTD` | `"D"` |
| `gpio.PORTE` | `"E"` |
| `gpio.PORTF` | `"F"` |
| `gpio.PORTG` | `"G"` |
| `gpio.PORTH` | `"H"` |
| `gpio.PORTI` | `"I"`，仅芯片/宏存在时导出 |
| `gpio.PORTJ` | `"J"`，仅芯片/宏存在时导出 |
| `gpio.PORTK` | `"K"`，仅芯片/宏存在时导出 |

推荐写法：
```lua
gpio.write(gpio.PORTB, 1, true)
```

注意：当前端口解析逻辑会取字符串中第一个 `A..K` 字母。推荐使用 `"B"`、`"PB"` 或 `gpio.PORTB`。不要使用 `"GPIOB"`，因为当前实现会先命中 `G`。

## PWM

推荐新脚本使用 snake_case API。旧的 camelCase 名称仅作为 legacy / compatibility 别名保留。

| API | 说明 |
| --- | --- |
| `pwm.count()` | 返回可用 PWM 数量 |
| `pwm.list()` | 返回 PWM 列表 |
| `pwm.info(pin)` | 返回指定 PWM 信息 |
| `pwm.setup(pin, config)` | 设置 PWM，`config` 可为频率数字或配置表 |
| `pwm.write(pin, duty)` | 写占空比 |
| `pwm.read(pin)` | 读占空比 |
| `pwm.set_freq(pin, freq)` | 设置频率 |
| `pwm.get_freq(pin)` | 读取频率 |
| `pwm.stop(pin)` | 停止输出 |
| `pwm.release(pin)` | 释放 PWM |

兼容别名：

| API | 说明 |
| --- | --- |
| `pwm.setFreq(pin, freq)` | legacy，等同 `pwm.set_freq(pin, freq)` |
| `pwm.getFreq(pin)` | legacy，等同 `pwm.get_freq(pin)` |

示例：

```lua
local PWM_PIN = 0

function init(self)
  pwm.setup(PWM_PIN, { freq = pwm.DEFAULT_FREQ, duty = pwm.MIN, start = true })
  pwm.set_freq(PWM_PIN, 2000)
end

function final(self)
  pwm.release(PWM_PIN)
end
```

## 5. delay API

### `delay(ms)`

毫秒级延时。在生命周期回调中调用时，`delay()` 会挂起当前 Lua 协程，运行时到期后再恢复执行，不会阻塞 RTOS 线程。

参数：

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `ms` | integer | 延时毫秒数。小于 0 时按 0 处理 |

返回值：无。

```lua
delay(100)
```

如果 `delay()` 在不可 yield 的 Lua 上下文中调用，例如顶层 chunk 或部分 C 事件回调中，会回退到 `HAL_Delay()`。

## 6. tim API

`tim` 模块基于 Cortex-M DWT `CYCCNT`，不占用具体 TIM 外设。

### 6.1 `tim.us() -> integer`

返回当前 DWT 计数换算出的微秒值。

```lua
local t0 = tim.us()
```

注意：DWT `CYCCNT` 是 32 位计数器，长时间运行后会回绕。适合短时间间隔测量。

### 6.2 `tim.delay_us(us)`

微秒级忙等延时。

参数：

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `us` | integer | 延时微秒数。小于等于 0 时直接返回 |

返回值：无。

```lua
tim.delay_us(50)
```

该函数会忙等阻塞。过大的 `us` 可能受 32 位计数计算影响，建议只用于短延时。

## RNG

`rng` 模块使用 STM32 HAL RNG，依赖 `MX_RNG_Init()` 已在 Lua VM 注册前完成。当前 `main()` 初始化顺序中已满足这一点。

| API | 说明 |
| --- | --- |
| `rng.u32()` | 返回一个 `0..0xFFFFFFFF` 的整数；硬件失败返回 `nil, "rng hardware failed"` |
| `rng.bytes(n)` | 返回长度为 `n` 的 Lua string；`n` 上限为 4096 字节 |

`rng.bytes(n)` 要求 `n` 是整数；`n < 0` 或过大属于参数错误，会抛 Lua error。

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

`crc` 模块使用 STM32 CRC 外设计算标准常见 IEEE CRC32。当前 `MX_CRC_Init()` 配置为默认多项式 `0x04C11DB7`、默认初始值 `0xFFFFFFFF`、输入按字节反转、输出反转，`CRC32_IEEE_Calculate()` 会追加最终异或 `0xFFFFFFFF`。因此 `crc.crc32_hex("hello")` 应返回 `"3610A686"`。

| API | 说明 |
| --- | --- |
| `crc.crc32(data)` | `data` 为 Lua string，成功返回 CRC32 数值，硬件不可用返回 `nil, "crc hardware failed"` |
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

## 7. UI Children

UI 接口基于 LVGL 9.5，但 Lua 用户侧不再暴露面向对象 setter 风格 API。脚本通过 `self.children` 声明 UI 树，宿主负责 UI 生命周期。

`self` 字段约定：

| 字段 | 说明 |
| --- | --- |
| `self.children` | 宿主管理的 UI / Drawable 树，可以是单个 Drawable 或 Drawable 数组 |
| `self.state` | 脚本自己的状态表，例如计时器、计数器、业务状态 |

不推荐把 UI 句柄挂到 `self.button`、`self.slider`、`self.label` 等顶层字段。

### 8.1 Drawable 通用 config

`ui.button(config)` 和 `ui.slider(config)` 都支持以下通用字段：

| 字段 | 说明 |
| --- | --- |
| `id` | 字符串 ID，用于 `ui.find()` 和 `ui.patch()` |
| `x` / `y` / `w` / `h` | 显式坐标和尺寸字段 |
| `rect = { x, y, w, h }` | 一次设置坐标和尺寸 |
| `pos = { x, y }` | 一次设置坐标 |
| `size = { w, h }` | 一次设置尺寸 |
| `hidden` | 设置或清除隐藏状态 |
| `input` | 投递到 `on_input(self, action_id, action)` 的动作 ID |
| `style` | 控件样式表 |

如果同时提供 `rect` / `pos` / `size` 和显式 `x` / `y` / `w` / `h`，显式字段优先。未提供 parent 时默认挂到当前活动 screen。Lua 层不再暴露 `get_screen()`。

### 8.2 Button config

```lua
ui.button({
  id = "run",
  text = "Run",
  rect = { 24, 24, 120, 48 },
  input = "run",
  style = {
    bg = 0x2D8CFF,
    bg_alpha = 255,
    text = 0xFFFFFF,
    border = { color = 0x145DA0, width = 2 },
    radius = 8,
  },
})
```

支持字段：

| 字段 | 说明 |
| --- | --- |
| `text` | 按钮文本 |
| `input` | 输入事件动作 ID |
| `style.bg` / `style.bg_alpha` | 背景色和透明度 |
| `style.text` | 文本颜色 |
| `style.border.color` / `style.border.width` | 边框颜色和宽度 |
| `style.radius` | 圆角半径 |

### 8.3 Slider config

```lua
ui.slider({
  id = "volume",
  rect = { 24, 90, 220, 24 },
  range = { 0, 255 },
  value = 128,
  input = "volume",
  style = {
    bg = 0x202020,
    indicator = 0x2D8CFF,
    knob = 0xFFFFFF,
    radius = 8,
  },
})
```

支持字段：

| 字段 | 说明 |
| --- | --- |
| `range = { min, max }` | 滑块范围 |
| `value` | 当前值 |
| `input` | 输入事件动作 ID |
| `style.bg` / `style.bg_alpha` | 主体背景色和透明度 |
| `style.indicator` / `style.indicator_alpha` | 指示条颜色和透明度 |
| `style.knob` / `style.knob_alpha` | 旋钮颜色和透明度 |
| `style.border.color` / `style.border.width` | 边框颜色和宽度 |
| `style.radius` | 圆角半径 |

### 8.4 UI 输入事件

按钮和滑块创建后会自动把 LVGL 输入事件投递到 Lua VM 的输入队列，并在下一次调度时调用：

```lua
function on_input(self, action_id, action)
end
```

控件默认 `action_id`：

| 控件 | 默认 `action_id` |
| --- | --- |
| `ui.button` | `"button"` |
| `ui.slider` | `"slider"` |

推荐用 config 的 `input` 字段设置脚本侧业务 ID，`action_id` 最长 23 字节。

按钮事件：

| `action.event` |
| --- |
| `"pressed"` |
| `"released"` |
| `"clicked"` |

滑块事件：

| `action.event` | 说明 |
| --- | --- |
| `"changed"` | `action.value` 为当前滑块值 |

### 8.5 查询和更新

`ui.find(self, id)` 在 `self.children` 中递归查找 `id`，找到返回 Drawable 句柄，找不到返回 `nil`。

`ui.patch(self, id, patch)` 找到 Drawable 后按构造 config 同名字段更新。成功返回 `true`；找不到返回 `nil, "ui id not found"`。

```lua
local btn = ui.find(self, "run")

local ok, err = ui.patch(self, "run", {
  text = "Running",
  style = {
    bg = 0x00AA00,
  },
})
```

参数数量或类型错误属于脚本错误，使用 Lua error。运行时失败返回 `nil, err`。

### 8.6 生命周期

`self.children` 可以是单个 Drawable，也可以是 Drawable 数组。宿主会在 `final(self)` 返回后自动递归删除 `self.children`，并清空该字段。容器型 Drawable 如果未来拥有 `children`，也按同样规则递归删除。

```lua
function final(self)
  -- UI children are deleted by the host after final(self).
end
```

## 9. UI API

| API | 说明 |
| --- | --- |
| `ui.button(config)` | 创建按钮 Drawable |
| `ui.slider(config)` | 创建滑块 Drawable |
| `ui.find(self, id)` | 递归查找 Drawable |
| `ui.patch(self, id, patch)` | 更新 Drawable |

旧 Lua UI API 不再注册到用户侧：`ui.button.create()`、`ui.button.draw()`、`ui.button.get_screen()`、`ui.slider.create()`、`ui.slider.draw()`、`ui.slider.get_screen()`，以及所有 `button:xxx()` / `slider:xxx()` setter、callback、delete 方法。

## 10. UI 示例

### 10.1 单按钮

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

### 10.2 按钮 + 滑块

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

### 10.3 通过 id 更新 UI

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

## 11. 示例

### 11.1 非阻塞闪灯

使用 `delay()` 可以写成顺序逻辑，Lua VM 会在延时期间让出当前协程：

```lua
function init(self)
  self.led_on = false
end

function update(self, dt)
  self.led_on = not self.led_on
  gpio.write(gpio.PORTB, 1, self.led_on)
  delay(500)
end
```

也可以继续使用 `dt` 做状态机：

```lua
function init(self)
  self.acc = 0
  self.led_on = false
  gpio.write(gpio.PORTB, 1, false)
end

function update(self, dt)
  self.acc = self.acc + dt
  if self.acc >= 0.5 then
    self.acc = self.acc - 0.5
    self.led_on = not self.led_on
    gpio.write(gpio.PORTB, 1, self.led_on)
  end
end
```

### 11.2 创建按钮和滑块

```lua
function init(self)
  self.state = {
    level = 50,
  }

  self.children = {
    ui.button({
      id = "start",
      text = "Start",
      rect = { 20, 20, 140, 50 },
      input = "start",
      style = {
        bg = 0x2196F3,
        text = 0xFFFFFF,
        radius = 8,
      },
    }),

    ui.slider({
      id = "level",
      rect = { 20, 100, 200, 20 },
      range = { 0, 100 },
      value = self.state.level,
      input = "level",
      style = {
        indicator = 0xFFC107,
        knob = 0xFF9800,
      },
    }),
  }
end

function on_input(self, action_id, action)
  if action_id == "start" and action.event == "clicked" then
    print("start")
  elseif action_id == "level" and action.event == "changed" then
    self.state.level = action.value
    print("level", self.state.level)
  end
end
```

## 12. 当前实现注意事项

- 当前只打开 `_G` / `coroutine` / `table` / `string` / `math` / `utf8`，没有打开 `io`、`os`、`package`、`debug`。
- `gpio.write()` 的第三个参数请使用 `true` / `false`，数字 `0` 在 Lua 中仍为真值。
- GPIO 端口推荐使用 `"B"`、`"PB"` 或 `gpio.PORTB`；当前不要使用 `"GPIOB"` 这种字符串。
- UI 控件创建后会默认挂接 LVGL 事件并投递到 `on_input()`；脚本通过 `self.children` 声明 UI 树。
- 宿主会在 `final(self)` 返回后自动递归删除 `self.children`。
- `delay()` 在生命周期回调中是协程非阻塞延时；`tim.delay_us()` 仍是忙等阻塞调用。
