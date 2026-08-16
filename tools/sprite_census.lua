-- LOOP 13 — MD-HARDWARE-SPRITE FEASIBILITY CENSUS (arcade oracle).
-- Runs against MAME *arcade* altbeast, NOT the port: reads live S16B
-- object RAM (0x440000, 8 words/record, sega16sp format — the same
-- fields m_main.c compose_sprites decodes) and the :sprites ROM
-- region for ragged strip widths.
--
--   mame altbeast -rompath ./mame -skip_gameinfo -video none \
--       -sound none -nothrottle -autoboot_script tools/sprite_census.lua
--   CENSUS_OUT=path.csv to redirect; CENSUS_FRAMES to change run length.
--
-- Question: what fraction of the sprite chip's actual per-frame load
-- is expressible as MD VDP hardware sprites, against the MD's real
-- ceilings (80/frame, 20/line, 320 sprite-px fetched/line, 32px max
-- sprite width, no zoom)? "Unzoomed" = zoom word d5 == 0 exactly.
-- Shadow sprites (colour 0x3F) counted separately: MD shadow/highlight
-- mode is the candidate mechanism there, not a normal sprite.
--
-- Width is measured by walking the strip like the hardware does
-- (nibbles until 0xF, cap 128 words = 512px) at three sample rows
-- (top / mid / bottom-1), taking the max — ragged strips vary per row.
-- Region byte order can skew a width by <=3px; census-grade, fine.
--
-- CSV per sampled frame:
--   frame,nrec,nunz,nshad,nzoom,md_equiv_total,
--   worst_line_rec,worst_line_mdeq,worst_line_px,
--   pp0,pp1,pp2,pp3,ncolsets
local out_path = os.getenv('CENSUS_OUT') or '/tmp/sprite_census.csv'
local max_frames = tonumber(os.getenv('CENSUS_FRAMES') or '5400')
local out = assert(io.open(out_path, 'w'))
out:write('frame,nrec,nunz,nshad,nzoom,md_equiv_total,' ..
          'worst_line_rec,worst_line_mdeq,worst_line_px,' ..
          'pp0,pp1,pp2,pp3,ncolsets\n')
local frames = 0

local function strip_width(rgn, bank, o, flip)
    -- Hardware row walk (m_main.c compose_sprites NIB loops): words
    -- forward high-nibble-first, or backward low-nibble-first when
    -- flipped; the row ends when the LAST-walked nibble of a word is
    -- 0xF (mid-word F nibbles are ordinary skipped pixels). Each word
    -- consumes 4 x-slots; the end word's marker nibble does not.
    local w = 0
    for i = 0, 127 do
        local idx = flip and ((o - i) % 0x10000) or ((o + i) % 0x10000)
        local word = rgn:read_u16((bank * 0x10000 + idx) * 2)
        local last = flip and ((word >> 12) & 0xF) or (word & 0xF)
        if last == 15 then return w + 3 end
        w = w + 4
    end
    return w
end

emu.register_frame_done(function()
    frames = frames + 1
    if frames > max_frames then manager.machine:exit() return end
    if frames % 10 ~= 0 then return end
    local md = manager.machine.devices[':maincpu'].spaces['program']
    local rgn = manager.machine.memory.regions[':sprites']
    local nrec, nunz, nshad, nzoom, mdeq_tot = 0, 0, 0, 0, 0
    local pp = {0, 0, 0, 0}
    local colsets = {}
    local line_rec, line_mdeq, line_px = {}, {}, {}
    for y = 0, 223 do line_rec[y] = 0; line_mdeq[y] = 0; line_px[y] = 0 end
    for i = 0, 127 do
        local base = 0x440000 + i * 16
        local d2 = md:read_u16(base + 4)
        if (d2 & 0x8000) ~= 0 then break end
        local d0 = md:read_u16(base)
        local top, bottom = d0 & 0xFF, d0 >> 8
        if (d2 & 0x4000) == 0 and top < bottom then
            local addr = md:read_u16(base + 6)
            local d4 = md:read_u16(base + 8)
            local d5 = md:read_u16(base + 10)
            local pitch = d2 & 0xFF
            if pitch >= 0x80 then pitch = pitch - 0x100 end
            local bank = (d4 >> 8) & 7
            local col = d4 & 0x3F
            local p = (d4 >> 6) & 3
            nrec = nrec + 1
            pp[p + 1] = pp[p + 1] + 1
            colsets[col] = true
            local shad = (col == 0x3F)
            if shad then nshad = nshad + 1 end
            local zoomed = (d5 & 0x3FF) ~= 0
            if zoomed then nzoom = nzoom + 1 end
            if not zoomed and not shad then nunz = nunz + 1 end
            -- width at 3 sample rows (rows step addr by pitch BEFORE
            -- drawing, vzoom=0 form; zoomed rows use the same as an
            -- upper bound)
            local flip = (d2 & 0x100) ~= 0
            local h = bottom - top
            local wmax = 0
            for _, dy in ipairs({1, (h >> 1) + 1, h}) do
                local a = (addr + pitch * dy) % 0x10000
                local w = strip_width(rgn, bank, a, flip)
                if w > wmax then wmax = w end
            end
            local mdeq = math.ceil(math.max(wmax, 1) / 32)
            if not zoomed and not shad then
                mdeq_tot = mdeq_tot + mdeq
            end
            for y = math.max(top, 0), math.min(bottom - 1, 223) do
                line_rec[y] = line_rec[y] + 1
                line_px[y] = line_px[y] + wmax
                if not zoomed and not shad then
                    line_mdeq[y] = line_mdeq[y] + mdeq
                end
            end
        end
    end
    local wr, wm, wp = 0, 0, 0
    for y = 0, 223 do
        if line_rec[y] > wr then wr = line_rec[y] end
        if line_mdeq[y] > wm then wm = line_mdeq[y] end
        if line_px[y] > wp then wp = line_px[y] end
    end
    local nc = 0
    for _ in pairs(colsets) do nc = nc + 1 end
    out:write(string.format('%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n',
        frames, nrec, nunz, nshad, nzoom, mdeq_tot, wr, wm, wp,
        pp[1], pp[2], pp[3], pp[4], nc))
    out:flush()
end)
