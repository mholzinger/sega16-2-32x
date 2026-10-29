-- SPRITE-LINE BUDGET on the ARCADE (kit tool). Answers TOOLKIT's "can the
-- MD VDP's sprite chip draw this title's sprites?" before any port code
-- exists: per scanline, how many live sprite records cover it and how
-- many pixels they carry, against the H40 budget of 20 sprites / 320 px.
-- Record layout (sega16sp.cpp, sys16b): +0 bottom-1 | top-1 ; +2 x ;
-- +4 bit15 end, bit14 hide, bit8 flip, low byte signed pitch ; +8 bank/
-- pri/colour ; +A zoom (v bits 5-9, h bits 0-4; 0 = 1:1). Width counted
-- as |pitch| * 4 px (a word is 4 packed pixels), transparent pixels
-- included, as the VDP charges it. Zoomed records (+A != 0) can never go
-- to MD hardware and are counted apart.
-- Env: SL_OUT, SL_A / SL_B window (default 1500-4000), SL_PLAY=1 drives
-- tools/auto_goldnaxe.lua, SL_BASEVAR (default 0xFFECC4: sprite base long).
local out = os.getenv('SL_OUT') or '/tmp/sprline_arcade.txt'
local A, B = tonumber(os.getenv('SL_A') or '1500'), tonumber(os.getenv('SL_B') or '4000')
local basevar = tonumber(os.getenv('SL_BASEVAR') or '0xFFECC4')
if os.getenv('SL_PLAY') == '1' then dofile('tools/auto_goldnaxe.lua') end
local sp = manager.machine.devices[':maincpu'].spaces['program']
local frames, lines, over_n, over_px, max_n, max_px, recs, zoomed, hidden = 0, 0, 0, 0, 0, 0, 0, 0, 0
local hist_n, hist_px = {}, {}
emu.register_frame_done(function()
    frames = frames + 1
    if frames < A then return end
    local base = sp:read_u16(basevar) << 16
    local cnt, px = {}, {}
    for l = 0, 223 do cnt[l] = 0; px[l] = 0 end
    for r = 0, 127 do
        local o = base + r * 16
        local w0, w4 = sp:read_u16(o), sp:read_u16(o + 4)
        if (w4 & 0x8000) ~= 0 then break end
        if (w4 & 0x4000) ~= 0 then hidden = hidden + 1
        else
            local top, bot = (w0 & 0xFF) + 1, (w0 >> 8) + 1
            local pitch = w4 & 0xFF; if pitch >= 0x80 then pitch = 256 - pitch end
            local wpx = pitch * 4
            local z = sp:read_u16(o + 0xA) & 0x3FF
            recs = recs + 1; if z ~= 0 then zoomed = zoomed + 1 end
            if bot > top and wpx > 0 then
                for l = math.max(top, 0), math.min(bot - 1, 223) do cnt[l] = cnt[l] + 1; px[l] = px[l] + wpx end
            end
        end
    end
    for l = 0, 223 do
        lines = lines + 1
        if cnt[l] > 20 then over_n = over_n + 1 end
        if px[l] > 320 then over_px = over_px + 1 end
        if cnt[l] > max_n then max_n = cnt[l] end
        if px[l] > max_px then max_px = px[l] end
        local bn, bp = math.min(cnt[l], 31), math.min(px[l] // 64, 15)
        hist_n[bn] = (hist_n[bn] or 0) + 1; hist_px[bp] = (hist_px[bp] or 0) + 1
    end
    if frames ~= B then return end
    local f = io.open(out, 'w')
    f:write(string.format('s16b_sprline_arcade %s frames %d-%d (%d frames, %d lines)\n', manager.machine.system.name, A, B, B - A + 1, lines))
    f:write(string.format('records %d (%.1f/frame), zoomed %d (%.1f%%), hidden %d\n', recs, recs / (B - A + 1), zoomed, 100 * zoomed / math.max(1, recs), hidden))
    f:write(string.format('lines over 20 sprites: %d (%.2f%%)   lines over 320 px: %d (%.2f%%)   worst line: %d sprites, %d px\n', over_n, 100 * over_n / lines, over_px, 100 * over_px / lines, max_n, max_px))
    f:write('sprites-per-line histogram: ') for i = 0, 31 do if hist_n[i] then f:write(string.format('%d:%d ', i, hist_n[i])) end end f:write('\n')
    f:write('px-per-line histogram (64 px bins): ') for i = 0, 15 do if hist_px[i] then f:write(string.format('%d-%d:%d ', i * 64, i * 64 + 63, hist_px[i])) end end f:write('\n')
    f:close(); print('wrote ' .. out); manager.machine:exit()
end)
