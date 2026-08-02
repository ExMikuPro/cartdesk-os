local modules = {
    ui = { "root", "container", "label", "button", "image", "patch", "delete" },
    assets = { "exists", "image", "data" },
    storage = { "has", "get", "set", "remove", "commit", "clear" },
    timer = { "now_ms", "after", "every", "cancel", "active" },
    system = { "screen_size", "firmware_version", "uptime_ms", "memory_info",
               "sd_status", "usb_status", "exit", "restart_app" },
    random = { "integer", "number", "bytes" },
    log = { "debug", "info", "warn", "error" },
    crc = { "crc32", "verify32" },
}

for module_name, functions in pairs(modules) do
    local module = _G[module_name]
    assert(type(module) == "table", "missing module: " .. module_name)
    for _, function_name in ipairs(functions) do
        assert(type(module[function_name]) == "function" or
               (module_name == "ui" and type(module[function_name]) == "table"),
               "missing API: " .. module_name .. "." .. function_name)
    end
end

for _, removed in ipairs({ "gpio", "pwm", "tim", "rng", "delay" }) do
    assert(_G[removed] == nil, "removed API remains exported: " .. removed)
end

local old_patch = ui.patch
local ok = pcall(function() ui.patch = nil end)
assert(not ok and ui.patch == old_patch, "core module must be read-only")
print("foundation API smoke test passed")
