# -*- coding: utf-8 -*-
# Ghidra headless (Jython): a COMPLETE profile of every function, so the
# whole program can be annotated rather than sampled.
#   tools/ghidra_run.sh script func_profile.py OUT.json
#
# Per function: entry, size, callers, callees, the object fields it
# touches through A6, the work-RAM globals it touches, the hardware
# regions it reaches (both addressing forms), and its terminator.
# The point is COVERAGE — 720 functions profiled mechanically beats 45
# read by hand, and the profile is what makes bulk naming possible.
import json
import re

args = getScriptArgs()
out_path = args[0] if args else '/tmp/func_profile.json'

listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()

HW = [(0x3F0000, 0x400000, 'tilebank'), (0x400000, 0x410000, 'tileram'),
      (0x410000, 0x411000, 'textram'), (0x440000, 0x450000, 'spriteram'),
      (0x840000, 0x841000, 'palette'), (0xC40000, 0xC44000, 'io')]

OBJ = re.compile(r'^\((0x[0-9a-f]+),A6')


def region(v):
    v &= 0xFFFFFF
    for lo, hi, n in HW:
        if lo <= v < hi:
            return n
    if 0xFFC000 <= v < 0xFFE000:
        return 'objtable'
    if 0xFFE000 <= v <= 0xFFFFFF:
        return 'workram'
    return None


def scalar(o):
    for m in ('getOffset', 'getUnsignedValue', 'getValue'):
        if hasattr(o, m):
            try:
                return getattr(o, m)()
            except Exception:
                pass
    return None


out = []
for f in fm.getFunctions(True):
    body = f.getBody()
    fields, globs, regions = set(), set(), set()
    last = None
    it = listing.getInstructions(body, True)
    while it.hasNext():
        i = it.next()
        last = i.getMnemonicString()
        for k in range(i.getNumOperands()):
            rep = str(i.getDefaultOperandRepresentation(k))
            m = OBJ.match(rep)
            if m:
                fields.add(int(m.group(1), 16))
            for o in i.getOpObjects(k):
                v = scalar(o)
                if v is None:
                    continue
                r = region(v)
                if r:
                    regions.add(r)
                    if r in ('workram', 'objtable'):
                        globs.add(v & 0xFFFFFF)
    out.append({
        'entry': '0x%X' % f.getEntryPoint().getOffset(),
        'size': body.getNumAddresses(),
        'callers': len(list(f.getCallingFunctions(monitor))),
        'callees': sorted('0x%X' % c.getEntryPoint().getOffset()
                          for c in f.getCalledFunctions(monitor)),
        'obj_fields': sorted('0x%02X' % x for x in fields),
        'globals': sorted('0x%06X' % x for x in globs)[:24],
        'regions': sorted(regions),
        'ends': last,
    })

out.sort(key=lambda r: int(r['entry'], 16))
with open(out_path, 'w') as fh:
    json.dump({'functions': out}, fh, indent=1)
print('func_profile: %d functions -> %s' % (len(out), out_path))
