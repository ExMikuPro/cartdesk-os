local BUTTON_PIN = 5
local LED_PIN = 3

function init(self)
    gpio.pinMode(BUTTON_PIN, gpio.INPUT_PULLUP)
    gpio.pinMode(LED_PIN, gpio.OUTPUT)
    gpio.digitalWrite(LED_PIN, gpio.LOW)
end

function update(self, dt)
    local pressed = gpio.digitalRead(BUTTON_PIN) == gpio.LOW
    gpio.digitalWrite(LED_PIN, pressed and gpio.HIGH or gpio.LOW)
end

function final(self)
    gpio.digitalWrite(LED_PIN, gpio.LOW)
    gpio.release(BUTTON_PIN)
    gpio.release(LED_PIN)
end
