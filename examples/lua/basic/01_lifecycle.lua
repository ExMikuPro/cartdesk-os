function init(self)
    self.state = {
        elapsed = 0,
        ticks = 0,
    }
    print("lifecycle init")
end

function update(self, dt)
    local s = self.state

    s.elapsed = s.elapsed + dt

    if s.elapsed >= 1.0 then
        s.elapsed = s.elapsed - 1.0
        s.ticks = s.ticks + 1
        print("lifecycle tick", s.ticks)
    end
end

function final(self)
    print("lifecycle final", self.state.ticks)
end
