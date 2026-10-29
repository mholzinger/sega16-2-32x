# -*- coding: utf-8 -*-
# Ghidra headless (Jython) script -- palette-demand census for the
# decompile thread (docs/handoff/HANDOFF-DECOMPILE.md question 1).
# Run via: tools/ghidra_run.sh script palette_census.py OUT.json
#
# Emits every write to object offsets $0A and $0B (the palette fields of
# the object struct) with:
#   addr, func, size, dest_off, src_kind, src (immediate value if any)
# src_kind is 'imm', 'reg', 'objtab' (sourced from another field of the
# same object), 'romtab' (indexed off an address register) or 'other'.
# Offsets alone do not prove the base register holds an object; sites
# whose base is not A6 are reported with their register so a reader can
# judge. Addresses are the ARCADE map.
import json, re

args = getScriptArgs()
out_path = args[0] if args else '/tmp/palette_census.json'

listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()

DEST = re.compile(r'^\(0x([ab]),(A\d)\)$')

def classify(rep):
    if rep.startswith('#'):
        return 'imm'
    if re.match(r'^[AD]\d[bwl]?$', rep):
        return 'reg'
    m = re.match(r'^\(0x[0-9a-f]+,(A\d)', rep)
    if m:
        return 'objtab' if m.group(1) == 'A6' else 'romtab'
    return 'other'

rows = []
ins = listing.getInstructions(True)
while ins.hasNext():
    i = ins.next()
    if i.getNumOperands() != 2:
        continue
    mn = i.getMnemonicString()
    if not mn.startswith('move'):
        continue
    drep = str(i.getDefaultOperandRepresentation(1))
    m = DEST.match(drep)
    if not m:
        continue
    srep = str(i.getDefaultOperandRepresentation(0))
    f = fm.getFunctionContaining(i.getAddress())
    val = None
    if srep.startswith('#'):
        try:
            val = int(srep[1:], 16) if srep[1:3] == '0x' else int(srep[1:])
        except ValueError:
            val = None
    rows.append({
        'addr': '0x%X' % i.getAddress().getOffset(),
        'func': f.getName() if f else None,
        'mnem': mn,
        'dest_off': '0x' + m.group(1),
        'base': m.group(2),
        'src': srep,
        'src_kind': classify(srep),
        'imm': val,
    })

rows.sort(key=lambda r: int(r['addr'], 16))
with open(out_path, 'w') as fh:
    json.dump({'writes': rows}, fh, indent=1)
print('palette_census: %d writes -> %s' % (len(rows), out_path))
