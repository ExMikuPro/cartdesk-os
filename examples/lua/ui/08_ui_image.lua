local IMAGE_SRC = "assets/images/player.png"

local function try_image(config)
    local image, err = ui.image(config)
    if not image then
        print("ui.image failed", config.src, err)
        return nil
    end
    return image
end

function init(self)
    self.children = {}

    local img1 = try_image({
        id = "player_a",
        src = IMAGE_SRC,
        rect = { 32, 32, 64, 64 },
        style = {
            alpha = 255,
        },
    })
    if img1 then
        self.children[#self.children + 1] = img1
    end

    local img2 = try_image({
        id = "player_b",
        src = IMAGE_SRC,
        rect = { 112, 32, 64, 64 },
        region = { 0, 0, 32, 32 },
        style = {
            alpha = 220,
        },
    })
    if img2 then
        self.children[#self.children + 1] = img2
    end

    local missing = try_image({
        id = "missing_image",
        src = "assets/images/not-found.png",
        rect = { 32, 112, 64, 64 },
    })
    if missing then
        self.children[#self.children + 1] = missing
    end

    local bad_region = try_image({
        id = "bad_region",
        src = IMAGE_SRC,
        rect = { 112, 112, 64, 64 },
        region = { 9999, 9999, 32, 32 },
    })
    if bad_region then
        self.children[#self.children + 1] = bad_region
    end

    local ok, err = pcall(function()
        ui.image({
            src = "../secret.png",
            rect = { 0, 0, 16, 16 },
        })
    end)
    print("invalid path throws", not ok, err)

    if #self.children > 0 then
        local patched, patch_err = ui.patch(self, "player_a", {
            src = IMAGE_SRC,
        })
        print("patch image src rejected", patched == nil, patch_err)
    end
end

function final(self)
    -- UI children are deleted by the host after final(self).
end
