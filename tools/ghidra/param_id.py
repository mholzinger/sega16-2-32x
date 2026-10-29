# -*- coding: utf-8 -*-
# DO NOT RUN. Kept as the record of a measured dead end.
#
# The idea: the port reads 977 inherited 68K registers and writes none
# (FULL-PORT.md 14) because Ghidra's prototypes are empty -- 0 of 788 functions
# have parameters -- so the decompiler renders every register input as a
# free-floating `unaff_D3w` and every call site passes nothing. Enabling the
# "Decompiler Parameter ID" analyzer and re-analysing should infer parameter
# storage and generate the plumbing.
#
# MEASURED 2026-09-28. It does not work, and it is destructive:
#
#   functions      788 -> 746
#   68K code inside a function   73,314 B -> 61,118 B   (12 KB LOST)
#   prototype parameters gained  0 of 746, exactly as before
#   entries deleted              0x145A2, 0x17BC6 (created by handler_entries.py)
#
# WHY IT CANNOT WORK HERE: the analyzer infers parameters against the processor's
# CALLING CONVENTION, and the 68000 language module passes arguments on the STACK.
# Register-passed arguments require custom storage, which the analyzer will not
# invent. This program passes everything in registers, so there is nothing for it
# to find.
#
# AND THE REPO ALREADY SAID SO. tools/ghidra/f1_rebuild.sh's own header:
#   "two attempts to adjust the project in place (declaring a calling convention,
#    then reverting it) left signatures altered and the SH-2 compile rate DOWN
#    from 415/623 to 303/623. In-place edits to an analysed project are not
#    reliably reversible; re-importing is."
# That is this mistake, already paid for once. The cost of repeating it was a
# full tools/ghidra/f1_rebuild.sh cycle.
#
# Recovery, if some later run does this anyway: tools/ghidra/f1_rebuild.sh, then
# handler_entries.py with the FILTERED address list, then f1_decompile.py. Check
# the result against docs/audit/corpus_sha256.txt before building anything.
raise Exception(
    'param_id.py is a recorded DEAD END and is destructive: it took the program '
    'from 788 functions to 746 and lost 12 KB of decompiled code while gaining '
    'zero prototype parameters. Read the comment at the top of this file.')
