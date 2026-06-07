function init(self)
    self.children = ui.button({
        id = "button_demo",
        text = "Run",
        rect = { 24, 24, 120, 48 },
        input = "button_demo",
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
end

function on_input(self, action_id, action)
    if action_id == "button_demo" then
        print("button event", action.event)
    end
end

function final(self)
    -- UI children are deleted by the host after final(self).
end
