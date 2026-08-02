# Lua 运行时契约

## 实例与生命周期

每个应用实例拥有独立 `_ENV`、`self`、生命周期协程和 UI owner。生命周期顺序为：

1. 创建实例与 `self` 五个节点。
2. `init(self)`。
3. 输入、`fixed_update`、`update`、`late_update`、消息。
4. 热重载时执行 `on_reload(self)`。
5. 关闭时执行 `final(self)`，随后宿主兜底清理。

## self 节点

| 节点 | 职责 |
|---|---|
| `self.state` | 应用业务状态 |
| `self.ui` | UI handle 引用 |
| `self.assets` | 资源路径和资源 handle |
| `self.timers` | 保存 `timer.after/every` 返回的 timer handle |
| `self.services` | 异步服务状态；当前只提供空 table |

应用不需要初始化这些 table。`self.ui` 仅保存 Lua 引用，不决定底层对象所有权；
清空或替换它不会影响宿主最终清理。

## UI 契约

当前仅导出 `ui.label(config)`、`ui.button(config)`、`ui.image(config)` 和
`ui.patch(handle, properties)`。创建成功返回不可伪造的 full userdata；失败返回
`nil, error`。所有对象只能创建在当前应用专属根容器中。

handle 记录 VM、owner、generation、对象类型与存活状态。`ui.patch` 会依次检查
userdata、当前 owner、generation、alive 和属性 table。对象或父容器收到
`LV_EVENT_DELETE` 后，handle 立即失效；后续访问只返回错误，不接触旧 `lv_obj_t`。

`id` 仅用于日志、Inspector、测试和错误定位，不参与对象查询。`input` 仅决定
`on_input` 的 `action_id`，与 Lua table 字段名及 `id` 没有强制相等关系。

## 图片资源

`ui.image` 的 `src` 是当前 cart 内部路径。图片经 `resource_manager` 同步加载；当前
支持 BGRA8888，支持 `rect`、`hidden`、`src`、`region`，以及图片 `style` 的
`alpha`、`tint`、`flip_x`、`flip_y`。更新 `src` 仍通过同一 handle 完成。

图片 handle 失效时释放资源引用。场景关闭后宿主统一重置 resource arena；Lua
无法访问 cart offset、SDRAM 地址、LVGL descriptor 或裸资源指针。

## 错误与关闭

未知属性、属性类型错误、跨应用 handle 和已删除 handle 均返回 `nil, error`。
关闭顺序是停止派发、停止更新、执行 `final`、销毁 owner 根容器、失效 handle、
释放 registry 引用、重置资源 arena、销毁 VM。重复清理通过 `alive` 和 owner 状态
保护，不会再次删除同一 LVGL 对象或重复释放图片资源。
