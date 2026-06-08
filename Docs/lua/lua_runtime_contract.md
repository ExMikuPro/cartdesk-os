# Lua 运行时契约

本文档固化当前 Lua VM 与脚本之间的生命周期和行为约定，便于脚本作者、宿主侧和测试脚本使用同一套预期。

## 推荐生命周期顺序

宿主侧推荐按以下顺序驱动脚本：

1. VM 创建。
2. 加载脚本。
3. 创建脚本实例和私有 `self` 状态表。
4. 调用 `init(self)`。
5. 每帧或每个调度周期处理事件与更新：
   - `on_input(self, action_id, action)` 按输入事件触发。
   - `fixed_update(self, dt)` 按固定步长触发。
   - `update(self, dt)` 按帧触发。
   - `late_update(self, dt)` 在 `update` 后触发。
   - `on_message(self, message_id, message, sender)` 按消息事件触发。
6. 热重载时调用 `on_reload(self)`。
7. 销毁脚本或关闭 VM 前调用 `final(self)`。

`start()` 是旧脚本兼容入口。宿主在未找到 `init` 且找到 `start` 时可用 `start()` 作为初始化兼容路径；新脚本不应再依赖 `start()`。

## self 使用规范

`self` 是每个脚本实例独立拥有的私有状态表。新脚本在 `self` 顶层只推荐使用 `self.state` 和 `self.children`。

`self.children` 用于 UI / Drawable 树，由宿主管理生命周期。`self.state` 用于脚本跨帧状态。硬件资源句柄如果必须跨帧保存，放入 `self.state.resources`。固定 pin 和常量使用文件级 `local`，临时变量使用函数内 `local`。

不推荐在 `self` 顶层创建 `self.elapsed`、`self.count`、`self.button`、`self.slider`、`self.file`、`self.pwm` 等字段。

示例：

```lua
function init(self)
    self.state = {
        elapsed = 0,
        count = 0,
    }
end

function update(self, dt)
    self.state.elapsed = self.state.elapsed + dt
end
```

宿主不应把不同脚本实例的 `self` 混用。脚本也不应假设全局变量可以替代实例状态，因为全局环境和热重载策略可能变化。

## UI Children

`self.children` 可以是单个 Drawable，也可以是 Drawable 数组。当前 Drawable 包括 `ui.button()`、`ui.slider()` 和 `ui.image()`。宿主会在 `final(self)` 返回后自动删除 `self.children`，并清空该字段。脚本通常不需要在 `final(self)` 中删除 UI。

需要更新 UI 时使用 `ui.patch(self, id, patch)`。需要查找 UI 时使用 `ui.find(self, id)`。

`ui.image()` 的 `src` 是当前 `cart.bin` 内部的资源相对路径，由宿主通过 INDEX/DATA 查找并管理图片内存。Lua 层不接触 cart offset、size、CRC、framebuffer 指针或 LVGL image descriptor。

加载 cart 入口脚本时，宿主会解析资源索引并生成图片资源目录。图片资源第一版采用同步懒加载：`ui.image()` 创建时才从 DATA 段读取 `XHGC_RES_IMAGE` + `XHGC_IMG_BGRA8888` 内容到 SDRAM 的 `APP_ARENA_REST` scene arena，不使用 MDMA 异步搬运、LRU 或压缩资源。

图片 Drawable 创建时会持有宿主侧资源 handle；相同 `src` 共享同一份 SDRAM 像素数据。Drawable 删除或 `self.children` 被宿主清理时释放引用。引用计数归零后资源只进入未使用状态，不单独释放 arena 中间块；场景结束时宿主统一 reset scene arena 并让旧 handle 失效。Lua 层不能访问资源 handle、SDRAM 地址、cart offset、size 或 CRC。

## 硬件外设与 self.state

GPIO/PWM/TIM/RNG/CRC 不属于 `self.children`。固定 pin 使用文件级 `local`，临时读数使用函数内 `local`，跨帧状态使用 `self.state`，跨帧资源句柄使用 `self.state.resources`。非 UI 资源仍然需要在 `final(self)` 中释放。

## dt 单位

当前代码实现中，`dt` 单位为秒。

- `update(self, dt)` 和 `late_update(self, dt)` 接收本帧耗时秒数。
- `fixed_update(self, dt)` 接收固定步长秒数，当前默认固定步长为 `1 / 60` 秒。
- 宿主时间源来自毫秒 tick，但在传给脚本前已换算为秒。

如果未来宿主侧实现改变了 `dt` 单位，需要同步修正文档、示例和调用层。

## 错误处理建议

生命周期函数报错时，推荐宿主行为如下：

- 终止当前生命周期函数调用。
- 将错误写入日志，包含脚本来源和生命周期函数名。
- VM 不应因为单个脚本错误直接崩溃。
- 是否禁用该脚本、重试、继续下一帧或进入降级状态，由宿主策略决定。

脚本作者应避免在生命周期函数中吞掉所有错误；必要时只在局部资源操作上使用 `pcall`，并输出足够的错误信息。

## final 的资源释放责任

`final(self)` 用于释放脚本创建或占用的资源。脚本作者应在这里：

- 释放 GPIO，例如 `gpio.release(pin)`。
- 停止或释放 PWM，例如 `pwm.stop(pin)`、`pwm.release(pin)`。
- 清空 `self.state` 中不再有效的非 UI 句柄。

UI 放在 `self.children` 中，由宿主在 `final(self)` 返回后自动递归删除，脚本不需要手动删除 UI。

`final` 应尽量短小、幂等。即使资源已经释放或创建失败，也应安全返回。

## 热重载 on_reload

`on_reload(self)` 用于热重载前后的状态调整。推荐用途：

- 保存需要跨重载保留的轻量状态。
- 更新或重建 `self.children` 中的 UI 状态。
- 输出脚本版本或重载日志。
- 标记下一次 `update` 重新同步硬件状态。

`on_reload` 不应承担完整初始化职责。完整初始化仍应放在 `init(self)`，资源清理仍应放在 `final(self)`。

## 事件参数

`on_input(self, action_id, action)`：

- `action_id` 是输入动作 ID 字符串。
- `action` 是输入事件表，宿主可提供 `event`、`pressed`、`released`、`repeated`、`value`、`x`、`y`、`dx`、`dy` 等字段。
- UI 按钮和滑块的 LVGL 输入事件统一投递到 `on_input`。控件默认 `action_id` 为 `"button"` / `"slider"`，可用 config 的 `input` 字段设置脚本侧业务 ID。

`on_message(self, message_id, message, sender)`：

- `message_id` 是消息 ID 字符串。
- `message` 当前可能为 `nil`，具体结构由宿主消息系统决定。
- `sender` 是发送者标识字符串或 `nil`。

脚本应对可选字段做空值保护。
