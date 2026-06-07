# Lua GPIO API

`gpio` provides Arduino-style digital GPIO access through logical pin ids.
Lua scripts do not need to know the STM32 GPIO port or pin mask.

## Logical Pins

```text
0 / GPIO0 / D0
1 / GPIO1 / D1
2 / GPIO2 / D2
3 / GPIO3 / D3
4 / GPIO4 / D4
```

The physical GPIO port and pin mapping is board-specific and intentionally
hidden from application scripts. Application scripts should use only logical
ids or logical names:

```lua
gpio.setup(0, gpio.OUTPUT)
gpio.setup("GPIO1", gpio.INPUT_PULLUP)
gpio.write("D2", gpio.HIGH)
```

Raw physical pin names are not accepted.

## Constants

Levels:

```lua
gpio.LOW
gpio.HIGH
```

Modes:

```lua
gpio.INPUT
gpio.INPUT_PULLUP
gpio.INPUT_PULLDOWN
gpio.OUTPUT
gpio.OUTPUT_OPEN_DRAIN
gpio.ANALOG
```

`gpio.ANALOG` configures the pin into analog/high-impedance safe mode. It does
not read analog values.

Analog input should be handled by a separate `adc` module in the future. Do not
use `gpio.read()` to read analog voltage.

Speeds:

```lua
gpio.SPEED_LOW
gpio.SPEED_MEDIUM
gpio.SPEED_HIGH
```

Interrupt edge constants are reserved for future interrupt support:

```lua
gpio.RISING
gpio.FALLING
gpio.CHANGE
gpio.LOW_LEVEL
gpio.HIGH_LEVEL
```

## Setup

Simple mode:

```lua
gpio.setup(0, gpio.OUTPUT)
gpio.setup(1, gpio.INPUT_PULLUP)
gpio.setup(2, gpio.INPUT_PULLDOWN)
gpio.setup(3, gpio.OUTPUT_OPEN_DRAIN)
```

Table configuration:

```lua
gpio.setup(0, {
    mode = gpio.OUTPUT,
    initial = gpio.LOW,
    speed = gpio.SPEED_LOW
})

gpio.setup(1, {
    mode = gpio.INPUT_PULLUP
})
```

Optional `pull` values are `"none"`, `"up"`, and `"down"`.

## Read And Write

```lua
gpio.write(0, gpio.HIGH)
gpio.write(0, gpio.LOW)
gpio.write(0, 1)
gpio.write(0, true)

local level = gpio.read(0)
```

`gpio.read()` returns the integer `gpio.LOW` (`0`) or `gpio.HIGH` (`1`).

GPIO does not auto-configure pins on read or write.

`gpio.write(pin, value)` requires the pin to have been configured with
`gpio.setup(pin, gpio.OUTPUT)` or
`gpio.setup(pin, gpio.OUTPUT_OPEN_DRAIN)`. If the pin is not configured for
output, it returns:

```lua
nil, "gpio is not configured as output"
```

`gpio.read(pin)` requires the pin to have been configured with `gpio.setup()`.
Reading from an input or output pin is allowed. If the pin is not configured,
it returns:

```lua
nil, "gpio is not configured"
```

Toggle the current output:

```lua
gpio.toggle(0)
```

## Query Pins

```lua
local count = gpio.count()
local pins = gpio.list()
local info = gpio.info(0)
```

`gpio.info(pin)` returns capability information without exposing the physical
port or pin:

```lua
{
    id = 0,
    name = "GPIO0",
    input = true,
    output = true,
    pullup = true,
    pulldown = true,
    irq = false,
    adc = false,
    pwm = true,
    open_drain = true,
    reserved = false
}
```

## Release

```lua
gpio.release(0)
```

Release restores the pin to analog/no-pull safe state and clears its GPIO
ownership. Release a GPIO before using the same logical pin as PWM:

```lua
gpio.setup(0, gpio.OUTPUT)
gpio.release(0)
pwm.write(0, 128)
```

GPIO and PWM cannot own the same logical pin at the same time. Use
`pwm.release(pin)` before switching a PWM pin back to GPIO.

## Arduino Aliases

The following global compatibility functions are available:

```lua
pinMode(pin, mode)
digitalRead(pin)
digitalWrite(pin, value)
```

The namespaced `gpio.*` API is preferred for new scripts. Arduino aliases are
compatibility helpers and may be disabled by firmware configuration.

## Interrupts

The API entry points exist:

```lua
gpio.on(pin, gpio.FALLING, callback)
gpio.off(pin)
```

GPIO interrupt dispatch is not implemented yet. Both calls currently return:

```lua
nil, "gpio interrupt not supported"
```

## Example

```lua
local pin = 0

gpio.setup(pin, {
    mode = gpio.OUTPUT,
    initial = gpio.LOW,
    speed = gpio.SPEED_LOW
})

while true do
    gpio.toggle(pin)
    delay.ms(500)
end
```
