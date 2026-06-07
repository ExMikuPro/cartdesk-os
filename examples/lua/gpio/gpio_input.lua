local EXT_IN = 5
local EXT_OUT = 6

function init(self)
    gpio.pinMode(EXT_IN, gpio.INPUT_PULLUP)
    gpio.pinMode(EXT_OUT, gpio.OUTPUT)
    gpio.digitalWrite(EXT_OUT, gpio.LOW)
end

function update(self, dt)
    local value = gpio.digitalRead(EXT_IN)
    gpio.digitalWrite(EXT_OUT, value)
end

function final(self)
    gpio.digitalWrite(EXT_OUT, gpio.LOW)
    gpio.release(EXT_IN)
    gpio.release(EXT_OUT)
end
