# CartDesk-OS 系统图标

## 1. 当前用途

系统图标资源目前只用于 Launcher 的五个系统圆形入口。它们只提供图标显示和选择态反馈，不实现相册、手柄、拓展、设置或休眠模式的业务页面。

## 2. 图标来源

图标来自 [Tabler Icons](https://github.com/tabler/tabler-icons) 的 Outline 集合，许可证为 MIT License。仓库只保留本功能使用的五个原始 SVG；许可证原文和资源清单位于 `Core/Screen/Assets/Icons/Tabler/`。

## 3. 当前图标映射

| 系统入口 | 图标 ID | Tabler 名称 | Launcher 光学校正 |
|---|---|---|---|
| 相册 | `CART_SYSTEM_ICON_GALLERY` | `library-photo` | `(0, 0)` |
| 手柄 | `CART_SYSTEM_ICON_GAMEPAD` | `device-gamepad-2` | `(-1, -1)` |
| 拓展 | `CART_SYSTEM_ICON_EXTENSIONS` | `puzzle` | `(+2, -2)` |
| 设置 | `CART_SYSTEM_ICON_SETTINGS` | `settings` | `(-1, -1)` |
| 休眠模式 | `CART_SYSTEM_ICON_SLEEP` | `moon` | `(0, 0)` |

所有图标保持相同的 40×40 画布和缩放比例。`ui_screen_launcher.c` 的入口表只对非对称线条图形施加少量像素级光学校正；正值向右/向下，负值向左/向上。该校正只适用于当前 56×56 Launcher 圆形入口。

## 4. 资源流程

```text
Tabler Outline SVG
    -> tools/ui/generate_system_icons.py 离线转换
    -> 40x40 LVGL A8 C 数据
    -> cart_system_icon_id_t 映射
    -> Launcher lv_image
```

固件不解析 SVG，也不从文件系统动态加载系统图标。A8 数据是静态 `const` 资源，主要占用内部 Flash。

## 5. 使用方法

页面只依赖统一图标 ID，不引用 SVG 路径或生成数据描述符：

```c
const lv_image_dsc_t *source =
    CartSystemIcon_GetSource(CART_SYSTEM_ICON_GALLERY);

if (source != NULL) {
    lv_obj_t *icon = lv_image_create(parent);
    lv_image_set_src(icon, source);
    lv_obj_set_style_image_recolor(icon, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
}
```

`CartSystemIcon_GetSource()` 和 `CartSystemIcon_GetDebugName()` 对无效 ID 返回 `NULL`。

## 6. 颜色和状态

A8 资源只保存每个像素的覆盖率，不固化主题颜色。Launcher 在 `prv_set_selection()` 中使用现有 `COLOR_BLACK` 作为普通色，使用现有 `COLOR_CYAN` 作为选中色；选中态不会改变图标尺寸或布局。

## 7. 重新生成

开发机需要安装 ImageMagick，并让 `convert` 位于 `PATH`。生成过程不联网，也不会自动安装依赖：

```bash
python3 tools/ui/generate_system_icons.py
```

脚本读取仓库内固定的五个 SVG，并稳定覆盖：

- `Core/Screen/Assets/cart_system_icons_data.h`
- `Core/Screen/Assets/cart_system_icons_data.c`

生成结果已提交到仓库，普通固件构建不需要运行生成脚本。

## 8. 添加新图标

1. 从 Tabler Icons 选择 Outline SVG。
2. 将原始 SVG 保存到 `Core/Screen/Assets/Icons/Tabler/`。
3. 更新该目录的 `README.md` 资源清单；许可证不变时无需复制第二份许可证。
4. 在 `cart_system_icons.h` 增加图标 ID。
5. 在 `tools/ui/generate_system_icons.py` 的固定清单中增加资源并运行脚本。
6. 在 `cart_system_icons.c` 更新 ID、描述符和调试名称映射。
7. 更新使用页面和本文档，并执行 `cmake --build --preset Release`。

## 9. 限制

- 当前只包含五个 Launcher 系统入口图标。
- 不包含 Cart 应用程序图标。
- 不在运行时解析 SVG。
- 不实现五个入口对应的业务页面。
- 40 px 图标在 56 px 圆形入口中的最终视觉间距仍需实机确认。
