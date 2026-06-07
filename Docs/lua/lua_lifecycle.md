# Lua Script Lifecycle

The Lua runtime uses Defold-style optional callbacks. Each loaded script gets
its own environment and registry-owned `self` table.

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

Missing callbacks are skipped. Callback errors are logged and do not leave
values on the main Lua stack.

## Order

When a script is loaded:

```text
init(self)
```

For each frame:

```text
queued on_input callbacks
fixed_update(self, 1 / 60) zero to five times
update(self, dt)
late_update(self, dt)
queued on_message callbacks
```

When the runtime is stopped:

```text
final(self)
```

`final()` runs at most once and only for instances whose `init()` completed.

## Instance State

State stored in `self.state` remains available for the complete script
lifetime. UI stored in `self.children` is deleted by the host after
`final(self)` returns.

```lua
function init(self)
    self.state = {
        elapsed = 0,
    }
end

function update(self, dt)
    self.state.elapsed = self.state.elapsed + dt
end
```

At the top level of `self`, new scripts should only use `self.state` and
`self.children`. Fixed pins and constants should be file-local variables;
temporary values should be function-local variables.

The runtime supports up to four script instances by default. The limit can be
changed with `LUA_RT_MAX_INSTANCES`.

## Input

C code queues input with:

```c
LuaInputAction action = {
    .event = "pressed",
    .pressed = true,
    .value = 1.0f,
};
lua_post_input("a", &action);
```

The Lua `action` table contains `event`, `pressed`, `released`, `repeated`,
`value`, `x`, `y`, `dx`, and `dy`. UI widgets post LVGL input events through
this same queue. Buttons and sliders use their config `input` value as
`action_id`, default to `"button"` / `"slider"`, and are owned by
`self.children`. The queue has fixed capacity and the API is intended for task
context, not direct ISR use.

## Messages

C code queues a lightweight message with:

```c
lua_post_message("game_over", "system");
```

The current message payload is `nil`; `message_id` and `sender` are strings.
Messages are dispatched after `late_update()` and never recursively.

## Reload And Shutdown

`lua_reload()` reloads file, cartridge, and embedded script instances while
preserving each `self` table. It then calls `on_reload(self)`. Raw bytecode
instances cannot be reloaded because their original byte buffer is not kept.

`lua_shutdown()` calls `final(self)` once, releases all registry references,
and closes the Lua state. `Task_LUA_Stop()` requests this through the Lua task.

## Compatibility

Legacy scripts using `start()` and `update(dt)` are detected when `init()` is
absent. New scripts should use the Defold-style signatures.
