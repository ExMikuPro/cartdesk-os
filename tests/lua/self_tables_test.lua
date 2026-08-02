function init(self)
    assert(type(self.state) == "table")
    assert(type(self.ui) == "table")
    assert(type(self.assets) == "table")
    assert(type(self.timers) == "table")
    assert(type(self.services) == "table")
    assert(self["child" .. "ren"] == nil)

    self.state.count = 1
    assert(self.state.count == 1)
end

function update(self, dt)
    assert(type(dt) == "number")
end

function final(self)
end
