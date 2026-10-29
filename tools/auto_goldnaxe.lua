-- Golden Axe level-1 play driver for headless probes (arcade or our rom).
-- Coin f600, '1 Player Start' f800, Button 1 at f900 picks the first hero,
-- walk right from f1100 with an attack every 300 frames. Deterministic
-- under MAME's headless run; the write census verified the window
-- f1500-f3000 is the intro cutscene into stage-1 play (LOOP-DECOMPILE-GOLDNAXE 5).
local fields, inited, af = {}, false, 0
local function set_input(n, v) local f = fields[n]; if f then f:set_value(v) else print('no input field ' .. n) end end
emu.register_frame_done(function()
    if not inited then
        for _, port in pairs(manager.machine.ioport.ports) do for fname, field in pairs(port.fields) do fields[fname] = field end end
        inited = true
    end
    af = af + 1
    if af == 600 then set_input('Coin 1', 1) end
    if af == 610 then set_input('Coin 1', 0) end
    if af == 800 then set_input('1 Player Start', 1) end
    if af == 820 then set_input('1 Player Start', 0) end
    if af == 900 then set_input('P1 Button 1', 1) end
    if af == 910 then set_input('P1 Button 1', 0) end
    if af == 1100 then set_input('P1 Right', 1) end
    if af >= 1200 and (af - 1200) % 300 == 0 then set_input('P1 Button 1', 1) end
    if af >= 1208 and (af - 1208) % 300 == 0 then set_input('P1 Button 1', 0) end
end)
