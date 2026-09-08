-- Arcade 68K frame utilisation UNDER GAMEPLAY. Drives the level-1
-- playthrough with tools/auto_beast.lua and samples the maincpu PC,
-- counting samples inside the game's idle loop (0x003980..0x003990 in
-- the arcade map; our port rebases the same code to 0x903980).
--
--   U = 1 - idle/samples  is the fraction of a frame the arcade's
--   10 MHz 68000 is working. Our 32X 68000 is 7.670453 MHz = 76.7% of
--   that, so the same work costs U * 1.3037 of a frame here.
local IDLE_LO, IDLE_HI = 0x003980, 0x003990
local samples, idle, frames = 0, 0, 0
local REPORT = tonumber(os.getenv('AU_REPORT') or '11000')
local START  = tonumber(os.getenv('AU_START')  or '5400')
local cpu = manager.machine.devices[":maincpu"]
dofile('tools/auto_beast.lua')          -- input timeline + its own callbacks
emu.register_frame_done(function()
    frames = frames + 1
    if frames < START then return end
    local pc = cpu.state["PC"].value & 0xFFFFFF
    samples = samples + 1
    if pc >= IDLE_LO and pc <= IDLE_HI then idle = idle + 1 end
    if frames == REPORT then
        local u = 1.0 - (idle / math.max(1, samples))
        print(string.format("PLAY U = %.3f  (samples %d, idle %d)", u, samples, idle))
        print(string.format("PLAY same work at 7.670 MHz = %.3f of a frame -> ceiling %.0f%% of 60fps",
              u * 1.3037, math.min(1.0, 1.0 / math.max(1e-6, u * 1.3037)) * 100))
    end
end)
