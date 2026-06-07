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
