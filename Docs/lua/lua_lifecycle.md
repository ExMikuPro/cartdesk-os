# Lua 脚本生命周期

每个脚本实例拥有独立环境、独立 `self` 和独立 UI owner。宿主支持以下可选回调：

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

缺失的回调会被跳过。初始化顺序为 `init`；一帧内依次处理输入、零到五次
`fixed_update`、`update`、`late_update` 和消息。`dt` 的单位是秒，固定步长默认
为 `1 / 60` 秒。

宿主在调用 `init(self)` 前创建：

```text
self.state
self.ui
self.assets
self.timers
self.services
```

这五个字段都是当前实例私有的普通 Lua table。应用重新启动会得到新的 table；
热重载保留同一实例和同一 `self`，然后调用 `on_reload(self)`。

UI 控件产生的事件只派发给控件所属应用。外部 C 输入可使用
`lua_post_input()` 广播；控件内部使用 owner 定向队列。输入动作表包含 `event`、
`pressed`、`released`、`repeated`、`value`、`x`、`y`、`dx` 和 `dy`。

关闭时先停止调度和新输入，再调用一次 `final(self)`，随后按宿主 owner 删除应用
UI 根容器、使全部 handle 失效、释放 registry 引用、重置场景资源并关闭 VM。
`final` 只会在 `init` 成功后调用；`init` 失败时已创建的 UI 立即由 owner 清理。
