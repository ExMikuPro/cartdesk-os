function init(self)
    local screen = ui.button.get_screen()

    self.button = ui.button.create(screen)
    self.button:set_text("USART Print")
    self.button:set_pos(24, 24)
    self.button:set_size(160, 48)
    self.button:set_style_bg_color(0x2D8CFF, 255)
    self.button:set_style_text_color(0xFFFFFF)
    self.button:set_style_border(0x145DA0, 2)
    self.button:set_style_radius(8)
    self.button:set_input_id("uart_print")
end

function on_input(self, action_id, action)
    if action_id == "uart_print" and action.event == "clicked" then
        print("hello world!")
    end
end

function final(self)
    if self.button then
        self.button:delete()
        self.button = nil
    end
end
