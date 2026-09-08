-- 68K FRAME UTILISATION ON THE ARCADE (the oracle).
-- Samples the maincpu PC every scanline and counts how many samples land
-- in the game's idle loop. 1 - idle = the fraction of each frame the
-- arcade's 10 MHz 68000 is actually working.
--
-- Our 32X 68000 runs at 7.670 MHz = 76.7% of 10 MHz, so the same work
-- costs 1.304x here. If the arcade's utilisation U satisfies 1.304*U > 1,
-- 60 fps is unreachable on this hardware with ZERO port overhead, and
-- the achievable ceiling is 1/(1.304*U).
--
-- Idle loop: our rebased map puts it at 0x903980..0x903990; the arcade
-- runs the same code at 0x003980..0x003990 (the 0x900000 base is ours).
local IDLE_LO, IDLE_HI = 0x003980, 0x003990
local samples, idle, frames = 0, 0, 0
local WARM, RUN = 600, 1800
local cpu = manager.machine.devices[":maincpu"]
local scr = manager.machine.screens[":screen"]
local pcs = {}
emu.register_frame_done(function()
    frames = frames + 1
    if frames < WARM then return end
    if frames > WARM + RUN then
        local u = 1.0 - (idle / math.max(1, samples))
        print(string.format("ARCADE 68K UTILISATION  samples %d  idle %d  U = %.3f", samples, idle, u))
        print(string.format("  same work on a 7.670 MHz 68000 = %.3f of a frame", u * 10.0 / 7.670453))
        print(string.format("  ceiling at zero port overhead   = %.1f%% of 60 fps",
              math.min(1.0, 1.0 / (u * 10.0 / 7.670453)) * 100))
        -- the busiest addresses, so the utilisation can be attributed
        local t = {}
        for pc, n in pairs(pcs) do t[#t+1] = { pc, n } end
        table.sort(t, function(a,b) return a[2] > b[2] end)
        for i = 1, math.min(8, #t) do
            print(string.format("  PC %06X  %5d samples", t[i][1], t[i][2]))
        end
        manager.machine:exit()
    end
end)
emu.register_periodic(function()
    if frames < WARM then return end
    local pc = cpu.state["PC"].value & 0xFFFFFF
    samples = samples + 1
    if pc >= IDLE_LO and pc <= IDLE_HI then idle = idle + 1 end
    local b = pc & 0xFFFFF0
    pcs[b] = (pcs[b] or 0) + 1
end)
