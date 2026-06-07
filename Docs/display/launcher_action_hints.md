# Launcher Action Hints

The launcher shows a native LVGL action hint bar in the bottom-right corner of
the 800x480 screen. It uses text-only labels such as `A 起动`, `B 返回`, and
`X 信息`. It is created by `launcher_action_hints_init()` under the launcher
root container, updated when selection changes, and deleted with the launcher
root during `DesignLauncher_Destroy()`.

## Mapping

- Selected cart slot 0: `A 起动`, `B 返回`, `X 信息`.
- `A 起动` is enabled only for the first cart slot when `0:/cart.bin` title
  loading did not fail and Lua is not already running.
- Placeholder app slots keep `A 起动` visible but disabled because no launch
  path exists for those slots yet.
- `X 信息` is enabled only for cart slot 0 because it is the only slot currently
  backed by `0:/cart.bin`; placeholder app slots show the hint disabled.
- Enabled hint text is black. Disabled hint text is gray, so selecting a slot
  without a backing app makes the unavailable actions visibly muted.
- `B 返回` stays visible as the base navigation hint.
- Tapping `A 起动` starts the selected app when it is startable.
- Tapping `X 信息` opens a native launcher popup populated from the selected
  `cart.bin` XHGC v2 header fields: `title`, `title_zh`, `publisher`,
  `version_str`, `entry`, `min_fw`, `cart_id`, plus the overall `cart.bin`
  file size formatted as B, KB, MB, or GB.
- Tapping `B 返回` closes the info popup if it is open; otherwise it leaves the
  launcher state unchanged.
- Favorite hints are hidden because there is no favorite state or KV persistence
  layer yet.

The hint bar reuses its LVGL objects. Selection changes call
`launcher_action_hints_update()` to update text, image source, and enabled state;
objects are not rebuilt every frame.

Touch selection and launch are separated: tapping an app icon once selects it,
and tapping the same selected app icon again starts it. This keeps the launcher
from starting the default-selected first cart slot on the first touch after boot.

## Icon Resources

The hint labels use `lv_font_source_han_sans_sc_16_cjk` because `lv_menu_font`
only contains the launcher circle labels. The wording uses glyphs covered by the
existing CJK font. The hint bar does not use icon assets.

## Manual Test

1. Build and flash the firmware.
2. Start the launcher.
3. Select the first cart slot and confirm the bottom-right hint bar shows
   `A 起动`, `B 返回`, and `X 信息`.
4. Select another placeholder app slot and confirm `A 起动` becomes disabled.
5. Tap `X 信息` and confirm a native popup shows the `cart.bin` header fields.
6. Tap `B 返回` or `OK` and confirm the popup closes.
7. Select a circular system item and confirm only `B 返回` remains visible.
8. Start the first cart slot from `A 起动` and confirm the existing Lua launch flow still works.
9. Exit the runtime screen and confirm the launcher recreates the hint bar
   without a crash.

This feature does not implement KV storage, does not restore SD Lua APIs, and
does not add any filesystem-facing Lua API.
