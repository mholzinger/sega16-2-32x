-- LOOP 17 JOB 1 — SPRITE-FRAME DISCOVERY LOGGER (arcade oracle).
--
-- Runs against MAME *arcade* altbeast, NOT the port: reads live S16B
-- object RAM (0x440000, 8 words/record, sega16sp format) and logs
-- every UNIQUE DECODE JOB it has ever seen, with the full 8-word
-- record and the exact decoded size. That unique-key set is the bake
-- set for tools/bake_sprites.py.
--
--   mame altbeast -rompath ./mame -skip_gameinfo -video none \
--       -sound none -nothrottle -autoboot_script tools/sprite_discover.lua
--   DISC_OUT=path.csv      output (default /tmp/sprite_discover.csv)
--   DISC_FRAMES=n          run length in frames (default 3900 = 65s)
--   DISC_PLAY=1            coin+start+mash schedule (gameplay coverage);
--                          omit for a pure attract pass
--
-- THE BAKE KEY is what determines the decoded PIXELS, and nothing
-- else (m_main.c compose_sprites, lines ~1475-1722):
--     bank   (d4>>8)&7      which 64K-word half of the sprite ROM
--     addr   d3             strip start; row r reads from addr+pitch*(r+1)
--     d2                    pitch (signed low byte) + flip (bit 8)
--     height bottom-top     number of strip rows walked
-- Deliberately NOT in the key:
--     xpos (d1)             placement, applied at compose time
--     colour set (d4&0x3F)  -> `base`, a runtime palette add; excluding
--                              it MERGES frames that differ only by
--                              palette pair, which raises the hit rate
--     priority (d4>>6)&3    picks the gated scalar path at runtime
--     top/bottom absolute   only height matters to the strip walk
-- Zoom (d5) is NOT part of the key either, but it IS a bake FILTER:
-- only native frames (d5 & 0x3FF == 0) can be copied verbatim; zoomed
-- records keep the live decoder, so `native` is logged per row and the
-- non-native rows are reported separately.
--
-- SIZE is measured, not guessed: at first sight of a key the strip is
-- walked exactly like the hardware (nibbles forward high-first, or
-- backward low-first when flipped; the row ends when the LAST nibble
-- of a word is 0xF -- mid-word F nibbles are ordinary skipped pixels),
-- so `words` is the real 4bpp payload the bake would have to store.
--
-- CSV (one row per unique key, most-seen first):
--   key,bank,addr,pitch,flip,height,rows_walked,words,bytes,
--   native,zoomed_seen,count,first_frame,w0..w7
local out_path   = os.getenv('DISC_OUT') or '/tmp/sprite_discover.csv'
local max_frames = tonumber(os.getenv('DISC_FRAMES') or '3900')
local do_play    = os.getenv('DISC_PLAY') == '1'

local frames = 0
local keys   = {}          -- key string -> record table
local nkeys  = 0
local nrec_total, nrec_native = 0, 0

-- scripted input (same proven pattern as tools/play_32x.lua)
local fields, inited = {}, false
local function init_fields()
    for _, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do fields[fname] = field end
    end
    inited = true
end
local warned = {}
local function set_input(name, val)
    local f = fields[name]
    if f then
        f:set_value(val)
    elseif not warned[name] then
        -- a missing field used to no-op in SILENCE, which is how a
        -- whole play pass can run with no coin inserted and still look
        -- like a clean attract run
        warned[name] = true
        print('[sprite_discover] WARNING: no input field "' .. name .. '"')
    end
end

-- One strip row, walked like the sprite chip: returns the number of
-- WORDS the row consumes (capped at 128 = 512px, the hardware's own
-- x<504 bound).
local function row_words(rgn, bank, o, flip)
    for i = 0, 127 do
        local idx = flip and ((o - i) % 0x10000) or ((o + i) % 0x10000)
        local word = rgn:read_u16((bank * 0x10000 + idx) * 2)
        local last = flip and ((word >> 12) & 0xF) or (word & 0xF)
        if last == 15 then return i + 1 end
    end
    return 128
end

-- Full frame size: every row of the strip, addressed the way
-- compose_sprites does it (addr += pitch BEFORE each drawn row, vzoom
-- 0 so no accumulator carries).
local function frame_words(rgn, bank, addr, pitch, flip, height)
    local total, rows = 0, 0
    for r = 1, height do
        local o = (addr + pitch * r) % 0x10000
        total = total + row_words(rgn, bank, o, flip)
        rows = rows + 1
        if rows >= 256 then break end       -- runaway guard
    end
    return total, rows
end

local function dump()
    local list = {}
    for _, e in pairs(keys) do list[#list + 1] = e end
    table.sort(list, function(a, b)
        if a.count ~= b.count then return a.count > b.count end
        return a.key < b.key
    end)
    local out = assert(io.open(out_path, 'w'))
    out:write('key,bank,addr,pitch,flip,height,rows_walked,words,bytes,' ..
              'native,zoomed_seen,count,first_frame,' ..
              'w0,w1,w2,w3,w4,w5,w6,w7\n')
    for _, e in ipairs(list) do
        out:write(string.format(
            '%s,%d,0x%04X,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,' ..
            '0x%04X,0x%04X,0x%04X,0x%04X,0x%04X,0x%04X,0x%04X,0x%04X\n',
            e.key, e.bank, e.addr, e.pitch, e.flip, e.height,
            e.rows, e.words, e.words * 2 + e.rows + 8,
            e.native, e.zoomed, e.count, e.first,
            e.w[0], e.w[1], e.w[2], e.w[3], e.w[4], e.w[5], e.w[6], e.w[7]))
    end
    out:close()
    print(string.format(
        '[sprite_discover] %d frames, %d records (%d native), ' ..
        '%d unique keys -> %s',
        frames, nrec_total, nrec_native, nkeys, out_path))
end

-- (no emu.register_stop in this MAME build — the frame cap is the only
-- exit that writes the CSV, so DISC_FRAMES must be reached: never kill
-- a discovery run early or it produces nothing)

emu.register_frame_done(function()
    if not inited then init_fields() end
    frames = frames + 1
    if frames > max_frames then dump(); manager.machine:exit(); return end

    if do_play then
        -- coin, start, then walk right and mash: the combat frames are
        -- the ones the attract loop never shows
        if frames == 300 or frames == 340 then set_input('Coin 1', 1) end
        if frames == 320 or frames == 360 then set_input('Coin 1', 0) end
        -- arcade field names, verified by port dump: '1 Player Start'
        -- (not 'P1 Start' -- that is the 32X shim's name)
        if frames == 420 then set_input('1 Player Start', 1) end
        if frames == 460 then set_input('1 Player Start', 0) end
        if frames > 700 then
            local p = frames % 240
            set_input('P1 Right', (p < 120) and 1 or 0)
            set_input('P1 Left',  (p >= 180) and 1 or 0)
            set_input('P1 Button 1', (p % 40 < 8) and 1 or 0)   -- attack
            set_input('P1 Button 2', (p % 90 < 6) and 1 or 0)   -- jump
        end
    end

    local md  = manager.machine.devices[':maincpu'].spaces['program']
    local rgn = manager.machine.memory.regions[':sprites']
    for i = 0, 127 do
        local base = 0x440000 + i * 16
        local d2 = md:read_u16(base + 4)
        if (d2 & 0x8000) ~= 0 then break end
        local d0 = md:read_u16(base)
        local top, bottom = d0 & 0xFF, d0 >> 8
        if (d2 & 0x4000) == 0 and top < bottom then
            local d3 = md:read_u16(base + 6)
            local d4 = md:read_u16(base + 8)
            local d5 = md:read_u16(base + 10)
            local pitch = d2 & 0xFF
            if pitch >= 0x80 then pitch = pitch - 0x100 end
            local flip   = ((d2 & 0x100) ~= 0) and 1 or 0
            local bank   = (d4 >> 8) & 7
            local height = bottom - top
            local native = ((d5 & 0x3FF) == 0) and 1 or 0
            nrec_total = nrec_total + 1
            if native == 1 then nrec_native = nrec_native + 1 end
            local key = string.format('%d_%04X_%04X_%03X',
                                      bank, d3, d2, height)
            local e = keys[key]
            if e then
                e.count = e.count + 1
                if native == 0 then e.zoomed = e.zoomed + 1 end
                if native == 1 then e.native = 1 end
            else
                local words, rows =
                    frame_words(rgn, bank, d3, pitch, flip == 1, height)
                e = {key = key, bank = bank, addr = d3, pitch = pitch,
                     flip = flip, height = height, rows = rows,
                     words = words, native = native,
                     zoomed = (native == 0) and 1 or 0,
                     count = 1, first = frames, w = {}}
                for wi = 0, 7 do e.w[wi] = md:read_u16(base + wi * 2) end
                keys[key] = e
                nkeys = nkeys + 1
            end
        end
    end
end)
