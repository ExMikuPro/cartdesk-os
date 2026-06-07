local PWM_PIN = 0
local STEP_TIME = 0.005

function init(self)
    self.duty = 0
    self.direction = 1
    self.accumulator = 0

    pwm.setup(PWM_PIN, 1000)
    pwm.write(PWM_PIN, self.duty)
end

function update(self, dt)
    self.accumulator = self.accumulator + dt

    while self.accumulator >= STEP_TIME do
        self.accumulator = self.accumulator - STEP_TIME
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
    pwm.release(PWM_PIN)
end
