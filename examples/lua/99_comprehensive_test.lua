local LED_PIN = 3
local BUTTON_PIN = 5
local PWM_PIN = 0

local BLINK_INTERVAL = 0.5
local PWM_STEP_INTERVAL = 0.02
local LOG_INTERVAL = 1.0
local STATUS_INTERVAL = 2.0

local function safe_call(label, fn)
    local ok, a, b = pcall(fn)
    if ok then
        return a, b
    end
    print(label .. " failed", a)
    return nil, a
end

local function report_result(label, value, err)
    if value == nil or value == false then
        print(label .. " failed", err)
        return false
    end

    print(label .. " ok")
    return true
end

local function init_hardware(self)
    report_result("gpio setup button", gpio.setup(BUTTON_PIN, gpio.INPUT_PULLUP))
    report_result("gpio setup led", gpio.setup(LED_PIN, {
        mode = gpio.OUTPUT,
        initial = self.state.level,
        speed = gpio.SPEED_LOW,
    }))

    local gpio_count = gpio.count()
    print("gpio count", gpio_count)
    local led_info = gpio.info(LED_PIN)
    if led_info then
        print("gpio led", led_info.id, led_info.name, led_info.output)
    end

    report_result("pwm setup", pwm.setup(PWM_PIN, {
        freq = pwm.DEFAULT_FREQ,
        duty = self.state.duty,
        start = true,
    }))

    local pwm_count = pwm.count()
    print("pwm count", pwm_count)
    local pwm_info = pwm.info(PWM_PIN)
    if pwm_info then
        print("pwm info", pwm_info.id, pwm_info.name, pwm_info.freq, pwm_info.duty)
    end

    local freq, err = pwm.get_freq(PWM_PIN)
    if freq then
        print("pwm freq", freq)
    else
        print("pwm get_freq failed", err)
    end
end

local function init_random_and_crc(self)
    local n, err = rng.u32()
    if n then
        self.state.seed = n
        print("rng u32", n)
    else
        print("rng u32 failed", err)
    end

    local bytes
    bytes, err = rng.bytes(8)
    if bytes then
        print("rng bytes len", #bytes)
    else
        print("rng bytes failed", err)
    end

    local crc_value
    crc_value, err = crc.crc32_hex("hello")
    if crc_value then
        print("crc32 hello", crc_value)
    else
        print("crc32 failed", err)
    end
end

local function init_ui(self)
    self.ui.run = ui.button({
        id = "run",
        text = "Run",
        rect = { 24, 24, 120, 48 },
        input = "run",
    })
    self.ui.duty = ui.label({
        id = "duty",
        text = "Duty: " .. self.state.duty,
        rect = { 24, 92, 220, 32 },
    })

    local ok, err = ui.patch(
        self.ui.run, { text = "Ready" })
    report_result("ui patch button", ok, err)
end

function init(self)
    self.state.elapsed = 0
    self.state.fixed_count = 0
    self.state.update_count = 0
    self.state.late_count = 0
    self.state.log_elapsed = 0
    self.state.status_elapsed = 0
    self.state.level = gpio.LOW
    self.state.duty = pwm.MIN
    self.state.direction = 1
    self.state.seed = 0
    self.state.input_count = 0
    self.state.resources = {}
    self.state.start_us = tim.us()

    print("comprehensive init")
    init_hardware(self)
    init_random_and_crc(self)
    init_ui(self)

    local t0 = tim.us()
    tim.delay_us(20)
    print("tim delay_us cost", tim.us() - t0)

    delay.ms(10)
    print("delay ok")
end

function fixed_update(self, dt)
    self.state.fixed_count = self.state.fixed_count + 1
end

function update(self, dt)
    local s = self.state

    s.update_count = s.update_count + 1
    s.elapsed = s.elapsed + dt
    s.log_elapsed = s.log_elapsed + dt
    s.status_elapsed = s.status_elapsed + dt

    if s.elapsed >= BLINK_INTERVAL then
        s.elapsed = s.elapsed - BLINK_INTERVAL
        s.level = s.level == gpio.LOW and gpio.HIGH or gpio.LOW
        gpio.write(LED_PIN, s.level)
    end

    local pressed = gpio.read(BUTTON_PIN) == gpio.LOW
    if pressed ~= s.button_pressed then
        s.button_pressed = pressed
        print("gpio button", pressed)
    end

    while s.status_elapsed >= STATUS_INTERVAL do
        s.status_elapsed = s.status_elapsed - STATUS_INTERVAL
        local elapsed_us = tim.us() - s.start_us
        local duty, duty_err = pwm.read(PWM_PIN)
        if duty then
            print("status", s.update_count, "fixed", s.fixed_count, "late", s.late_count, "us", elapsed_us, "duty", duty)
        else
            print("pwm read failed", duty_err)
        end
    end

    s.duty = s.duty + s.direction
    if s.duty >= pwm.MAX then
        s.duty = pwm.MAX
        s.direction = -1
    elseif s.duty <= pwm.MIN then
        s.duty = pwm.MIN
        s.direction = 1
    end
    pwm.write(PWM_PIN, s.duty)

    if s.update_count % 10 == 0 then
        ui.patch(
            self.ui.duty, { text = "Duty: " .. s.duty })
    end
end

function late_update(self, dt)
    self.state.late_count = self.state.late_count + 1
end

function on_input(self, action_id, action)
    local s = self.state
    s.input_count = s.input_count + 1
    print("input", action_id, action.event, action.value)

    if action_id == "run" and action.event == "clicked" then
        local ok, err = ui.patch(
            self.ui.run, {
            text = "Clicked " .. s.input_count,
        })
        report_result("ui patch clicked", ok, err)
    end
end

function on_message(self, message_id, message, sender)
    print("message", message_id, sender)
end

function on_reload(self)
    print("reload", self.state.update_count)
end

function final(self)
    print("comprehensive final", self.state.update_count, self.state.input_count)

    gpio.write(LED_PIN, gpio.LOW)
    gpio.release(BUTTON_PIN)
    gpio.release(LED_PIN)

    pwm.stop(PWM_PIN)
    pwm.release(PWM_PIN)

end
