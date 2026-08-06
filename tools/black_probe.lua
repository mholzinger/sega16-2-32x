-- LOOP 7c: hunt the STROBE. Mike's ares run shows a fully-black frame
-- interleaved with correct ones (screenshots 551-554). blit skips read 0,
-- so the master is not skipping — something flips onto a bank that was
-- never composed. This asks whether MAME reproduces it at all: snapshot a
-- run of consecutive frames so they can be scanned for all-black.
--   PROBE_DIR=<dir> PROBE_FIRST=<frame> PROBE_N=<count> mame 32x ...
local first = tonumber(os.getenv('PROBE_FIRST') or '900')
local n     = tonumber(os.getenv('PROBE_N') or '120')
local dir   = os.getenv('PROBE_DIR') or '/tmp/blackprobe'
local f = 0
emu.register_frame_done(function()
    f = f + 1
    if f < first or f >= first + n then
        if f >= first + n then manager.machine:exit() end
        return
    end
    for _, screen in pairs(manager.machine.screens) do
        screen:snapshot(string.format('%s/f%05d.png', dir, f))
        break
    end
end)
