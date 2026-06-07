local PWM_PIN = 1

function init(self)
    pwm.setup(PWM_PIN, 1000)
    pwm.write(PWM_PIN, 128)

    delay(300)
    pwm.setFreq(PWM_PIN, 2000)

    delay(300)
    pwm.setFreq(PWM_PIN, 4000)

    delay(300)
    pwm.stop(PWM_PIN)
end

function final(self)
    pwm.release(PWM_PIN)
end
