-- Log accesses to danger zones; survive memory-map changes by reinstalling
-- taps every frame. Inject coin+start and mash buttons to reach real play.
local out = assert(io.open(os.getenv("TAP_OUT") or "/tmp/tap_hits.txt", "w"))
local seen = {}
local cpu, mem, tap1, tap2
local frames = 0

local function hook(zone)
    return function(offset, data, mask)
        local pc = cpu.state["CURPC"].value
        local key = string.format("%s a=%06x pc=%06x", zone, offset, pc)
        if not seen[key] then
            seen[key] = true
            out:write(key .. "\n")
            out:flush()
        end
    end
end

local function install()
    if tap1 then tap1:remove() end
    if tap2 then tap2:remove() end
    tap1 = mem:install_read_tap(0x000000, 0x0003ef, "boot", hook("BOOT"))
    tap2 = mem:install_read_tap(0x0003f0, 0x0003ff, "mystery", hook("MYST"))
end

cpu = manager.machine.devices[":maincpu"]
mem = cpu.spaces["program"]
install()

local function find_field(names)
    for pname, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do
            for _, want in ipairs(names) do
                if fname == want then return field end
            end
        end
    end
end

local coin  = find_field({"Coin 1"})
local start = find_field({"1 Player Start", "Start 1", "P1 Start"})
local btn1  = find_field({"P1 Button 1"})
local btn2  = find_field({"P1 Button 2"})
local left  = find_field({"P1 Left"})
local right = find_field({"P1 Right"})

emu.register_frame_done(function()
    frames = frames + 1
    install()  -- mapper remaps kill taps; cheap to re-add
    -- coin at 10s, start at 12s, then mash
    if coin and frames >= 600 and frames < 610 then coin:set_value(1)
    elseif coin and frames == 610 then coin:set_value(0) end
    if start and frames >= 720 and frames < 730 then start:set_value(1)
    elseif start and frames == 730 then start:set_value(0) end
    if frames > 800 then
        local ph = frames % 120
        if btn1 then btn1:set_value(ph < 20 and 1 or 0) end
        if btn2 then btn2:set_value(ph >= 40 and ph < 50 and 1 or 0) end
        if right then right:set_value(ph >= 60 and ph < 100 and 1 or 0) end
        if left then left:set_value(ph >= 100 and 1 or 0) end
    end
end)
