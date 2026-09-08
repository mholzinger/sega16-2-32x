-- REF DUMP — write every emulated frame as a PNG while you PLAY.
-- screen:snapshot() captures the EMULATED 320x224 screen, pre-filter,
-- regardless of window size or bgfx settings (measured — see the
-- headless-MAME notes in CLAUDE.md), so these are true reference
-- frames even though the window is live and interactive.
--
-- ~60 PNGs/sec ≈ 1 GB per 5 minutes of play. RD_EVERY=n captures
-- every nth frame instead (RD_EVERY=2 = 30/sec) if that's too much.
-- Frame numbers are emulated frames from boot — directly comparable
-- to ares --frames indices.
local every = tonumber(os.getenv('RD_EVERY') or '1')
local screen
for _, s in pairs(manager.machine.screens) do screen = s break end
local n = 0
emu.register_frame_done(function()
    n = n + 1
    if n % every == 0 then
        screen:snapshot(string.format('ref_%06d.png', n))
    end
end)
