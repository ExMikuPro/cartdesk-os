local function assert_type(path, value, expected)
    if type(value) ~= expected then
        error(path .. " expected " .. expected .. ", got " .. type(value))
    end
end

local n, rng_err = rng.u32()
if n ~= nil then
    assert_type("rng.u32()", n, "number")
elseif type(rng_err) ~= "string" then
    error("rng.u32() must return number or nil, err")
end

local bytes, err = rng.bytes(16)
if not bytes then
    error("rng.bytes(16) failed: " .. tostring(err))
end
assert_type("rng.bytes(16)", bytes, "string")
if #bytes ~= 16 then
    error("rng.bytes(16) length mismatch")
end

local empty = rng.bytes(0)
assert_type("rng.bytes(0)", empty, "string")
if #empty ~= 0 then
    error("rng.bytes(0) length mismatch")
end

local crc_value, crc_err = crc.crc32("hello")
if not crc_value then
    error("crc.crc32 failed: " .. tostring(crc_err))
end
assert_type("crc.crc32", crc_value, "number")

local crc_hex, crc_hex_err = crc.crc32_hex("hello")
if not crc_hex then
    error("crc.crc32_hex failed: " .. tostring(crc_hex_err))
end
assert_type("crc.crc32_hex", crc_hex, "string")
if #crc_hex ~= 8 then
    error("crc.crc32_hex length mismatch")
end
if crc_hex ~= "3610A686" then
    error("crc.crc32_hex('hello') expected 3610A686, got " .. crc_hex)
end

assert_type("pwm.set_freq", pwm.set_freq, "function")
assert_type("pwm.get_freq", pwm.get_freq, "function")
assert_type("pwm.setFreq", pwm.setFreq, "function")
assert_type("pwm.getFreq", pwm.getFreq, "function")

assert_type("gpio.pinMode", gpio.pinMode, "function")
assert_type("gpio.digitalRead", gpio.digitalRead, "function")
assert_type("gpio.digitalWrite", gpio.digitalWrite, "function")
assert_type("pinMode", pinMode, "function")
assert_type("digitalRead", digitalRead, "function")
assert_type("digitalWrite", digitalWrite, "function")

print("rng/crc/pwm api test passed")
