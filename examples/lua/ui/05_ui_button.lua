function init(self)
    local screen = ui.button.get_screen()

    self.button = ui.button.create(screen)
    self.button:set_text("Run")
    self.button:set_pos(24, 24)
    self.button:set_size(120, 48)
    self.button:set_style_bg_color(0x2D8CFF, 255)
    self.button:set_style_text_color(0xFFFFFF)
    self.button:set_style_border(0x145DA0, 2)
    self.button:set_style_radius(8)
    self.button:set_callback(function(btn, event)
        print("button event", event)
    end)
end

function final(self)
    if self.button then
        self.button:delete()
        self.button = nil
    end
end
