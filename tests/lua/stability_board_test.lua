local function expect_error(value, message, expected)
    assert(value == nil)
    assert(message == expected, tostring(message))
end

function init(self)
    assert(random.integer(0, 0) == 0)

    local bit = assert(random.integer(0, 1))
    assert(bit >= 0 and bit <= 1)

    local full_u32 = assert(random.integer(0, 0xFFFFFFFF))
    assert(full_u32 >= 0 and full_u32 <= 0xFFFFFFFF)

    local signed_u32 = assert(random.integer(-0x80000000, 0x7FFFFFFF))
    assert(signed_u32 >= -0x80000000 and signed_u32 <= 0x7FFFFFFF)

    local shifted_u32 = assert(random.integer(-1, 0xFFFFFFFE))
    assert(shifted_u32 >= -1 and shifted_u32 <= 0xFFFFFFFE)

    local value, message = random.integer(0, 0x100000000)
    expect_error(value, message, "random range exceeds 32-bit entropy")

    value, message = random.integer(0, 0x100000001)
    expect_error(value, message, "random range exceeds 32-bit entropy")

    value, message = random.integer(math.mininteger, math.maxinteger)
    expect_error(value, message, "random range exceeds 32-bit entropy")

    self.ui.title = assert(ui.label({
        id = "stability-title",
        text = "before intentional init failure",
        rect = {20, 20, 500, 40},
    }))
    self.timers.test = assert(timer.every(1000, function()
        log.error("stability timer survived init failure")
    end))

    error("intentional stability init failure")
end

function update(self, dt)
    error("update must not run after init failure")
end

function final(self)
end
