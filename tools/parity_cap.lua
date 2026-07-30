-- Scene-anchored capture for the parity loop (arcade + ours identically).
-- FRAME-EXACT anchors: each scene fires the first time ALL its terms
-- match — game-state equality means identical visuals modulo rendering
-- bugs, which is the whole objective function. Loose (settle) anchors
-- proved useless for moving scenes: one frame of slop painted a panning
-- screen 98% magenta.
-- Terms: {addr, size(1|2), mask, val}. Addresses from the attract
-- forensics (NOTES 2026-07-29..31).
-- Env: PC_DIR (output dir), PC_TAG (arc|ours)
-- STABLE-MOMENT anchors: exact state match, then `after` extra frames
-- so pipeline latency settles (palette stream ~4 vints, sprite list 1,
-- compose 3). Instant-equality charged every in-flight fade frame as
-- mismatch (identical counts across unrelated renderer changes proved
-- the scoreboard was measuring latency, not rendering).
local scenes = {
    { name = "title",  after = 45,
      terms = { {0xFFF031,1,0xFF,0x08}, {0xFFF02D,1,0x3F,0x00} } },
    { name = "scream", after = 0,
      terms = { {0xFFF031,1,0xFF,0x0C}, {0xFFF0E4,2,0x1F0,0x100} } },
    { name = "eyehold",after = 20,
      terms = { {0xFFF031,1,0xFF,0x10}, {0xFFC0A1,1,0xFF,0x09},
                {0xFFC0A2,2,0xFFF0,0x0040} } },
    { name = "demo",   after = 0,
      terms = { {0xFFF031,1,0xFF,0x14}, {0xFFF02A,2,0xFFFF,0x0060} } },
    { name = "demo2",  after = 0,
      terms = { {0xFFF031,1,0xFF,0x14}, {0xFFF02A,2,0xFFFF,0x0180} } },
}
local f, mem = 0, nil
local shot, nshot = {}, 0

emu.register_frame_done(function()
    f = f + 1
    if f < 60 then return end
    if not mem then
        for _, c in pairs(manager.machine.devices) do
            if c.tag == ':maincpu' then mem = c.spaces['program'] end
        end
        if not mem then return end
    end
    for i, sc in ipairs(scenes) do
        if not shot[i] then
            if armed[i] then
                armed[i] = armed[i] - 1
            else
                local ok = true
                for _, t in ipairs(sc.terms) do
                    local v = (t[2] == 2) and mem:read_u16(t[1]) or mem:read_u8(t[1])
                    if (v & t[3]) ~= t[4] then ok = false break end
                end
                if ok and sc.after > 0 then armed[i] = sc.after; ok = false end
                if not ok then goto next_scene end
            end
            if armed[i] == nil or armed[i] <= 0 then
                armed[i] = nil
                for _, screen in pairs(manager.machine.screens) do
                    screen:snapshot(string.format('%s/%s_%s.png',
                        os.getenv('PC_DIR'), sc.name, os.getenv('PC_TAG')))
                    break
                end
                shot[i] = true
                nshot = nshot + 1
            end
        end
        ::next_scene::
    end
    if nshot >= #scenes or f > 14000 then manager.machine:exit() end
end)
