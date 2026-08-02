# Lua Examples

Lua examples are grouped by API area:

- `basic/`: lifecycle and runtime basics.
- `gpio/`: GPIO input/output examples.
- `pwm/`: PWM output examples.
- `ui/`: LVGL UI binding examples.
- `ui_state_example.lua`: complete `self` five-node and UI handle example.

The numbered examples are the recommended starting sequence for new scripts.
The host creates `self.state`, `self.ui`, `self.assets`, `self.timers`, and
`self.services` before `init(self)`.
