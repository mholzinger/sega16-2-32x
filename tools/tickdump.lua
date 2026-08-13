-- LOOP 13 tick-row hunt: dump the MD VDP's REAL stores (emu.item on
-- :gen_vdp — the videoram space is fake, see LOOP12) at several
-- attract-mode frames, so the fixed row of dashes on the sky can be
-- traced to nametable cells + patterns + palette offline.
--
--   TD_OUT=parity_tick/td mame 32x -cart rom/s16.32x -rompath ./mame \
--     -skip_gameinfo -video none -sound none -nothrottle \
--     -autoboot_script tools/tickdump.lua
--
-- Emulator-side only; nothing in the guest is perturbed.

local OUT = os.getenv('TD_OUT') or 'tickdump'
local AT = {2400, 3600, 4800, 6000}
local frames = 0

local function dump_item(items, key, path, nbytes)
    local it = emu.item(items[key])
    if not it then print("NO ITEM " .. key) return end
    local f = assert(io.open(path, "wb"))
    local ok, blob = pcall(function() return it:read_block(0, nbytes) end)
    if ok and blob and #blob == nbytes then
        f:write(blob)
    else
        for i = 0, nbytes - 1 do
            f:write(string.char(it:read(i) % 256))
        end
    end
    f:close()
end

emu.register_frame_done(function()
    frames = frames + 1
    for i, tf in ipairs(AT) do
        if frames == tf then
            local dev = manager.machine.devices[":gen_vdp"]
            if not dev then print("NO :gen_vdp") return end
            local items = dev.items
            if frames == AT[1] then   -- once: list keys for the record
                for k in pairs(items) do print("item: " .. k) end
            end
            local tag = OUT .. "_f" .. tf
            dump_item(items, "0/m_vram", tag .. ".vram", 65536)
            pcall(function() dump_item(items, "0/m_cram", tag .. ".cram", 128) end)
            pcall(function() dump_item(items, "0/m_vsram", tag .. ".vsram", 80) end)
            pcall(function() dump_item(items, "0/m_regs", tag .. ".regs", 32) end)
            print("dumped frame " .. tf)
            if tf == AT[#AT] then manager.machine:exit() end
        end
    end
end)
