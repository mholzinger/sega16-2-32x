-- LOOP 19: does the late claim actually close the one-cycle gap?
--   make ... SPRLATE=1   then run this.
-- [3] is the verdict: sprites that still drew with pr==0xFF, i.e. in the
-- shadow ramp. It must reach 0, and it must be shown to FIRE when the
-- claim is disabled, or the 0 means nothing.
local f, sh2 = 0, nil
local fields, inited = {}, false
local function init_fields()
    for _, p in pairs(manager.machine.ioport.ports) do
        for n, fl in pairs(p.fields) do fields[n] = fl end
    end
    inited = true
end
local function si(n,v) local x=fields[n] if x then x:set_value(v) end end
emu.register_frame_done(function()
    if not inited then init_fields() end
    f = f + 1
    if f == 600 or f == 800 then si('P1 Start',1); si('P1 A',1) end
    if f == 640 or f == 840 then si('P1 Start',0); si('P1 A',0) end
    if f == 1000 then si('P1 Start',1) end
    if f == 1040 then si('P1 Start',0) end
    if f == 1800 then si('P1 Right',1) end
    if f == 2400 then si('P1 A',1) end
    if f == 2410 then si('P1 A',0) end
    if f == 3000 then si('P1 Right',0) end
    if f == 3600 then si('P1 Left',1) end
    if f == 4200 then si('P1 Left',0) end
    if not sh2 then
        for tag, c in pairs(manager.machine.devices) do
            if tag:find('sh2') and c.spaces['program'] then sh2 = c.spaces['program']; break end
        end
        if not sh2 then return end
    end
    if f % 900 ~= 0 then return end
    -- READ THE CACHED (0x06) ALIAS. Proven by sentinel: the rom writes
    -- 0xA5A5A500 through its uncached 0x2603A7D8 pointer and MAME's SH-2
    -- debugger space returns it at 0x0603A7D8 and ZERO at 0x2603A7D8 --
    -- the 0x26 alias is not served to the debugger at all. Read fixed-
    -- block counters at 0x06 in lua, always. Reading 0x26 yields a
    -- silent, uniform zero that looks exactly like "the feature never
    -- fired" and cost a full debugging round here.
    local b = 0x0603A780
    local missed, nofree, claimed, shadow =
        sh2:read_u32(b), sh2:read_u32(b+4), sh2:read_u32(b+8), sh2:read_u32(b+12)
    print(string.format(
        'f=%d  map missed %d sets  claimed %d  no free pair %d  ||  DREW IN SHADOW RAMP: %d',
        f, missed, claimed, nofree, shadow))
    local want, got, share, bound =
        sh2:read_u32(b+16), sh2:read_u32(b+20), sh2:read_u32(b+24), sh2:read_u32(b+28)
    print(string.format(
        '     CAPACITY: worst sets wanting a pair %d | got their own %d | FORCED TO SHARE %d | tightest bound %d (pairs for sprites = (32-bound)/2)',
        want, got, share, bound))
    local lo, hi = sh2:read_u32(b+32), sh2:read_u32(b+36)
    print(string.format(
        '     GROUPS: tile zone uses %d of %d below bound | LIVE SINGLES SQUATTING in the sprite pair zone: %d',
        lo, bound, hi))
    local lofree, nsq = sh2:read_u32(b+40), sh2:read_u32(b+44)
    print(string.format(
        '     RELOCATION: %d cycles had a squatter; WORST free groups below bound at that moment = %s',
        nsq, lofree == 0 and 'n/a' or tostring(lofree - 1)))
    local rel, norel = sh2:read_u32(b+48), sh2:read_u32(b+52)
    print(string.format(
        '     RELOC: squatters moved out of the reserved zone %d | no free low group %d',
        rel, norel))
    print(string.format('     STALE HOLDS: reserved pairs owned by an ABSENT set %d',
        sh2:read_u32(b+60)))
    print(string.format('     TILEDEDUP: colours sharing an identical palette %d',
        sh2:read_u32(b+56)))
    local pk, pkd = 0, 0
    local tot, totd = sh2:read_u32(b+56), sh2:read_u32(b+60)
    print(string.format(
        '     DEDUP: peak cycle %d live sets = %d distinct palettes | COLOURS: %d distinct of %d entries (%.2fx) -> fits in %d pairs',
        pk, pkd, tot, totd, totd > 0 and totd/math.max(tot,1) or 0,
        math.ceil(tot / 15)))
    if f >= 5400 then manager.machine:exit() end
end)
