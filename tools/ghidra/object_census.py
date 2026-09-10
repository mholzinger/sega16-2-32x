# -*- coding: utf-8 -*-
# Ghidra headless (Jython): derive the OBJECT STRUCT by census.
#   tools/ghidra_run.sh script object_census.py OUT.json
#
# The dispatcher at 0x398E walks 64 slots of 128 bytes from 0xFFC000 and
# calls each slot's routine pointer at offset 2 with A6 (fp) holding the
# slot base (docs/log/LOOP-DECOMPILE.md 28). Handlers inherit that
# convention, so every (d16,A6) access in the program is a field access
# on the current object.
#
# This counts them: per offset, how many instructions touch it, at what
# size, reading or writing, and from how many distinct functions. The hot
# offsets ARE the struct — derived, not guessed. Naming still needs the
# running-frame check; this only says where the fields are.
import json
import re

args = getScriptArgs()
out_path = args[0] if args else '/tmp/object_census.json'

listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()

OBJ_SIZE = 128

# (0x6c,A6) or (0x6c,A6,D0w*0x1) — displacement off A6, indexed or not
DISP = re.compile(r'^\((0x[0-9a-f]+|-0x[0-9a-f]+),A6(,|\))')

SIZED = {'.b': 1, '.w': 2, '.l': 4}


def op_size(mn):
    for suf, n in SIZED.items():
        if mn.endswith(suf):
            return n
    return 0


rows = {}
it = listing.getInstructions(True)
while it.hasNext():
    i = it.next()
    f = fm.getFunctionContaining(i.getAddress())
    mn = i.getMnemonicString()
    n = i.getNumOperands()
    for k in range(n):
        rep = str(i.getDefaultOperandRepresentation(k))
        m = DISP.match(rep)
        if not m:
            continue
        try:
            off = int(m.group(1), 16)
        except ValueError:
            continue
        if off < 0 or off >= OBJ_SIZE:
            continue          # not a field of the current object
        e = rows.setdefault(off, {'offset': '0x%02X' % off, 'reads': 0,
                                  'writes': 0, 'sizes': {}, 'funcs': set(),
                                  'indexed': 0, 'sample': []})
        # operand 1 of a 2-operand move is the destination
        is_write = (n == 2 and k == 1) or (n == 1 and mn.startswith(('clr', 'st', 'sf')))
        if is_write:
            e['writes'] += 1
        else:
            e['reads'] += 1
        sz = op_size(mn)
        if sz:
            e['sizes'][sz] = e['sizes'].get(sz, 0) + 1
        if ',D' in rep and '*' in rep:
            e['indexed'] += 1
        if f is not None:
            e['funcs'].add(f.getEntryPoint().getOffset())
        if len(e['sample']) < 3:
            e['sample'].append('0x%X %s' % (i.getAddress().getOffset(), str(i)))

out = []
for off in sorted(rows):
    e = rows[off]
    e['funcs'] = len(e['funcs'])
    e['sizes'] = dict(('%d' % k, v) for k, v in sorted(e['sizes'].items()))
    e['total'] = e['reads'] + e['writes']
    out.append(e)

with open(out_path, 'w') as fh:
    json.dump({'object_size': OBJ_SIZE, 'fields': out}, fh, indent=1)

touched = len(out)
total = sum(e['total'] for e in out)
print('object_census: %d of %d offsets touched, %d accesses -> %s'
      % (touched, OBJ_SIZE, total, out_path))
