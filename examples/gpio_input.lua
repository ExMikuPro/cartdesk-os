local EXT_IN = 5
local EXT_OUT = 6

gpio.pinMode(EXT_IN, gpio.INPUT_PULLUP)
gpio.pinMode(EXT_OUT, gpio.OUTPUT)

while true do
    local value = gpio.digitalRead(EXT_IN)
    gpio.digitalWrite(EXT_OUT, value)
    delay(10)
end
