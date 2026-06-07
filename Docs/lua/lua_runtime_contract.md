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

## self 状态表

`self` 是每个脚本实例独立拥有的私有状态表。脚本应把跨帧状态、资源句柄、UI 对象、计时器累计值等保存在 `self` 上。

示例：

```lua
function init(self)
    self.elapsed = 0
    self.count = 0
end

function update(self, dt)
    self.elapsed = self.elapsed + dt
end
```

宿主不应把不同脚本实例的 `self` 混用。脚本也不应假设全局变量可以替代实例状态，因为全局环境和热重载策略可能变化。

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

- 删除 UI 对象，例如 `btn:delete()`、`slider:delete()`。
- 释放 GPIO，例如 `gpio.release(pin)`。
- 停止或释放 PWM，例如 `pwm.stop(pin)`、`pwm.release(pin)`。
- 关闭 SD 文件句柄，例如 `f:close()`。
- 清空 `self` 上不再有效的句柄。

`final` 应尽量短小、幂等。即使资源已经释放或创建失败，也应安全返回。

## 热重载 on_reload

`on_reload(self)` 用于热重载前后的状态调整。推荐用途：

- 保存需要跨重载保留的轻量状态。
- 关闭或刷新旧 UI 对象。
- 输出脚本版本或重载日志。
- 标记下一次 `update` 重新同步硬件状态。

`on_reload` 不应承担完整初始化职责。完整初始化仍应放在 `init(self)`，资源清理仍应放在 `final(self)`。

## 事件参数

`on_input(self, action_id, action)`：

- `action_id` 是输入动作 ID 字符串。
- `action` 是输入事件表，宿主可提供 `pressed`、`released`、`value` 等字段。

`on_message(self, message_id, message, sender)`：

- `message_id` 是消息 ID 字符串。
- `message` 当前可能为 `nil`，具体结构由宿主消息系统决定。
- `sender` 是发送者标识字符串或 `nil`。

脚本应对可选字段做空值保护。
