# -*- coding: utf-8 -*-
# Print address + mnemonic + operands for a 68K address range.
#   tools/ghidra_run.sh script disasm_range.py 0x3CD4 0x3D14 [OUT.txt]
#
# WHY A NEW SCRIPT: instr_list.py writes ADDRESSES ONLY, which cannot answer "what
# does this function do with D3". Reading 64 bytes of 68000 is often the shortest
# path to a question the decompiler has modelled wrongly -- here zoom_scale_lookup,
# whose six outputs do not fit Ghidra's two-register return (FULL-PORT.md 30c).
import sys
from ghidra.program.model.address import AddressSet

args = getScriptArgs()
lo = int(args[0], 16) if len(args) > 0 else 0
hi = int(args[1], 16) if len(args) > 1 else lo + 0x40
out = args[2] if len(args) > 2 else None

sp = currentProgram.getAddressFactory().getDefaultAddressSpace()
listing = currentProgram.getListing()
lines = []
a = sp.getAddress(lo)
end = sp.getAddress(hi)
ins = listing.getInstructionAt(a)
if ins is None:
    ins = listing.getInstructionAfter(a)
while ins is not None and ins.getAddress().getOffset() < hi:
    ad = ins.getAddress()
    lines.append('%06X  %-10s %s' % (ad.getOffset(), ins.getMnemonicString(),
                                     ins.toString()[len(ins.getMnemonicString()):].strip()))
    ins = ins.getNext()

text = '\n'.join(lines)
print('disasm_range: 0x%X-0x%X, %d instructions' % (lo, hi, len(lines)))
print(text)
if out:
    f = open(out, 'w')
    f.write(text + '\n')
    f.close()
