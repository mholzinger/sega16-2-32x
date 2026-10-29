# -*- coding: utf-8 -*-
# Ghidra headless (Jython): the VIDEO SURFACE inventory -- every function
# that touches System 16B video, palette, sprite or IO hardware, with the
# regions it touches and who calls it.
#   tools/ghidra_run.sh script video_map.py OUT.json
#
# Counts BOTH addressing forms. tools/ghidra/timing_census.py sees only
# operands that carry the address, and this program reaches most hardware
# through an immediate loaded into an address register
#   movea.l #$440000,a2 ; ... ; move.l (a3)+,(a2)+
# which made the sprite upload -- the single busiest video routine in the
# game -- invisible to a census (docs/log/LOOP-DECOMPILE.md 20).
import json

args = getScriptArgs()
out_path = args[0] if args else '/tmp/video_map.json'

listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()

HW = [(0x3F0000, 0x400000, 'tilebank'),
      (0x400000, 0x410000, 'tileram'),
      (0x410000, 0x411000, 'textram'),
      (0x440000, 0x450000, 'spriteram'),
      (0x840000, 0x841000, 'palette'),
      (0xC40000, 0xC44000, 'io'),
      (0xFFF0C0, 0xFFF0C8, 'mcu_mailbox'),
      (0xFFF01C, 0xFFF020, 'frameflag')]


def region(v):
    v &= 0xFFFFFF
    for lo, hi, n in HW:
        if lo <= v < hi:
            return n
    return None


rows = {}
it = listing.getInstructions(True)
while it.hasNext():
    i = it.next()
    f = fm.getFunctionContaining(i.getAddress())
    if f is None:
        continue
    for k in range(i.getNumOperands()):
        hit = None
        for o in i.getOpObjects(k):
            # Address operands answer getOffset(); immediates are Scalars and
            # answer getValue(). Asking only for getOffset() and swallowing the
            # failure silently drops every immediate-addressed hardware access,
            # which is most of them in this program.
            v = None
            for meth in ('getOffset', 'getUnsignedValue', 'getValue'):
                if hasattr(o, meth):
                    try:
                        v = getattr(o, meth)()
                        break
                    except Exception:
                        pass
            if v is None:
                continue
            r = region(v)
            if r:
                hit = (r, 'imm' if i.getOperandType(k) & 0x4000 else 'abs')
                break
        if hit is None:
            continue
        key = f.getEntryPoint().getOffset()
        e = rows.setdefault(key, {'entry': '0x%X' % key, 'name': f.getName(),
                                  'size': f.getBody().getNumAddresses(),
                                  'regions': {}, 'sites': []})
        e['regions'][hit[0]] = e['regions'].get(hit[0], 0) + 1
        e['sites'].append({'pc': '0x%X' % i.getAddress().getOffset(),
                           'mnem': i.getMnemonicString(),
                           'region': hit[0], 'form': hit[1]})

for k, e in rows.items():
    f = fm.getFunctionAt(currentProgram.getAddressFactory().getAddress(hex(k).rstrip('L')))
    e['callers'] = sorted(set(str(c.getEntryPoint()) for c in f.getCallingFunctions(monitor)))

out = sorted(rows.values(), key=lambda r: int(r['entry'], 16))
with open(out_path, 'w') as fh:
    json.dump({'functions': out}, fh, indent=1)
print('video_map: %d functions touch video hardware -> %s' % (len(out), out_path))
