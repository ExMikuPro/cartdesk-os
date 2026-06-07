local PWM_PIN = 0

function init(self)
    pwm.setup(PWM_PIN, {
        freq = pwm.DEFAULT_FREQ,
        duty = pwm.MIN,
        start = true,
    })

    local screen = ui.slider.get_screen()
    self.slider = ui.slider.draw(screen, 24, 88, 220, 24)
    self.slider:set_range(0, pwm.MAX)
    self.slider:set_value(pwm.MIN)
    self.slider:set_style_bg_color(0x30343B, 255)
    self.slider:set_style_indicator_color(0x2D8CFF, 255)
    self.slider:set_style_knob_color(0xFFFFFF, 255)
    self.slider:set_callback(function(slider, event)
        local duty = self.slider:get_value()
        pwm.write(PWM_PIN, duty)
        print("slider pwm", event, duty)
    end)
end

function final(self)
    if self.slider then
        self.slider:delete()
        self.slider = nil
    end

    pwm.stop(PWM_PIN)
    pwm.release(PWM_PIN)
end
