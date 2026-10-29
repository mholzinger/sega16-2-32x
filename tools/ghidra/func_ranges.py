# -*- coding: utf-8 -*-
# Ghidra headless (Jython): every function's TRUE body ranges.
#   tools/ghidra_run.sh script func_ranges.py docs/audit/func_ranges.txt
#
# WHY THIS EXISTS. The corpus header says `(%d B)` from
# getBody().getNumAddresses() -- a COUNT of bytes in the body, which may be
# DISJOINT. Any analysis that treats it as an extent (`start <= a < start+size`)
# is wrong for every function whose body is in more than one piece: it claims
# addresses the function does not contain and misses ones it does.
#
# Measured 2026-09-28: that model put the end of FUN_00007ca0 at 0x0809C, in the
# MIDDLE of the 4-byte `clr.b (0x21,A6)` at 0x0809A, and produced a "102 byte
# coverage loss" that does not exist. The coverage arithmetic in an earlier pass
# of FULL-PORT.md 12 was computed the same way and is not to be trusted.
#
# A FUNCTION'S ENTRY POINT IS NOT ITS BODY'S LOWEST ADDRESS. Measured 2026-09-28:
# FUN_00018146 has body 0x18126-0x18161 and entry 0x18146 -- code before the entry
# belongs to the function (a shared prologue reached by a branch). So "is address A
# a function entry" must be asked of the ENTRY column, never of min(body).
#
# Format, one line per range (a function with a split body has several):
#   0xSTART 0xEND_INCLUSIVE 0xENTRY NAME
print_lines = []
fm = currentProgram.getFunctionManager()
args = getScriptArgs()
out = args[0] if args else 'docs/audit/func_ranges.txt'

n_fn = 0
n_split = 0
n_offset_entry = 0
total = 0
for f in fm.getFunctions(True):
    n_fn += 1
    body = f.getBody()
    rs = [r for r in body.getAddressRanges()]
    if len(rs) > 1:
        n_split += 1
    if rs and f.getEntryPoint().getOffset() != min(r.getMinAddress().getOffset() for r in rs):
        n_offset_entry += 1
    for r in rs:
        a = r.getMinAddress().getOffset()
        b = r.getMaxAddress().getOffset()
        total += b - a + 1
        print_lines.append('0x%05X 0x%05X 0x%05X %s'
                           % (a, b, f.getEntryPoint().getOffset(), f.getName()))

fh = open(out, 'w')
fh.write('# %d functions, %d of them with a DISJOINT body, %d ranges, %d bytes.\n'
         % (n_fn, n_split, len(print_lines), total))
fh.write('# start end_inclusive ENTRY name -- tools/ghidra/func_ranges.py.\n')
fh.write('# Use THIS for address ownership, never start+getNumAddresses().\n')
for l in print_lines:
    fh.write(l + '\n')
fh.close()
print('func_ranges: %d functions, %d disjoint, %d whose ENTRY is not the body '
      'minimum, %d ranges, %d bytes -> %s'
      % (n_fn, n_split, n_offset_entry, len(print_lines), total, out))
