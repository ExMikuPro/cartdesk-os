function init(self)
    self.ui.button = ui.button({
        id = "uart_print",
        text = "USART Print",
        rect = { 24, 24, 160, 48 },
        input = "uart_print",
    })
end

function on_input(self, action_id, action)
    if action_id == "uart_print" and action.event == "clicked" then
        print("hello world!")
    end
end

function final(self)
end
