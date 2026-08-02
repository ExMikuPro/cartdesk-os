# CartDesk Lua 应用实例

## self 的五个节点

宿主在 `init(self)` 前创建 `state`、`ui`、`assets`、`timers`、`services` 五个
相互独立的普通 Lua table。不同实例不共享，应用重新启动时会重新创建。

## self.state

保存计数、页面状态等业务数据，不保存底层指针。

## self.ui

保存 `ui.label`、`ui.button`、`ui.image` 返回的 full userdata handle。这个 table
只是引用容器，不负责对象所有权。

## self.assets

保存 cart 资源路径或资源 API 返回的 handle，例如
`self.assets.logo = "assets/logo.png"`。

## self.timers

当前由宿主创建为空 table，完整 timer API 尚未实现。

## self.services

当前由宿主创建为空 table，完整异步 service API 尚未实现。

## 创建 UI

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

创建对象只能挂到当前应用专属 LVGL 根容器。成功返回安全 handle，失败返回
`nil, error`。

## 修改 UI

```lua
local ok, err = ui.patch(
    self.ui.title, { text = "Updated" })
if not ok then
    print("patch failed", err)
end
```

修改前宿主校验 handle 的 VM、owner、generation、alive 和对象类型。

## 输入事件

button 的 `input` 值成为 `action_id`。`id` 只用于调试，不用于查找对象。

## UI 对象生命周期

宿主登记每个对象的 owner、handle、LVGL object、类型、generation 和存活状态。
对象收到 `LV_EVENT_DELETE` 时立即清空裸指针、标记失效、递增 generation，并释放
图片等模块资源。父根容器删除也会触发所有子对象的失效路径。

## 自动资源清理

停止输入和更新后，宿主调用 `final(self)`，再删除应用 UI 根容器并释放 registry
引用。即使应用替换 `self.ui` 或把字段设为 `nil`，登记过的对象仍会完整清理。

## 错误处理

创建和 patch 的可预期错误均返回 `nil, error`。普通 table、跨应用 handle、已删除
handle、未知属性和错误属性类型均会被拒绝，不会访问 `lv_obj_t`。

## 完整示例

见 `examples/lua/ui_state_example.lua`。该示例展示五节点 self、label/image/button
创建、输入事件以及 handle patch。

## 检查

```bash
build/host_tools/bin/luavm --self-test
build/host_tools/bin/luavm --check tests/lua/self_tables_test.lua
build/host_tools/bin/lua_ui_owner_test
sh tests/lua/style_contract_lint.sh
```

`lua_ui_owner_test` 编译真实 `lua_ui.c` 并用 host LVGL 对象树桩验证跨 owner 拒绝、
对象删除失效、父根容器递归失效、registry 强引用释放及重复清理不会再次 delete 或
再次调用模块 cleanup。
