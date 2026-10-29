# CARD: the rig tools no longer have a default (2026-09-27, builder -> decompile)

**This will break your next rig command, and that is the point.** Mike caught a
rom going to mister.office.local twice. Rather than be more careful, the
default is gone from all four rig tools: they now REFUSE to run without an
explicit rig and print both names.

**mister.office.local is yours. mister.tv.local is the builder's.**

## What to change in your invocations

| tool | before | now |
|---|---|---|
| `tools/mister_push.sh` | `tools/mister_push.sh rom/x.32x` | `tools/mister_push.sh --name mister.office.local rom/x.32x` |
| `tools/rig_value.py` | `python3 tools/rig_value.py ...` | `MISTER=root@mister.office.local python3 tools/rig_value.py ...` |
| `tools/rig_barcode.py` | `python3 tools/rig_barcode.py ...` | `MISTER=root@mister.office.local python3 tools/rig_barcode.py ...` |
| `tools/rig_blackcells.sh` | `tools/rig_blackcells.sh ...` | `MISTER=root@mister.office.local tools/rig_blackcells.sh ...` |

`mister_push.sh` also accepts `MISTER=`/`MISTER_HOST=`; `--name` wins over both
and sets the ssh target and the launch API host together. All four guards were
tested both ways (refuse without, work with) before this card was written.

## Why it had to be fail-closed rather than fail-careful

The misfire is invisible from the sending side: the launch API answers
`INFO game started` whichever machine took it. There is no readback that says
which rig ran, so a wrong-rig push is only ever caught by the person sitting in
front of the other TV. A default is not a convenience when two targets are
equally valid and only the operator knows which -- it is a silent wrong answer.

If a script of yours wraps any of the four, it needs the same one-line change.

## Two findings from the test cart you may want, unrelated to the above

**1. The FPGA taxes plain work-RAM access, not only cart reads.** Relevant to
HANDOFF-68K-FETCH.md, where the FPGA's excess is attributed to the core's ROM
DTACK wait (`IF.sv USE_ROM_WAIT=1`), a CART-read penalty. TESTCART's T08 is a
1K work-RAM word-read loop executing FROM work RAM -- it touches no cart and no
VDP -- and the FPGA reads 0x5D = 93 lines against ares' 0x5A = 90. **+3 lines,
with no cart access in the loop at all**, against cart read's +5 (T09). The
harness overhead is identical on both machines and cancels in the delta, so the
+3 stands on its own. It does not contradict USE_ROM_WAIT; it says that wait is
not the whole story.

**2. 68000 cycle counts predict the timing tests, so they can stop being
baselines.** The loops are LTO-inlined into `main` and live in .data; get them
with `objdump -D rom/md_start.elf` (~0xFF2670). T08 is
`MOVE.W (xxx).L,Dn` 16 + `MOVE.W Dn,(d16,An)` 12 + `SUBQ.W` 4 + `Bcc.B` taken
10 = 42 cycles; x1024 / 488.57 cycles per NTSC line = **88.0 lines** against 90
measured. T13 (byte, also 42) predicts 88.0, reads 90. T14 (long,
20+16+4+10 = 50) predicts 104.8, reads 107. The residual is CONSTANT at ~2
lines rather than scaling with the loop, so it is fixed harness overhead and
not a broken cycle table.

The cart now carries a provenance letter per row (C computed / R from RTL /
D documented / E one ares run only) with an on-screen key. 24 of 36 are E, and
that list is the worklist. If you have a derivation for any of the 68K timing
rows, it converts one.
