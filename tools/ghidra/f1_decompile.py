# -*- coding: utf-8 -*-
# Name every function from docs/audit/function_map.md, then export the whole
# program as C.  This is the artifact the port consumes.
#   tools/ghidra_run.sh script f1_decompile.py OUT.c [timeout_s]
#
# Cavern Crawl was reconstructed against exactly this (analysis/decompiled/
# cavern_all.c in ~/src/kyocera-2235), so the shape is proven: one C file, one
# function per Ghidra function, named where the map names it and classed where
# it only classes it. Plumbing (74% of bytes: tile fill, sprite animation,
# palette, sound) is portable nearly as-is; the 35 object handlers are the part
# that needs reading.
import re, os, time
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

args = getScriptArgs()
OUT = args[0] if args else '/tmp/altbeast.c'
TMO = int(args[1]) if len(args) > 1 else 60

ROOT = '/Users/mikeholzinger/src/sega16-2-32x'
NAMES, CLASSES = {}, {}
row = re.compile(r'\|\s*0x([0-9A-Fa-f]+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*([^|]*?)\s*\|')
for l in open(os.path.join(ROOT, 'docs/audit/function_map.md')):
    m = row.match(l)
    if not m:
        continue
    a, cl = int(m.group(1), 16), m.group(4).strip()
    CLASSES[a] = cl
    # A class that is a bare TAG is a classification, not a name. Naming
    # functions after tags produced `hardware()`, `motion()`, `palette()` --
    # colliding, meaningless, and it broke compilation (2026-09-26).
    TAGWORDS = ('signature', 'hardware', 'motion', 'palette', 'sound',
                'collision', 'animation', 'draws', 'despawns')
    if (re.match(r'^[a-z][a-z0-9_]*$', cl) and cl not in TAGWORDS
            and '_' in cl):                       # a real name has structure
        NAMES[a] = cl

fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
from ghidra.program.model.symbol import SourceType
import java.lang.Throwable

# `except Exception` DOES NOT CATCH A JAVA EXCEPTION HERE. setName raises
# ghidra.util.exception.DuplicateNameException when a LABEL of that name already
# sits at the address -- which is what a project-side rename leaves behind
# (main_entry at 0x005BE, after tools/ghidra/split_frame_body.py). Jython let it
# through this handler and the whole export died AFTER WRITING NOTHING, so the next
# step in the chain read a day-old file and believed it (2026-10-02). Catch the Java
# side explicitly, and say which rename was skipped rather than passing silently:
# the map is a naming SOURCE, not an authority over a deliberate project edit.
# Runs under Jython (Ghidra 11.x) and PyGhidra/CPython 3 (Ghidra 12.x): the
# `except X as e` form is the one syntax both accept (2026-10-23).
named = skipped = 0
for f in fm.getFunctions(True):
    a = f.getEntryPoint().getOffset()
    if a in NAMES and f.getName() != NAMES[a]:
        try:
            f.setName(NAMES[a], SourceType.USER_DEFINED); named += 1
        except java.lang.Throwable as e:
            print('  KEPT the project name %s at 0x%05X (map says %s): %s'
                  % (f.getName(), a, NAMES[a], e.getClass().getSimpleName()))
            skipped += 1
        except Exception as e:
            print('  KEPT the project name %s at 0x%05X: %s' % (f.getName(), a, e))
            skipped += 1
print('named %d functions from the map, %d kept from the project (%d names '
      'available, %d classes)' % (named, skipped, len(NAMES), len(CLASSES)))

di = DecompInterface()
di.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
ok = fail = 0
t0 = time.time()
with open(OUT, 'w') as fh:
    fh.write('/* Altered Beast (System 16B, US set 8) decompiled from\n'
             ' * roms/altbeast/prog68k.bin by tools/ghidra/f1_decompile.py.\n'
             ' * Function classes come from docs/audit/function_map.md; a class\n'
             ' * naming several tags is a SIGNATURE hypothesis, not a reading\n'
             ' * (only 46 of 560 map entries are marked READ).\n */\n\n')
    for f in fm.getFunctions(True):
        a = f.getEntryPoint().getOffset()
        cl = CLASSES.get(a, '?')
        res = di.decompileFunction(f, TMO, mon)
        fh.write('/* ---- 0x%05X  %s  (%d B)  class: %s ---- */\n'
                 % (a, f.getName(), f.getBody().getNumAddresses(), cl))
        if res and res.decompileCompleted():
            fh.write(res.getDecompiledFunction().getC())
            ok += 1
        else:
            fh.write('/* DECOMPILE FAILED: %s */\n'
                     % (res.getErrorMessage() if res else 'no result'))
            fail += 1
        fh.write('\n')
print('decompiled %d ok, %d failed, %.0fs -> %s' % (ok, fail, time.time() - t0, OUT))
