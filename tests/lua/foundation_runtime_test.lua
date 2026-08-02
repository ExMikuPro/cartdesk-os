local fired = 0

function init(self)
    assert(type(self.state) == "table" and type(self.ui) == "table")
    assert(type(self.assets) == "table" and type(self.timers) == "table")
    assert(type(self.services) == "table" and self["child" .. "ren"] == nil)

    assert(random.integer(7, 7) == 7)
    local number = assert(random.number())
    assert(number >= 0 and number < 1)
    assert(#assert(random.bytes(32)) == 32)
    assert(select(1, random.integer(2, 1)) == nil)
    assert(select(1, random.bytes(4097)) == nil)

    assert(crc.crc32("123456789") == 0xCBF43926)
    assert(crc.verify32("hello", 0x3610A686))

    self.ui.panel = assert(ui.container({ id="panel", rect={0,0,300,200} }))
    self.ui.title = assert(ui.label({
        id="title", parent=self.ui.panel, text="test", rect={0,0,100,30}
    }))
    assert(ui.patch(self.ui.title, { text="patched", hidden=false }))
    assert(select(1, ui.patch(self.ui.title, { titel="bad" })) == nil)
    assert(select(1, ui.patch({}, { text="bad" })) == nil)

    self.timers.once = assert(timer.after(5, function() fired = fired + 1 end))
    assert(timer.active(self.timers.once))

    assert(storage.set("binary", "a\0b"))
    assert(storage.get("binary") == "a\0b")
    assert(storage.has("binary"))
    assert(storage.remove("binary"))
    assert(not storage.has("binary"))

    local width, height = system.screen_size()
    assert(width > 0 and height > 0)
    assert(type(system.firmware_version()) == "string")
    assert(type(system.memory_info()) == "table")
    assert(type(system.sd_status()) == "table")
    assert(type(system.usb_status()) == "table")
    assert(log.info("foundation runtime test") == true)
end

function update(self, dt)
    if fired == 1 and self.ui.panel then
        assert(ui.delete(self.ui.panel))
        assert(select(1, ui.patch(self.ui.title, { text="deleted" })) == nil)
        self.ui = {}
    end
end

function final(self)
end
