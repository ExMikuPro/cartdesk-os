local LED_PIN = 3
local BLINK_INTERVAL = 0.5

function init(self)
    self.elapsed = 0
    self.level = gpio.LOW

    gpio.setup(LED_PIN, {
        mode = gpio.OUTPUT,
        initial = self.level,
        speed = gpio.SPEED_LOW,
    })
end

function update(self, dt)
    self.elapsed = self.elapsed + dt

    if self.elapsed >= BLINK_INTERVAL then
        self.elapsed = self.elapsed - BLINK_INTERVAL
        self.level = self.level == gpio.LOW and gpio.HIGH or gpio.LOW
        gpio.write(LED_PIN, self.level)
    end
end

function final(self)
    gpio.write(LED_PIN, gpio.LOW)
    gpio.release(LED_PIN)
end
