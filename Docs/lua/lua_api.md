# CartDesk Lua API

Lua runtime 提供受限标准库、GPIO/PWM/TIM/RNG/CRC 绑定、生命周期回调以及声明式
UI。硬件接口详见同目录的 `lua_gpio.md`、`lua_pwm.md` 和相关模块文档。

## 应用实例

宿主在 `init(self)` 前自动创建五个普通 table：

```lua
self.state
self.ui
self.assets
self.timers
self.services
```

应用把业务数据放入 `state`，UI handle 放入 `ui`，资源路径或资源 handle 放入
`assets`。`timers` 和 `services` 当前只预留命名空间，没有对应完整 API。

## 创建 UI

```lua
self.ui.title = ui.label({
    id = "title",
    text = "Hello",
    rect = { 20, 20, 200, 40 },
})

self.ui.logo = ui.image({
    id = "logo",
    src = "assets/logo.png",
    rect = { 20, 80, 128, 128 },
})

self.ui.button = ui.button({
    id = "button",
    text = "点击",
    rect = { 20, 230, 120, 48 },
    input = "button",
})
```

创建成功返回 UI full userdata handle；失败返回 `nil, error`。需要处理错误时：

```lua
local title, err = ui.label({
    id = "title",
    text = "Hello",
    rect = { 20, 20, 200, 40 },
})
if not title then
    print("title failed", err)
    return
end
self.ui.title = title
```

## 修改 UI

唯一修改入口接收 handle 和属性 table：

```lua
local ok, err = ui.patch(
    self.ui.title, {
    text = "Updated",
    hidden = false,
})
```

label 支持 `text`、`rect`、`hidden`；button 支持这些属性及 `style`；image 支持
`src`、`rect`、`hidden`、`region` 和 `style`。未知属性和错误类型不会被忽略，
而是返回可读错误。

## id、input 与 Lua 字段

```text
self.ui.button     Lua 保存的 UI handle
input = "button"  产生输入 action_id
id = "button"     调试标识
```

三者没有自动绑定关系。业务代码保存并直接使用 handle，不按字符串查找对象。

## 输入事件

```lua
function on_input(self, action_id, action)
    if action_id == "button" and action.event == "clicked" then
        ui.patch(
            self.ui.title, { text = "Clicked" })
    end
end
```

button 可产生 `pressed`、`released` 和 `clicked`。事件只投递给控件所属应用。

## 所有权与清理

每个应用有宿主管理的 LVGL 根容器和 owner registry。创建成功的对象立即登记；
Lua table 不是所有权来源。关闭时宿主删除根容器，`LV_EVENT_DELETE` 使全部 handle
失效并释放图片资源。已删除、跨应用或错误 VM 的 handle 只返回错误，不访问裸
LVGL 指针。

完整示例见 `examples/lua/ui_state_example.lua`。
