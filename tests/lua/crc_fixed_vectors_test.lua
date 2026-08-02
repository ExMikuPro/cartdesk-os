local vectors = {
    { "", 0x00000000 },
    { "hello", 0x3610A686 },
    { "123456789", 0xCBF43926 },
    { "\0\1\2\3", 0x8BB98613 },
    { "a", 0xE8B7BE43 },
    { "abc", 0x352441C2 },
    { "abcd", 0xED82CD11 },
    { "abcde", 0x8587D865 },
}

for _, vector in ipairs(vectors) do
    local actual = assert(crc.crc32(vector[1]))
    assert(actual == vector[2], string.format("CRC mismatch: %08X", actual))
    assert(crc.verify32(vector[1], vector[2]) == true)
end
assert(crc.verify32("hello", 0) == false)
assert(select(1, crc.verify32("hello", -1)) == nil)
assert(select(1, crc.verify32("hello", 0x100000000)) == nil)
assert(select(1, crc.crc32({})) == nil)
