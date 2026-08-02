function init(self)
    self.state.count = storage.get("count", 0)
    self.assets.logo = "assets/logo.png"

    self.ui.title = ui.label({
        id = "title",
        text = "Count: " .. self.state.count,
        rect = { 20, 20, 300, 40 },
    })
    self.ui.logo = ui.image({
        id = "logo",
        src = self.assets.logo,
        rect = { 20, 80, 128, 128 },
    })
    self.ui.button = ui.button({
        id = "button",
        text = "点击",
        rect = { 20, 230, 120, 48 },
        input = "button",
    })

    local random_value, random_err = random.integer(1, 100)
    if random_value then
        self.state.random_value = random_value
    else
        log.error("random failed", random_err)
    end

    local checksum, crc_err = crc.crc32("CartDesk")
    if checksum then
        self.state.checksum = checksum
        log.info("crc32", checksum)
    else
        log.error("crc failed", crc_err)
    end

    self.timers.refresh = timer.every(1000, function()
        log.debug("uptime", system.uptime_ms())
    end)
end

function on_input(self, action_id, action)
    if action_id == "button" and action.event == "clicked" then
        self.state.count = self.state.count + 1
        ui.patch(self.ui.title, { text = "Count: " .. self.state.count })
        storage.set("count", self.state.count)
        local ok, err = storage.commit()
        if not ok then log.error("storage commit failed", err) end
    end
end

function update(self, dt)
end

function final(self)
    log.info("application final")
end
