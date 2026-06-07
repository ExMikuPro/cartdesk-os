local function expect(value, message)
    if not value then
        error(message)
    end
end

function init(self)
    expect(ui.button.create == nil, "old ui.button.create must not exist")
    expect(ui.button.draw == nil, "old ui.button.draw must not exist")
    expect(ui.button.get_screen == nil, "old ui.button.get_screen must not exist")
    expect(ui.slider.create == nil, "old ui.slider.create must not exist")
    expect(ui.slider.draw == nil, "old ui.slider.draw must not exist")
    expect(ui.slider.get_screen == nil, "old ui.slider.get_screen must not exist")

    self.children = ui.button({
        id = "run",
        text = "Run",
        rect = { 24, 24, 120, 48 },
        input = "run",
        style = {
            bg = 0x2D8CFF,
            bg_alpha = 255,
            text = 0xFFFFFF,
            border = {
                color = 0x145DA0,
                width = 2,
            },
            radius = 8,
        },
    })

    expect(ui.find(self, "run") ~= nil, "single button should be found")
    expect(ui.patch(self, "run", { text = "Running" }) == true, "button patch should work")

    self.state = {
        volume = 128,
    }
    self.children = {
        self.children,
        ui.slider({
            id = "volume",
            rect = { 24, 90, 220, 24 },
            range = { 0, 255 },
            value = self.state.volume,
            input = "volume",
            style = {
                bg = 0x202020,
                indicator = 0x2D8CFF,
                knob = 0xFFFFFF,
                radius = 8,
            },
        }),
    }

    expect(ui.find(self, "run") ~= nil, "button in children array should be found")
    expect(ui.find(self, "volume") ~= nil, "slider in children array should be found")
    expect(ui.find(self, "missing") == nil, "missing id should return nil")

    expect(ui.patch(self, "volume", { value = 200 }) == true, "slider patch should work")

    local ok, err = ui.patch(self, "missing", { text = "Missing" })
    expect(ok == nil, "missing patch should return nil")
    expect(err == "ui id not found", "missing patch should explain failure")
end

function on_input(self, action_id, action)
    if action_id == "volume" and action.event == "changed" then
        self.state.volume = action.value
    end
end

function final(self)
    -- Host deletes self.children after final(self); scripts do not delete UI handles.
end
