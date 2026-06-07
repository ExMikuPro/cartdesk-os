function init(self)
    self.count = 0
    self.fixed_count = 0
    print("init")
end

function fixed_update(self, dt)
    self.fixed_count = self.fixed_count + 1
    self.fixed_dt = dt
end

function update(self, dt)
    self.count = self.count + 1
    print("update", self.count, dt)
end

function late_update(self, dt)
    print("late_update", self.count, dt)
end

function on_input(self, action_id, action)
    print("input", action_id, action.event, action.pressed, action.released, action.value)
end

function on_message(self, message_id, message, sender)
    print("message", message_id, sender)
end

function on_reload(self)
    print("reload", self.count)
end

function final(self)
    print("final", self.count, self.fixed_count)
end
