# Lua PWM API

`pwm` is a simplified Arduino `analogWrite()` style PWM module. It keeps the API under the `pwm` namespace and does not add global `analogWrite()` helpers.

Lua scripts use logical pin ids only:

```lua
pwm.write(0, 128)
pwm.write("GPIO0", 128)
pwm.write("D0", 128)
```

The script does not need to know the real GPIO port, pin, timer, or channel.

## Pins

Current PWM-capable logical pins:

```text
0 / GPIO0 / D0
1 / GPIO1 / D1
2 / GPIO2 / D2
3 / GPIO3 / D3
4 / GPIO4 / D4
```

The real GPIO port, pin, timer, and channel mapping is board-specific and
intentionally hidden from Lua scripts.

Some PWM-capable logical pins share the same underlying PWM frequency source.
These pins have the same `shared_group` value.

Changing the frequency of one pin changes the frequency for all configured PWM
pins in the same `shared_group`:

```lua
local a = pwm.info(0)
local b = pwm.info(1)

if a.shared_group == b.shared_group then
    print("GPIO0 and GPIO1 share the same PWM frequency source")
end
```

## Constants

```lua
pwm.MIN = 0
pwm.MAX = 255
pwm.DEFAULT_FREQ = 1000
```

Duty uses the `0..255` range:

```lua
pwm.write(0, 0)    -- 0%
pwm.write(0, 128)  -- about 50%
pwm.write(0, 255)  -- 100%
```

## API

```lua
pwm.count()
pwm.list()
pwm.info(pin)

pwm.setup(pin, freq)
pwm.setup(pin, { freq = 1000, duty = 128, start = true })

pwm.write(pin, value)
pwm.read(pin)

pwm.setFreq(pin, freq)
pwm.getFreq(pin)

pwm.stop(pin)
pwm.release(pin)
```

`pwm.write(pin, value)` automatically configures the pin at `pwm.DEFAULT_FREQ` if it has not been configured yet.

`pwm.count()` returns the number of PWM-capable logical pins, not the total
number of GPIO pins.

For `pwm.setup(pin, config)`, if `start` is omitted or false:

- `configured = true`
- `running = false`
- duty is stored but PWM output is not started

`pwm.setup(pin, freq)` follows the same default and configures the pin without
starting PWM output.

If `start` is true:

- `configured = true`
- `running = true`
- PWM output starts with the configured duty

`pwm.write(pin, value)` starts PWM output if the pin is configured but not
running.

`pwm.write(pin, 0)`:

- sets duty to 0%
- keeps the PWM configuration
- keeps pin ownership as PWM
- does not release the pin
- makes `pwm.read(pin)` return `0`

`pwm.stop(pin)`:

- stops PWM output
- keeps the PWM configuration
- keeps the current frequency and duty value
- keeps pin ownership as PWM
- sets `running = false`

Calling `pwm.write(pin, value)` after `pwm.stop(pin)` updates duty and starts
PWM output again.

`pwm.release(pin)`:

- stops PWM output
- clears PWM configuration
- releases pin ownership
- restores the pin to a safe analog/high-impedance state
- allows the same logical pin to be used by GPIO again

Failures return:

```lua
nil, "error message"
```

Successful mutating calls return:

```lua
true
```

## Info

```lua
local info = pwm.info(0)
print(info.name)
print(info.pwm)
print(info.shared_group)
print(info.configured)
print(info.running)
print(info.freq)
print(info.duty)
```

`pwm.list()` returns the same style of table for every PWM-capable logical pin.

## GPIO Ownership

A logical pin cannot be used as GPIO and PWM at the same time.

```lua
gpio.setup(0, gpio.OUTPUT)
local ok, err = pwm.write(0, 128) -- nil, "pin is already used by gpio"
gpio.release(0)
pwm.write(0, 128)
```

Use `pwm.release(pin)` when switching a PWM pin back to GPIO. It stops PWM, releases ownership, and restores the pin to a safe analog state.

## Examples

Simple half-duty output:

```lua
pwm.write(0, 128)
```

Fade:

```lua
pwm.setup(0, 1000)

while true do
    for i = 0, 255 do
        pwm.write(0, i)
        delay.ms(5)
    end

    for i = 255, 0, -1 do
        pwm.write(0, i)
        delay.ms(5)
    end
end
```

Change frequency:

```lua
pwm.setup(1, 1000)
pwm.write(1, 128)
delay.ms(300)

pwm.setFreq(1, 2000)
delay.ms(300)

pwm.setFreq(1, 4000)
delay.ms(300)

pwm.stop(1)
```
