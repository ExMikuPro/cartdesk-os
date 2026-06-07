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
0 / GPIO0 / D0 -> TIM3 channel 1
1 / GPIO1 / D1 -> TIM3 channel 2
2 / GPIO2 / D2 -> TIM3 channel 3
3 / GPIO3 / D3 -> TIM3 channel 4
4 / GPIO4 / D4 -> TIM2 channel 4
```

Pins 0 through 3 are in the same `shared_group`, so changing frequency on one of them changes the timer frequency for the other TIM3 PWM channels too. Pin 4 uses its own TIM2 group.

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
