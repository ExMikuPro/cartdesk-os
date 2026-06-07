function init(self)
    self.elapsed = 0
    self.ticks = 0
    print("lifecycle init")
end

function update(self, dt)
    self.elapsed = self.elapsed + dt

    if self.elapsed >= 1.0 then
        self.elapsed = self.elapsed - 1.0
        self.ticks = self.ticks + 1
        print("lifecycle tick", self.ticks)
    end
end

function final(self)
    print("lifecycle final", self.ticks)
end
