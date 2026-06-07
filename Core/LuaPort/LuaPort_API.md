# LuaPort Lua 端 API 文档

本文档按当前工程源码整理，覆盖 Lua 脚本侧能直接使用的接口。

相关源码：

- `Core/Src/lua_vm.c`：Lua 运行时、脚本实例和 Defold 风格生命周期调度。
- `Core/Inc/lua_vm.h`：运行时对外 C 接口。
- `Core/LuaPort/lua_port.c`：把底层模块绑定到 Lua 全局环境。
- `Core/LuaPort/modules/lua_gpio.c`：GPIO 模块。
- `Core/LuaPort/modules/lua_tim.c`：微秒计时与微秒延时。
- `Core/LuaPort/modules/lua_delay.c`：毫秒级协程延时。
- `Core/LuaPort/modules/lua_sd.c`：FatFs SD 文件接口。
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
| `tim` | table | `lua_tim.c` | DWT 微秒计时、微秒忙等 |
| `sd` | table | `lua_sd.c` | FatFs 文件读写 |
| `ui` | table | `lua_ui_button.c` / `lua_ui_slider.c` | LVGL UI 命名空间 |
| `ui.button` | table | `lua_ui_button.c` | 按钮创建和控制 |
| `ui.slider` | table | `lua_ui_slider.c` | 滑块创建和控制 |
| `delay` | function | `lua_delay.c` | 毫秒级协程延时 |
| `print` | function | Lua base lib | 调试输出函数，输出去向由运行时日志实现决定 |

## 3. Lua 真值约定

Lua 里只有 `false` 和 `nil` 是假值，数字 `0` 也是真值。

当前多个接口使用 `lua_toboolean()` 读取参数，所以建议明确传 `true` / `false`：

```lua
gpio.write("B", 1, true)    -- 高电平
gpio.write("B", 1, false)   -- 低电平

slider:set_value(50, true)  -- 开启动画
slider:set_value(50, false) -- 不开动画
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

## 7. SD / FatFs API

`sd` 模块提供 FatFs 文件读写。第一次 `sd.open()` 会自动挂载，也可以手动调用 `sd.mount()`。

### 7.1 路径规则

默认宏：

| 宏 | 默认值 |
| --- | --- |
| `LUA_SD_DRIVE` | `"0:"` |
| `LUA_SD_MOUNT_PATH` | `LUA_SD_DRIVE` |

路径处理规则：

- 传入路径包含 `:` 时，按原样传给 FatFs。
- 传入路径不包含 `:` 且以 `/` 开头时，自动拼成 `0:/xxx`。
- 传入路径不包含 `:` 且不以 `/` 开头时，自动拼成 `0:/xxx`。

示例：

```lua
sd.open("log.txt", "a")      -- 实际路径：0:/log.txt
sd.open("/dir/a.txt", "r")   -- 实际路径：0:/dir/a.txt
sd.open("1:/a.txt", "r")     -- 保持原样
```

内部路径缓冲区长度为 256 字节，脚本侧应避免过长路径。

### 7.2 `sd.mount() -> boolean`

强制挂载 SD 文件系统。

成功返回 `true`，失败抛出 Lua 错误。

```lua
sd.mount()
```

### 7.3 `sd.umount() -> boolean`

卸载 SD 文件系统。

成功返回 `true`，失败抛出 Lua 错误。

```lua
sd.umount()
```

### 7.4 `sd.open(path, mode?) -> file`

打开文件并返回 `sd.file` userdata。

参数：

| 参数 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `path` | string | 必填 | 文件路径 |
| `mode` | string | `"r"` | 打开模式 |

支持的模式：

| 模式 | 含义 |
| --- | --- |
| `"r"` / `"rb"` | 只读，文件必须存在 |
| `"r+"` / `"rb+"` | 读写，文件必须存在 |
| `"w"` / `"wb"` | 写入，创建或清空 |
| `"w+"` / `"wb+"` | 读写，创建或清空 |
| `"a"` / `"ab"` | 追加写，文件不存在则创建 |
| `"a+"` / `"ab+"` | 追加读写，文件不存在则创建 |

```lua
local f = sd.open("log.txt", "a")
```

打开失败会抛出 Lua 错误，错误信息包含 FatFs `FRESULT` 名称和值。

### 7.5 文件对象方法

#### `file:write(data) -> written_len`

写入字符串数据，返回实际写入字节数。

```lua
local n = f:write("hello\n")
```

#### `file:read(n) -> string`

最多读取 `n` 字节，返回字符串。读到 EOF 时返回内容可能短于 `n`。

```lua
local data = f:read(128)
```

`n` 必须大于等于 0。

#### `file:seek(pos) -> new_pos`

跳转到文件绝对位置 `pos`，返回跳转后的当前位置。

```lua
f:seek(0)
```

`pos` 必须大于等于 0。当前接口不支持 `SEEK_CUR` / `SEEK_END` 风格参数。

#### `file:size() -> size`

返回文件大小，单位字节。

```lua
local size = f:size()
```

#### `file:close()`

关闭文件。已关闭文件再次关闭不会报错。

```lua
f:close()
```

兼容别名：`file:wirte(data)` 等价于 `file:write(data)`，这是为了兼容已有 typo，不推荐新代码使用。

### 7.6 模块级包装函数

以下函数等价于对应的文件对象方法：

| 模块级函数 | 等价写法 |
| --- | --- |
| `sd.write(f, data)` | `f:write(data)` |
| `sd.read(f, n)` | `f:read(n)` |
| `sd.seek(f, pos)` | `f:seek(pos)` |
| `sd.size(f)` | `f:size()` |
| `sd.close(f)` | `f:close()` |

兼容别名：`sd.wirte(f, data)` 等价于 `sd.write(f, data)`，不推荐新代码使用。

示例：

```lua
local f = sd.open("hello.txt", "w")
sd.write(f, "hello\n")
sd.close(f)
```

## 8. UI 通用约定

UI 接口基于 LVGL 9.5。

### 8.1 父对象

`ui.button.create(parent?)`、`ui.button.draw(parent?, ...)`、`ui.slider.create(parent?)`、`ui.slider.draw(parent?, ...)` 都支持可选父对象：

- 省略或传 `nil`：使用 `lv_screen_active()`。
- 传入 `ui.button.get_screen()` / `ui.slider.get_screen()` 返回的对象：使用该屏幕对象。

```lua
local scr = ui.button.get_screen()
local btn = ui.button.create(scr)
```

### 8.2 对齐字符串

按钮和滑块的 `align()` 支持以下字符串：

| 字符串 |
| --- |
| `"center"` |
| `"top_left"` |
| `"top_mid"` |
| `"top_right"` |
| `"bottom_left"` |
| `"bottom_mid"` |
| `"bottom_right"` |
| `"left_mid"` |
| `"right_mid"` |

未知字符串会按 `"center"` 处理。

### 8.3 颜色和透明度

颜色使用 `0xRRGGBB` 整数：

```lua
btn:set_style_bg_color(0x2196F3, 255)
```

`alpha` 范围建议为 `0..255`，默认 `255`。当前实现会转成 `uint8_t`，超出范围会截断。

### 8.4 UI 回调当前限制

`set_callback(callback)` 当前会保存 Lua 回调引用，但源码中尚未调用 `lv_obj_add_event_cb()` 把 C 事件回调挂到 LVGL 对象上，因此事件不会自动触发。

同时，当前 C 事件回调实现把第一个参数压成 lightuserdata，不是带 metatable 的 Lua 控件对象。即使后续接上事件，也不应依赖 `obj:set_text(...)` 这种写法，除非 C 侧改为传回完整 userdata。

推荐未来修复事件派发后，在 Lua 侧用闭包捕获控件对象：

```lua
local btn = ui.button.draw(nil, 20, 20, 120, 50, "OK")

btn:set_callback(function(_, event)
  if event == "clicked" then
    btn:set_text("Clicked")
  end
end)
```

事件字符串转换函数已覆盖：

| 事件字符串 |
| --- |
| `"clicked"` |
| `"pressed"` |
| `"released"` |
| `"long_pressed"` |
| `"value_changed"` |
| `"unknown"` |

## 9. ui.button API

### 9.1 模块函数

#### `ui.button.create(parent?) -> button`

创建按钮。默认父对象为当前活动屏幕。

```lua
local btn = ui.button.create()
```

#### `ui.button.draw(parent?, x?, y?, width?, height?, text?) -> button`

创建按钮并设置位置、尺寸和文本。

默认值：

| 参数 | 默认值 |
| --- | ---: |
| `x` | `0` |
| `y` | `0` |
| `width` | `100` |
| `height` | `50` |
| `text` | `"Button"` |

```lua
local btn = ui.button.draw(nil, 20, 30, 120, 48, "Start")
```

#### `ui.button.get_screen() -> screen`

返回当前活动屏幕 lightuserdata，可作为父对象传给 `create()` / `draw()`。

```lua
local scr = ui.button.get_screen()
```

### 9.2 按钮对象方法

#### `button:set_text(text)`

设置按钮文本。若按钮还没有 label，会自动创建并居中。

```lua
btn:set_text("Run")
```

#### `button:set_size(width, height)`

设置按钮尺寸。

```lua
btn:set_size(150, 60)
```

#### `button:set_pos(x, y)`

设置按钮坐标。

```lua
btn:set_pos(20, 30)
```

#### `button:align(align_type, x_offset?, y_offset?)`

按 LVGL 对齐方式定位按钮。偏移默认值为 `0`。

```lua
btn:align("center", 0, -50)
```

#### `button:set_style_bg_color(color, alpha?)`

设置按钮背景色和透明度。

```lua
btn:set_style_bg_color(0x2196F3, 255)
```

#### `button:set_style_text_color(color)`

设置按钮文本颜色。该方法只在 label 已存在时生效，所以应先调用 `set_text()`，或使用 `draw()` 创建带文本的按钮。

```lua
btn:set_text("OK")
btn:set_style_text_color(0xFFFFFF)
```

#### `button:set_style_border(color, width?)`

设置按钮边框颜色和宽度。`width` 默认 `1`。

```lua
btn:set_style_border(0x1976D2, 2)
```

#### `button:set_style_radius(radius)`

设置按钮圆角半径。

```lua
btn:set_style_radius(8)
```

#### `button:add_flag(flag_name)`

添加 LVGL 对象 flag。

支持的字符串：

| flag |
| --- |
| `"hidden"` |
| `"clickable"` |
| `"checkable"` |
| `"scrollable"` |
| `"press_lock"` |

未知字符串会被忽略。

```lua
btn:add_flag("checkable")
```

#### `button:clear_flag(flag_name)`

移除 LVGL 对象 flag。

```lua
btn:clear_flag("hidden")
```

#### `button:set_checkable(enable)`

启用或关闭可选中状态。

```lua
btn:set_checkable(true)
btn:set_checkable(false)
```

注意：不要用 `0` 表示 false。

#### `button:is_checked() -> boolean`

返回按钮是否处于 `LV_STATE_CHECKED` 状态。

```lua
if btn:is_checked() then
  -- checked
end
```

#### `button:set_callback(callback_or_nil)`

设置或清除按钮事件回调。

```lua
btn:set_callback(function(obj, event)
  -- 当前事件尚未自动接入 LVGL，见“UI 回调当前限制”
end)

btn:set_callback(nil) -- 清除回调
```

#### `button:delete()`

删除按钮对象并释放保存的 Lua 回调引用。

```lua
btn:delete()
```

删除后继续调用该对象方法会抛出 Lua 错误。

## 10. ui.slider API

### 10.1 模块函数

#### `ui.slider.create(parent?) -> slider`

创建滑块。默认父对象为当前活动屏幕。

```lua
local slider = ui.slider.create()
```

#### `ui.slider.draw(parent?, x?, y?, width?, height?) -> slider`

创建滑块并设置位置和尺寸。

默认值：

| 参数 | 默认值 |
| --- | ---: |
| `x` | `0` |
| `y` | `0` |
| `width` | `200` |
| `height` | `20` |

```lua
local slider = ui.slider.draw(nil, 20, 100, 200, 20)
```

#### `ui.slider.get_screen() -> screen`

返回当前活动屏幕 lightuserdata，可作为父对象传给 `create()` / `draw()`。

```lua
local scr = ui.slider.get_screen()
```

### 10.2 滑块对象方法

#### `slider:set_size(width, height)`

设置滑块尺寸。

```lua
slider:set_size(200, 20)
```

#### `slider:set_pos(x, y)`

设置滑块坐标。

```lua
slider:set_pos(20, 100)
```

#### `slider:align(align_type, x_offset?, y_offset?)`

按 LVGL 对齐方式定位滑块。偏移默认值为 `0`。

```lua
slider:align("center", 0, 50)
```

#### `slider:set_range(min, max)`

设置滑块数值范围。

```lua
slider:set_range(0, 100)
```

#### `slider:set_value(value, anim)`

设置滑块当前值。

```lua
slider:set_value(50, true)
slider:set_value(0, false)
```

`anim` 使用 Lua 真值，建议传 `true` / `false`。

#### `slider:get_value() -> integer`

读取滑块当前值。

```lua
local value = slider:get_value()
```

#### `slider:set_style_bg_color(color, alpha?)`

设置滑块主体背景色和透明度。

```lua
slider:set_style_bg_color(0xEEEEEE, 255)
```

#### `slider:set_style_indicator_color(color, alpha?)`

设置滑块已填充指示条颜色和透明度。

```lua
slider:set_style_indicator_color(0xFFC107, 255)
```

#### `slider:set_style_knob_color(color, alpha?)`

设置滑块旋钮颜色和透明度。

```lua
slider:set_style_knob_color(0xFF9800, 255)
```

#### `slider:set_style_border(color, width?)`

设置滑块主体边框颜色和宽度。`width` 默认 `1`。

```lua
slider:set_style_border(0xBDBDBD, 1)
```

#### `slider:set_style_radius(radius)`

设置滑块主体、指示条、旋钮的圆角半径。

```lua
slider:set_style_radius(10)
```

#### `slider:set_callback(callback_or_nil)`

设置或清除滑块事件回调。

```lua
slider:set_callback(function(obj, event)
  -- 当前事件尚未自动接入 LVGL，见“UI 回调当前限制”
end)

slider:set_callback(nil) -- 清除回调
```

#### `slider:delete()`

删除滑块对象并释放保存的 Lua 回调引用。

```lua
slider:delete()
```

删除后继续调用该对象方法会抛出 Lua 错误。

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

### 11.2 写入 SD 日志

```lua
function init(self)
  local f = sd.open("boot.log", "a")
  f:write("lua boot\n")
  f:close()
end
```

### 11.3 创建按钮和滑块

```lua
local btn
local slider

function init(self)
  btn = ui.button.draw(nil, 20, 20, 140, 50, "Start")
  btn:set_style_bg_color(0x2196F3, 255)
  btn:set_style_text_color(0xFFFFFF)
  btn:set_style_radius(8)

  slider = ui.slider.draw(nil, 20, 100, 200, 20)
  slider:set_range(0, 100)
  slider:set_value(50, false)
  slider:set_style_indicator_color(0xFFC107, 255)
  slider:set_style_knob_color(0xFF9800, 255)
end
```

## 12. 当前实现注意事项

- 当前只打开 `_G` / `coroutine` / `table` / `string` / `math` / `utf8`，没有打开 `io`、`os`、`package`、`debug`。
- `gpio.write()` 的第三个参数请使用 `true` / `false`，数字 `0` 在 Lua 中仍为真值。
- GPIO 端口推荐使用 `"B"`、`"PB"` 或 `gpio.PORTB`；当前不要使用 `"GPIOB"` 这种字符串。
- UI `set_callback()` 当前只保存 Lua 回调引用，尚未真正挂接 LVGL 事件。
- UI 事件回调当前 C 代码传入的是 lightuserdata，不是可直接调用方法的 Lua 控件 userdata。
- `delay()` 在生命周期回调中是协程非阻塞延时；`tim.delay_us()` 仍是忙等阻塞调用。
- SD 文件操作失败会抛 Lua 错误；需要脚本侧容错时，应先开启 `pcall` 所在标准库或在 C 侧提供封装。
