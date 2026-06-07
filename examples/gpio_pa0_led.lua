local BUTTON_PIN = 5
local LED_PIN = 3

gpio.pinMode(BUTTON_PIN, gpio.INPUT_PULLUP)
gpio.pinMode(LED_PIN, gpio.OUTPUT)

while true do
    local pressed = gpio.digitalRead(BUTTON_PIN) == gpio.LOW
    gpio.digitalWrite(LED_PIN, pressed and gpio.HIGH or gpio.LOW)
    delay(10)
end
