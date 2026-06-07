function init(self)
    self.state = {
        count = 0,
        fixed_count = 0,
        fixed_dt = 0,
    }
    print("init")
end

function fixed_update(self, dt)
    local s = self.state
    s.fixed_count = s.fixed_count + 1
    s.fixed_dt = dt
end

function update(self, dt)
    local s = self.state
    s.count = s.count + 1
    print("update", s.count, dt)
end

function late_update(self, dt)
    print("late_update", self.state.count, dt)
end

function on_input(self, action_id, action)
    print("input", action_id, action.event, action.pressed, action.released, action.value)
end

function on_message(self, message_id, message, sender)
    print("message", message_id, sender)
end

function on_reload(self)
    print("reload", self.state.count)
end

function final(self)
    local s = self.state
    print("final", s.count, s.fixed_count)
end
