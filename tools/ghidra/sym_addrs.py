# -*- coding: utf-8 -*-
# Ghidra headless (Jython): the ADDRESS of every symbol whose name encodes a
# pointer or data cell, so the port can place it instead of fabricating it.
#   tools/ghidra_run.sh script sym_addrs.py OUT.txt
#
# WHY. tools/cport_layout.py places absolute symbols by matching their NAMES:
# `(DAT|[a-z]{1,3}Ram)_<hex>`. Ghidra also invents `PTR_FUN_00006d90`,
# `PTR_DAT_00019848`, `switchD_00002ef0` -- and those do NOT match, so they fall
# through to cport_prep's catch-all and are DEFINED IN defs.c AS 64-BYTE ZEROED
# ARRAYS.
#
# That is not a cosmetic gap. `PTR_FUN_00006d90` is the base of a jump table in
# ROM, and the port reads zeros out of it: measured 2026-09-28, 208 of the frame's
# indirect calls resolved to address 0x00000 and were counted as misses instead of
# dispatched. The object state machines never run.
#
# A name like PTR_FUN_00006d90 encodes its TARGET (0x6D90), not its own address,
# so the address cannot be recovered from the name. Ghidra has it.
#
# Format: 0xADDRESS NAME
import re

args = getScriptArgs()
out = args[0] if args else '/tmp/sym_addrs.txt'
st = currentProgram.getSymbolTable()
PAT = re.compile(r'^(PTR_|switchD?_|caseD_|s_|_?DAT_|[a-z]{1,3}Ram)')
rows = []
for s in st.getAllSymbols(True):
    nm = s.getName()
    if not PAT.match(nm):
        continue
    a = s.getAddress()
    if a is None or not a.isMemoryAddress():
        continue
    rows.append((a.getOffset(), nm))
rows.sort()
fh = open(out, 'w')
fh.write('# %d symbols: address NAME. tools/ghidra/sym_addrs.py\n' % len(rows))
for a, nm in rows:
    fh.write('0x%08X %s\n' % (a, nm))
fh.close()
print('sym_addrs: %d symbols -> %s' % (len(rows), out))
