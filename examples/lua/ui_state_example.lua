function init(self)
    self.state.count = 0

    self.assets.logo = "assets/logo.png"

    local title, title_err = ui.label({
        id = "title",
        text = "Hello",
        rect = { 20, 20, 200, 40 },
    })

    if not title then
        print("title failed", title_err)
        return
    end

    self.ui.title = title

    local logo, logo_err = ui.image({
        id = "logo",
        src = self.assets.logo,
        rect = { 20, 80, 128, 128 },
    })

    if not logo then
        print("logo failed", logo_err)
        return
    end

    self.ui.logo = logo

    local button, button_err = ui.button({
        id = "button",
        text = "点击",
        rect = { 20, 230, 120, 48 },
        input = "button",
    })

    if not button then
        print("button failed", button_err)
        return
    end

    self.ui.button = button
end

function on_input(self, action_id, action)
    if action_id == "button"
        and action.event == "clicked" then

        self.state.count = self.state.count + 1

        local ok, err = ui.patch(
            self.ui.title, {
            text = "点击次数：" .. self.state.count,
        })

        if not ok then
            print("title patch failed", err)
        end
    end
end

function update(self, dt)
end

function final(self)
end
