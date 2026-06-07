local PWM_PIN = 0
local STEP_INTERVAL = 0.02

function init(self)
    self.elapsed = 0
    self.duty = pwm.MIN
    self.direction = 1

    pwm.setup(PWM_PIN, {
        freq = pwm.DEFAULT_FREQ,
        duty = self.duty,
        start = true,
    })
end

function update(self, dt)
    self.elapsed = self.elapsed + dt

    while self.elapsed >= STEP_INTERVAL do
        self.elapsed = self.elapsed - STEP_INTERVAL
        self.duty = self.duty + self.direction

        if self.duty >= pwm.MAX then
            self.duty = pwm.MAX
            self.direction = -1
        elseif self.duty <= pwm.MIN then
            self.duty = pwm.MIN
            self.direction = 1
        end
    end

    pwm.write(PWM_PIN, self.duty)
end

function final(self)
    pwm.stop(PWM_PIN)
    pwm.release(PWM_PIN)
end
