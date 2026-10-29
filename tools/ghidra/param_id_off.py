# -*- coding: utf-8 -*-
# Turn Decompiler Parameter ID back OFF.
#   tools/ghidra_run.sh script param_id_off.py
#
# MEASURED 2026-09-28 AND IT IS A DEAD END, recorded so nobody pays for it twice:
# enabling Decompiler Parameter ID and re-analysing took the program from 788
# functions to 746, coverage from 73,314 bytes of 68K code to 61,118 (12 KB LOST),
# deleted two of the entries handler_entries.py had created (0x145A2, 0x17BC6),
# and produced ZERO prototype parameters -- 0 of 746, exactly as before.
#
# Why it cannot work here: the analyzer infers parameters against the processor's
# CALLING CONVENTION, and the 68000 language module passes arguments on the STACK.
# Register-passed arguments need custom storage, which the analyzer will not
# invent. This program's routines pass everything in registers, so there is
# nothing for it to find, and re-analysis discards the object-table entries that
# only exist because a script created them.
from ghidra.program.model.listing import Program
opts = currentProgram.getOptions(Program.ANALYSIS_PROPERTIES)
TOP = 'Decompiler Parameter ID'
print('param_id_off: %s was %s' % (TOP, opts.getBoolean(TOP, False)))
opts.setBoolean(TOP, False)
print('param_id_off: now %s' % opts.getBoolean(TOP, False))
