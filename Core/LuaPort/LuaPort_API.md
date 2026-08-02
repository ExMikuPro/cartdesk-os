# LuaPort API

LuaPort 负责把 GPIO、PWM、TIM、RNG、CRC、delay 和 UI 绑定注册到固件 Lua VM。
公共脚本契约见 `Docs/lua/lua_api.md` 和 `Docs/LUA_APP_INSTANCE.md`。

## UI 导出

| API | 返回 | 说明 |
|---|---|---|
| `ui.label(config)` | `ui_handle` 或 `nil, error` | 创建文本标签 |
| `ui.button(config)` | `ui_handle` 或 `nil, error` | 创建按钮并可产生输入 |
| `ui.image(config)` | `ui_handle` 或 `nil, error` | 从当前 cart 加载图片 |
| `ui.patch(handle, properties)` | `true` 或 `nil, error` | 修改已登记对象 |

## Handle 与 owner

`lua_ui.c` 定义统一 full userdata 元数据和 owner registry。控件 userdata 的首字段
都是 `lua_ui_handle_t`，其中保存对象指针、Lua VM、owner ID、generation、对象类型
和 alive 状态。Lua 不可取得或伪造其中的 `lv_obj_t *`。

每个应用实例由 `lua_vm.c` 创建独立 owner 和专属根容器。控件构造函数只使用该
根容器作为父对象；系统 screen、Launcher 和 EXIT 控件不会形成 Lua handle。

`LV_EVENT_DELETE` 是统一失效边界。回调清空 object、标记死亡、递增 generation、
释放模块资源并解除 registry 强引用。应用退出按 owner 删除根容器，不依赖 Lua
保存 handle 的 table。

## 属性

- label：创建支持 `id`、`text`、`rect`、`hidden`；patch 支持后三项。
- button：创建额外支持 `input`；patch 支持 `text`、`rect`、`hidden`、`style`。
- button style：`bg`、`bg_alpha`、`text`、`border`、`radius`。
- image：创建支持 `id`、`src`、`rect`、`hidden`、`region`、`style`；patch 支持
  除 `id` 外的相同可变属性。
- image style：`alpha`、`tint`、`flip_x`、`flip_y`。

属性名未知、类型错误、handle 已删除或 owner 不一致均返回 `nil, error`。

## 输入

按钮事件通过 `lua_post_input_for_owner()` 定向投递给创建它的应用。外部硬件输入
仍可调用 `lua_post_input()` 广播给活动实例。`input` 是 action ID；`id` 仅用于
调试和错误信息。

## 图片资源

`ui.image` 通过 `resource_manager` 获取 cart 中的 BGRA8888 图片，并用 LVGL
descriptor 显示。裁剪或翻转视图使用 resource arena scratch，不从 LVGL heap
分配。handle 失效时释放资源引用，场景关闭后由 runtime reset scene arena。
