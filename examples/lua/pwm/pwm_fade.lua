local PWM_PIN = 0
local STEP_TIME = 0.005

function init(self)
    self.state = {
        duty = pwm.MIN,
        direction = 1,
        accumulator = 0,
    }

    pwm.setup(PWM_PIN, 1000)
    pwm.write(PWM_PIN, self.state.duty)
end

function update(self, dt)
    local s = self.state

    s.accumulator = s.accumulator + dt

    while s.accumulator >= STEP_TIME do
        s.accumulator = s.accumulator - STEP_TIME
        s.duty = s.duty + s.direction

        if s.duty >= pwm.MAX then
            s.duty = pwm.MAX
            s.direction = -1
        elseif s.duty <= pwm.MIN then
            s.duty = pwm.MIN
            s.direction = 1
        end
    end

    pwm.write(PWM_PIN, s.duty)
end

function final(self)
    pwm.release(PWM_PIN)
end
