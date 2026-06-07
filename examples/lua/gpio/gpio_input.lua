local EXT_IN = 5
local EXT_OUT = 6

function init(self)
    gpio.setup(EXT_IN, gpio.INPUT_PULLUP)
    gpio.setup(EXT_OUT, gpio.OUTPUT)
    gpio.write(EXT_OUT, gpio.LOW)
end

function update(self, dt)
    local value = gpio.read(EXT_IN)
    gpio.write(EXT_OUT, value)
end

function final(self)
    gpio.write(EXT_OUT, gpio.LOW)
    gpio.release(EXT_IN)
    gpio.release(EXT_OUT)
end
