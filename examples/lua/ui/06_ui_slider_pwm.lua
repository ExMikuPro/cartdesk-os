local PWM_PIN = 0

function init(self)
    self.state = {
        duty = pwm.MIN,
    }

    pwm.setup(PWM_PIN, {
        freq = pwm.DEFAULT_FREQ,
        duty = self.state.duty,
        start = true,
    })

    self.children = ui.slider({
        id = "pwm_duty",
        rect = { 24, 88, 220, 24 },
        range = { 0, pwm.MAX },
        value = self.state.duty,
        input = "pwm_duty",
        style = {
            bg = 0x30343B,
            bg_alpha = 255,
            indicator = 0x2D8CFF,
            indicator_alpha = 255,
            knob = 0xFFFFFF,
            knob_alpha = 255,
        },
    })
end

function on_input(self, action_id, action)
    if action_id == "pwm_duty" and action.event == "changed" then
        self.state.duty = action.value
        pwm.write(PWM_PIN, self.state.duty)
        print("slider pwm", action.event, self.state.duty)
    end
end

function final(self)
    pwm.stop(PWM_PIN)
    pwm.release(PWM_PIN)
end
