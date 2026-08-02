function init(self)
    self.assets.player = "assets/images/player.png"

    local image, err = ui.image({
        id = "player",
        src = self.assets.player,
        rect = { 32, 32, 64, 64 },
    })
    if not image then
        print("ui.image failed", err)
        return
    end
    self.ui.player = image

    local ok, patch_err = ui.patch(
        self.ui.player, {
        hidden = false,
        src = self.assets.player,
    })
    if not ok then
        print("ui.image patch failed", patch_err)
    end
end

function final(self)
end
