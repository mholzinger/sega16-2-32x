# -*- coding: utf-8 -*-
# Ghidra headless (Jython): how much of the program is reached code, how
# much is defined data, and how much the analyser never touched.
# tools/ghidra_run.sh script coverage.py
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()

total = 0
for b in mem.getBlocks():
    total += b.getSize()

fn_bytes = 0
fn_count = 0
for f in fm.getFunctions(True):
    fn_count += 1
    fn_bytes += f.getBody().getNumAddresses()

ins_bytes = 0
ins_count = 0
it = listing.getInstructions(True)
while it.hasNext():
    i = it.next()
    ins_bytes += i.getLength()
    ins_count += 1

data_bytes = 0
data_count = 0
undef_bytes = 0
it = listing.getDefinedData(True)
while it.hasNext():
    d = it.next()
    data_bytes += d.getLength()
    data_count += 1

print('TOTAL      %d bytes' % total)
print('FUNCTIONS  %d, covering %d bytes (%.1f%%)' % (fn_count, fn_bytes, 100.0 * fn_bytes / total))
print('INSTRS     %d, covering %d bytes (%.1f%%)' % (ins_count, ins_bytes, 100.0 * ins_bytes / total))
print('DEF DATA   %d items, %d bytes (%.1f%%)' % (data_count, data_bytes, 100.0 * data_bytes / total))
print('UNTOUCHED  %d bytes (%.1f%%)' % (total - ins_bytes - data_bytes,
                                        100.0 * (total - ins_bytes - data_bytes) / total))

# size histogram of functions, and the biggest ones
sizes = sorted(((f.getBody().getNumAddresses(), f.getName(),
                 f.getEntryPoint().getOffset()) for f in fm.getFunctions(True)),
               reverse=True)
print('\nLARGEST FUNCTIONS')
for n, name, ep in sizes[:15]:
    print('  0x%05X  %6d bytes  %s' % (ep, n, name))
