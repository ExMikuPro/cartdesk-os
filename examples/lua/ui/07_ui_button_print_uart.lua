function init(self)
    self.children = ui.button({
        id = "uart_print",
        text = "USART Print",
        rect = { 24, 24, 160, 48 },
        input = "uart_print",
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
    if action_id == "uart_print" and action.event == "clicked" then
        print("hello world!")
    end
end

function final(self)
    -- UI children are deleted by the host after final(self).
end
