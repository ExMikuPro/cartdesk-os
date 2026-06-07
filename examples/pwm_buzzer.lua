local BUZZER_PIN = 1

function init(self)
    pwm.setup(BUZZER_PIN, 2000)
    pwm.write(BUZZER_PIN, 128)

    delay(200)
    pwm.stop(BUZZER_PIN)
end

function final(self)
    pwm.release(BUZZER_PIN)
end
