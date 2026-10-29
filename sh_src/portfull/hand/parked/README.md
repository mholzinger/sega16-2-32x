# Parked hand files

Hand-derived functions that are CORRECT against the 68K listing but cannot go on the line
yet because something they enable is still broken elsewhere. cport_prep only swaps in
`hand/FUN_*.c` at the top level, so nothing here is built.

(empty, 2026-10-23: FUN_000132a0.c went up with hand/FUN_00013374.c, the head handler
derived from the 68K with the 0x13926 state table as a switch; the attract's level-2 demo
then matches the arcade's roster kind for kind: 13374 15/15, 137cc 28/28, 137e0 2/2.)
