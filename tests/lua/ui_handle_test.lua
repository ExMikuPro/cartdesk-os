local function expect(value, message)
    if not value then
        error(message)
    end
end

function init(self)
    expect(type(self.state) == "table", "self.state must be a table")
    expect(type(self.ui) == "table", "self.ui must be a table")
    expect(type(self.assets) == "table", "self.assets must be a table")
    expect(type(self.timers) == "table", "self.timers must be a table")
    expect(type(self.services) == "table", "self.services must be a table")
    expect(self["child" .. "ren"] == nil, "unexpected extra self node")

    self.ui.title = ui.label({
        id = "title",
        text = "Ready",
        rect = { 24, 24, 200, 40 },
    })
    expect(type(self.ui.title) == "userdata", "label must return full userdata")

    self.ui.button = ui.button({
        id = "run",
        text = "Run",
        rect = { 24, 88, 120, 48 },
        input = "run",
    })
    expect(type(self.ui.button) == "userdata", "button must return full userdata")

    self.assets.logo = "assets/logo.png"
    local logo, logo_err = ui.image({
        id = "logo",
        src = self.assets.logo,
        rect = { 180, 88, 64, 64 },
    })
    expect(logo ~= nil, logo_err or "image creation failed")
    expect(type(logo) == "userdata", "image must return full userdata")
    self.ui.logo = logo

    expect(ui.patch(
        self.ui.title, { text = "Running" }) == true,
           "handle patch must work")
    expect(ui.patch(
        self.ui.logo, {
            hidden = false,
            rect = { 180, 88, 72, 72 },
            src = self.assets.logo,
        }) == true, "image patch must support hidden, rect, and src")

    local ok, err = ui.patch({}, { text = "forged" })
    expect(ok == nil, "plain table must not be accepted as a UI handle")
    expect(type(err) == "string", "invalid handle must return an error")

    ok, err = ui.patch(
        self.ui.title, { unknown = true })
    expect(ok == nil, "unknown property must fail")
    expect(type(err) == "string", "unknown property must explain failure")

    ok, err = ui.patch(
        self.ui.title, { hidden = "yes" })
    expect(ok == nil, "invalid property type must fail")
    expect(type(err) == "string", "invalid property type must explain failure")
end

function on_input(self, action_id, action)
    if action_id == "run" and action.event == "clicked" then
        ui.patch(
            self.ui.title, { text = "Clicked" })
    end
end

function final(self)
    -- Host ownership cleanup is independent of the contents of self.ui.
    self.ui = {}
end
