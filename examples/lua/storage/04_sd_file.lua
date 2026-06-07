local PATH = "/lua_example.txt"

local function sd_call(fn)
    local ok, a, b = pcall(fn)
    if ok then
        return a, b
    end
    return nil, a
end

function init(self)
    local ok, err = sd_call(function()
        return sd.mount()
    end)
    if not ok then
        print("sd mount failed", err)
        return
    end

    local f
    f, err = sd_call(function()
        return sd.open(PATH, "w+")
    end)
    if not f then
        print("sd open failed", err)
        sd_call(function()
            return sd.umount()
        end)
        return
    end

    local written
    written, err = sd_call(function()
        return f:write("hello from lua\n")
    end)
    if not written then
        print("sd write failed", err)
    end

    sd_call(function()
        return f:seek(0)
    end)

    local size
    size, err = sd_call(function()
        return f:size()
    end)
    if size then
        local data
        data, err = sd_call(function()
            return f:read(size)
        end)
        if data then
            print("sd read", data)
        else
            print("sd read failed", err)
        end
    else
        print("sd size failed", err)
    end

    sd_call(function()
        return f:close()
    end)
    sd_call(function()
        return sd.umount()
    end)
end
