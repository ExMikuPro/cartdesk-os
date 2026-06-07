local PWM_PIN = 0
local STEP_INTERVAL = 0.02

function init(self)
    self.state = {
        elapsed = 0,
        duty = pwm.MIN,
        direction = 1,
    }

    pwm.setup(PWM_PIN, {
        freq = pwm.DEFAULT_FREQ,
        duty = self.state.duty,
        start = true,
    })
end

function update(self, dt)
    local s = self.state

    s.elapsed = s.elapsed + dt

    while s.elapsed >= STEP_INTERVAL do
        s.elapsed = s.elapsed - STEP_INTERVAL
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
    pwm.stop(PWM_PIN)
    pwm.release(PWM_PIN)
end
