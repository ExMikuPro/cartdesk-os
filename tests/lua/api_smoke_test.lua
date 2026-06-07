local function require_api(path, value)
    if value == nil then
        error("missing api: " .. path)
    end
end

local function require_type(path, value, expected)
    require_api(path, value)
    if type(value) ~= expected then
        error("missing api: " .. path)
    end
end

require_api("delay", delay)
require_type("delay.ms", delay.ms, "function")
require_type("pinMode", pinMode, "function")
require_type("digitalRead", digitalRead, "function")
require_type("digitalWrite", digitalWrite, "function")

require_type("gpio", gpio, "table")
require_type("gpio.count", gpio.count, "function")
require_type("gpio.list", gpio.list, "function")
require_type("gpio.info", gpio.info, "function")
require_type("gpio.setup", gpio.setup, "function")
require_type("gpio.read", gpio.read, "function")
require_type("gpio.write", gpio.write, "function")
require_type("gpio.toggle", gpio.toggle, "function")
require_type("gpio.release", gpio.release, "function")
require_type("gpio.pinMode", gpio.pinMode, "function")
require_type("gpio.digitalRead", gpio.digitalRead, "function")
require_type("gpio.digitalWrite", gpio.digitalWrite, "function")
require_type("gpio.on", gpio.on, "function")
require_type("gpio.off", gpio.off, "function")

require_api("gpio.LOW", gpio.LOW)
require_api("gpio.HIGH", gpio.HIGH)
require_api("gpio.OUTPUT", gpio.OUTPUT)
require_api("gpio.INPUT", gpio.INPUT)
require_api("gpio.INPUT_PULLUP", gpio.INPUT_PULLUP)
require_api("gpio.INPUT_PULLDOWN", gpio.INPUT_PULLDOWN)
require_api("gpio.OUTPUT_OPEN_DRAIN", gpio.OUTPUT_OPEN_DRAIN)
require_api("gpio.ANALOG", gpio.ANALOG)
require_api("gpio.RISING", gpio.RISING)
require_api("gpio.FALLING", gpio.FALLING)
require_api("gpio.CHANGE", gpio.CHANGE)
require_api("gpio.LOW_LEVEL", gpio.LOW_LEVEL)
require_api("gpio.HIGH_LEVEL", gpio.HIGH_LEVEL)
require_api("gpio.SPEED_LOW", gpio.SPEED_LOW)
require_api("gpio.SPEED_MEDIUM", gpio.SPEED_MEDIUM)
require_api("gpio.SPEED_HIGH", gpio.SPEED_HIGH)

require_type("pwm", pwm, "table")
require_type("pwm.count", pwm.count, "function")
require_type("pwm.list", pwm.list, "function")
require_type("pwm.info", pwm.info, "function")
require_type("pwm.setup", pwm.setup, "function")
require_type("pwm.write", pwm.write, "function")
require_type("pwm.read", pwm.read, "function")
require_type("pwm.set_freq", pwm.set_freq, "function")
require_type("pwm.get_freq", pwm.get_freq, "function")
require_type("pwm.setFreq", pwm.setFreq, "function")
require_type("pwm.getFreq", pwm.getFreq, "function")
require_type("pwm.stop", pwm.stop, "function")
require_type("pwm.release", pwm.release, "function")
require_api("pwm.MIN", pwm.MIN)
require_api("pwm.MAX", pwm.MAX)
require_api("pwm.DEFAULT_FREQ", pwm.DEFAULT_FREQ)
require_api("pwm.POLARITY_HIGH", pwm.POLARITY_HIGH)
require_api("pwm.POLARITY_LOW", pwm.POLARITY_LOW)

require_type("tim", tim, "table")
require_type("tim.us", tim.us, "function")
require_type("tim.delay_us", tim.delay_us, "function")

require_type("sd", sd, "table")
require_type("sd.open", sd.open, "function")
require_type("sd.close", sd.close, "function")
require_type("sd.write", sd.write, "function")
require_type("sd.wirte", sd.wirte, "function")
require_type("sd.read", sd.read, "function")
require_type("sd.seek", sd.seek, "function")
require_type("sd.size", sd.size, "function")
require_type("sd.mount", sd.mount, "function")
require_type("sd.umount", sd.umount, "function")

require_type("rng", rng, "table")
require_type("rng.u32", rng.u32, "function")
require_type("rng.bytes", rng.bytes, "function")

require_type("crc", crc, "table")
require_type("crc.crc32", crc.crc32, "function")
require_type("crc.crc32_hex", crc.crc32_hex, "function")

require_type("ui", ui, "table")
require_type("ui.button", ui.button, "table")
require_type("ui.button.create", ui.button.create, "function")
require_type("ui.button.draw", ui.button.draw, "function")
require_type("ui.button.get_screen", ui.button.get_screen, "function")

require_type("ui.slider", ui.slider, "table")
require_type("ui.slider.create", ui.slider.create, "function")
require_type("ui.slider.draw", ui.slider.draw, "function")
require_type("ui.slider.get_screen", ui.slider.get_screen, "function")

require_type("_G", _G, "table")
require_type("coroutine", coroutine, "table")
require_type("table", table, "table")
require_type("string", string, "table")
require_type("math", math, "table")
require_type("utf8", utf8, "table")

if io ~= nil then
    error("missing api: io should be disabled")
end
if os ~= nil then
    error("missing api: os should be disabled")
end
if debug ~= nil then
    error("missing api: debug should be disabled")
end
if package ~= nil then
    error("missing api: package should be disabled")
end

print("api smoke test passed")
