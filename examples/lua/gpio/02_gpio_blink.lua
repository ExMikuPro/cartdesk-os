local LED_PIN = 3
local BLINK_INTERVAL = 0.5

function init(self)
    self.state = {
        elapsed = 0,
        level = gpio.LOW,
    }

    gpio.setup(LED_PIN, {
        mode = gpio.OUTPUT,
        initial = self.state.level,
        speed = gpio.SPEED_LOW,
    })
end

function update(self, dt)
    local s = self.state

    s.elapsed = s.elapsed + dt

    if s.elapsed >= BLINK_INTERVAL then
        s.elapsed = s.elapsed - BLINK_INTERVAL
        s.level = s.level == gpio.LOW and gpio.HIGH or gpio.LOW
        gpio.write(LED_PIN, s.level)
    end
end

function final(self)
    gpio.write(LED_PIN, gpio.LOW)
    gpio.release(LED_PIN)
end
