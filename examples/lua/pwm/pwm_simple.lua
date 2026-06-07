local PWM_PIN = 0

function init(self)
    pwm.write(PWM_PIN, 128)
end

function final(self)
    pwm.release(PWM_PIN)
end
