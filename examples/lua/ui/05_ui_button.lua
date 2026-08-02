function init(self)
    self.ui.button = ui.button({
        id = "button_demo",
        text = "Run",
        rect = { 24, 24, 120, 48 },
        input = "button_demo",
    })
end

function on_input(self, action_id, action)
    if action_id == "button_demo" and action.event == "clicked" then
        print("button clicked")
    end
end

function final(self)
end
