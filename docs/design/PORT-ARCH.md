# PORT-ARCH — the approved architecture for finishing the SH-2 port

**Mike approved this on 2026-10-02, in this order: 1, then 2, then 3.** It
supersedes any sequencing in FULL-PORT.md §26-§33, which is the execution log that
led here.

---

## 0. WHERE THINGS STAND (the banked state)

    rom/banked.32x
    make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 RIGBARCODE=1 WRAMCODE=1
    python3 tools/portl2_seed.py  rom/X.32x 64      # ONE seeded frame, cell by cell
    python3 tools/portl2m_read.py rom/X.32x 40000   # the 176-frame gate, both curves
    python3 tools/port_guard.py                     # 3 build-time checks
    python3 tools/port_loop.py                      # the adoption path

    seed frame      9 wrong of 16384   control 52   net -43
    per-frame       mean 19.3          control 43.8 net -24.5   NET CORRECT
    free-run        mean 147.0         control 137.1 net +9.9; last +45  DIVERGING
    chain D3        0x12 -- the arcade's value
    sprite records  BYTE-CORRECT, all fields +0..+9 on records 0, 16, 17
    frame cost      ~0.2 ms on ares = ~2% of a vint on hardware

**THE GATE IS 176 FRAMES, NOT 256.** The delta chain was trimmed to make cart room.
The control moved with it (per-frame 39.7 -> 43.8, free-run 161.1 -> 137.1), so
numbers in FULL-PORT.md §30e and earlier are **NOT comparable** to anything after
§33a. Never quote across that boundary.

**WHAT THIS BUILD DOES:** it MEASURES, it does not play. The port runs on the SH-2
master against a MIRROR of work RAM at `0x0602CC00` (`portfull/layout.ld:6`) and
drives ZERO pixels. The picture comes from the 68K pipeline as usual, degraded
because NOSPRBAKE and NOTILESMD drop the baked blobs. THE LINE is still
`rom/night/ring1.32x` and nothing here touches it.

---

## 1. CALL DECOMPILED CODE. NEVER REIMPLEMENT CONTROL FLOW.  <-- DONE, AND ADOPTED

**2026-10-02: the real frame BEATS the hand-rolled body and is now the frame body.**

    four-call body (the old baseline)   9 wrong of 16384   control 52   net -43
    the rotated real frame              8 wrong                         net -44

    make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 PORTSEED=64 PORTMAIN=1 PORTOS=1 \
         RIGBARCODE=1 WRAMCODE=1

16,376 of 16,384 work-RAM bytes, the decompiler's own branching, and the 68K's own
stack top on the arcade's value (0x0AE2). Of the 8 left, SEVEN are wrong under both
bodies -- they are not about the frame loop. FULL-PORT.md 34-41.

The route was: root the ROTATED frame at 0x0AE2 (not 0x005BE, which runs the
state-init pass), hand-write `FUN_0000E16A` (the decompiler cannot express `movem` to
`-(SP)`), give the gate the frame's OTHER input (the controller and dips), and fix two
generator faults -- an over-wide declaration that truncated to the wrong half, and a
self-relative ROM table mistaken for a 16-bit work-RAM pointer.

---

## 1a. (superseded) THE ORIGINAL ARGUMENT FOR DOING THIS FIRST

**The step:** add **`0x005BE`** as a root and call it once per frame. Delete the
hand-rolled frame body.

That function (0x005BE-0x00BD5, 1,560 bytes) contains the whole frame loop at
0x970-0xADC. Ghidra named it `test_mode_screen`; **the name is wrong** -- it is the
main routine. It is NOT in the unit today.

**Why a list cannot work** (FULL-PORT.md §33, `docs/audit/frame_loop.asm`):

* **It has branches.** `btst.b #0,(0xfffff026).w ; bne 0xABE` conditionally skips
  the 0x0DFA call; `cmpi.b #6,(0xfffff142).w` gates more. A flat unconditional list
  runs calls the arcade SKIPS.
* **Two calls were missing** from the oracle's fifteen: 0x09052 at 0x996, 0x17188 at
  0x0ACE. The L1 window did not reach them.
* **A6 is never set** in that range, so routing through `port_call68_at` (which
  passes `port_cur_regs`, whose `[14]` is the last dispatched SLOT) handed ten of
  fourteen an A6 pointing at an object record.

`tools/port_loop.py` measured the flat list at every length: 1 call net -6, 2-4 +2,
5 +9, 6-7 +17, all 14 +45 and hanging in 0x0E052 -- against **-43** for the four
direct calls. Monotonically worse. Reverted.

**The general rule this establishes:** hand-write ONLY what the decompiler
structurally cannot express, and then transcribe from disassembly with the listing
archived. `zoom_scale_lookup` qualifies -- six outputs (A0,D0,D1,D3,D6,D7) against a
two-register return -- and transcribing it from `docs/audit/zoom_scale_lookup.asm`
was right on the first attempt. `port_object_dispatch` qualifies. **A sequence of
addresses never qualifies.**

Expect most of the free-run divergence to be the missing two thirds of the frame.

### BUILT 2026-10-02 -- `make ... PORTMAIN=1 PORTOS=1` (FULL-PORT.md 34)

`0x005BE` is rooted and the mechanism is control INVERSION, not a call:
`wait_frame_flag` (0x397E, the `jsr` four instructions into the loop at 0x640) is
REDIRECTED to `port_frame_wait` in `sh_src/port_l2.c`. The game loop drives; this
driver is its vsync. Seed at yield 1 (after the prologue, which clears
text/tile/sprite RAM and would erase a seed placed earlier -- and which is also
where the arcade's capture was taken); `__builtin_longjmp` out at yield 2.

Three facts the step forced, each with its own cost:

* **FREE-RUN IS REFUSED under PORTMAIN** (`'NOFR'` in `port_l2m_out[195]`). A
  longjmp cannot resume, so every frame re-runs the prologue and nothing
  accumulates; the curve would have duplicated the per-frame curve under free-run's
  name. A real resume needs a second stack. Own step.
* **`.portcode` overflowed by 10,980 bytes.** The closure went 429 -> 716
  functions. `-Os` is 352,820 B against -O2's 436,752 (-19%) and fits, so PORTMAIN
  requires `PORTOS=1`. It is a second variable and is A/B'd, not assumed.
* **Four build bugs fell out of doing this**, the worst being that `.build_flags`
  never contained a single PORT* probe flag -- so switching PORTSEED values reused
  `port_l2.o` and remeasured the old object. See FULL-PORT.md 34 and LESSONS.

The sixteen-root 2026-10-01 baseline is reproduced with
`make ... CPORT_ROOTS_BASE=<the sixteen>` (a `.port_flags` stamp now makes that
override rebuild the unit).

---

## 2. DECLARE THE DATA MODEL. MAKE LAYOUT ERRORS COMPILE ERRORS.

**The root cause of the recurring error class**, and it is countable:

    obj_record.h EXISTS, is generated, is #included in every build -- used ZERO times
    26,679 raw A6 offsets          0 named accessors
    12 of 128 bytes named          1,572 sites touch A6+0x2C alone
    the sprite record at 0xFFF800 has NO declaration
    no type separates a 68K address from an SH-2 pointer

So every site answers *"what is here, how wide, which address space, what byte
order"* **by inference at the point of use.** Tens of thousands of inferences fail a
fraction of the time, reliably. Every one of these was one instance:

| failure | the inference |
|---|---|
| `DAT_fffff142` ushort/byte | width (index scaled 256x, floor_collide read garbage) |
| `DAT_ffffec80` array base | stride (stores landed 4x too far out) |
| `p_A0` dereferenced raw | address space (read SH-2 address 1) |
| A1 in the register file | address space (SH-2 pointer vs 68K integer) |
| `(byte*)&p_D6 + 2`, `CONCAT44` | byte order / layout |
| slot 0's `+0x4E` for slot 16 | base |
| the sprite record `+0..+9` | structure -- rediscovered by diffing bytes |

**The work:**

1. **Full `OBJ` struct** -- all 128 bytes named, **widths generated from the oracle's
   OBSERVED accesses** (already in `docs/proof/l1_vectors.jsonl`: every read and
   write carries its width). Not from Ghidra, which is a whole-symbol inference.
2. **A sprite-record struct** for `0xFFF800 + i*0x10`, fields +0..+9 now known.
3. **`a68` as a distinct type** from any real pointer, so mixing them is a `gcc`
   error. This alone kills the A1 bug, the `p_A0` bug and the absolute-short class.
4. **ONE width decision**, shared. There are three today: `cport_prep.py`,
   `gen_selftest.py`, `dat_width_observed.py`. The array-base rule lived in
   `gen_selftest.py:665` for three days and was re-derived from scratch anyway,
   because it was in a file nobody was reading.

**The test of success: `gcc` starts catching what has been caught by measurement.**
If the next layout error still surfaces as a gate number, the declarations are wrong
and that is known immediately. Secondary measure:
`docs/audit/port_guard_baseline.txt` shrinks to zero.

---

## 1c. THE GATE MUST SUPPLY THE CONTROLLER  <-- DO THIS NEXT

Step 1 is DONE as an architecture: `frame_rotated` (0x0AE2) is rooted, the
decompiler branches for itself, and the 68K's own stack top reads 0x0AE2 -- the
arcade's value. The score is **19 wrong, net -33**, against the four-call body's
9 / -43, and every one of the nineteen is named (FULL-PORT.md 38):

    9 cells  controller and dip input the gate does not supply
    2 cells  FUN_0000E052's D4 parameter (the register-convention class)
    1 cell   a timer decremented four more than the arcade's
    7 cells  also wrong under the four-call body

**THE GATE PENALISES THE MORE FAITHFUL PORT.** The four-call body never reads the
controller, so it never writes those nine cells and never gets them wrong -- it
leaves the seed values, which are already correct. A gate that rewards not executing
code will keep choosing the hand-rolled body, which is why this is the next step and
not the register work.

**The step:** capture the System 16 I/O bytes per frame alongside the work-RAM
deltas (`tools/l1_oracle.lua`, `tools/l2_pack.py`) and have the port's MMIO stub
return the frame's own values instead of a constant. Derivable from the arcade, and
not circular: the controller state is an INPUT to the frame, declared like the seed.

---

## 1b. THE COROUTINE, FOR FREE-RUN ONLY

Step 1 is built and the rooted main loop now COMPLETES a frame (FULL-PORT.md 36),
after three fixes: the 16-bit pointer round-trip (22 sites, `W68`/`A68OF`),
`register0x0000003c` given the 68K's real SP (0xFFFFFF00, from `lea
0xffffff00,%sp`), and that SP re-loaded per frame because 0x62A does and Ghidra
dropped it.

**It completes the WRONG frame.** The game's own progress marker 0xFFFFFEFE -- the
68K address each machine last passed -- reads:

    port 0x0094      arcade 0x0AE2      seed 0x0AE2        <- the FOUR-CALL body: 0x0AE2

0x0AE2 is inside the gameplay frame loop (0x970-0xADC). Entering `test_mode_screen`
at its top runs the STATE-INITIALISATION pass instead, whose
`clear_textram`/`clear_tileram`/`clear_spriteram_and_pool` calls sit after the first
wait and therefore inside the measured span: 868 of the 871 wrong cells in 0xFFE000
are zero in the port and non-zero in both the arcade and the seed. Faithful code,
wrong position.

    A  four-call body      9 wrong   control 52   net   -43
    C  main loop rooted  1163 wrong  control 52   net +1111   (position, not arithmetic)

**The step:** a real coroutine. The arcade is suspended at a wait inside
0x970-0xADC and the port must be put back THERE; `__builtin_longjmp` cannot resume.
A second stack plus a symmetric context switch -- push r8-r14 and pr, swap r15, pop,
rts, ~20 SH-2 instructions -- makes `port_frame_wait` a yield instead of an exit,
and makes BOTH the per-frame and the free-run curves measurable through the real
loop. Prime it once at boot, run forward to the capture's position, then measure.

---

## 3. THE `a68` TYPE EVERYWHERE  <-- MEASURED 2026-10-02 TO BE A PRECONDITION OF 1

**THE ORDER HAS A DEPENDENCY RUNNING THE OTHER WAY.** Step 1 is built (PORTMAIN=1
PORTOS=1) and the rooted main loop HANGS. The hang is `FUN_0000e16a`, which scans
the object table through `psVar10[0x3c]` -- a 16-bit work-RAM pointer -- with no
translation, in a for-loop whose body is empty and which stops only at a negative
sentinel. On the SH-2 that pointer names the 32X BIOS, so the scan never returns.
`w68.h`'s `W68()` has always been the right translation; nothing applied it, and no
type forbade the mix.

    59 sites in 40 functions   pointer re-expanded from a 16-bit integer, (T *)(int)x
    11 sites                   pointer truncated to a short
    29 sites in  3 functions   register0x0000003c, Ghidra's STACK placeholder

Rooting the real main loop reaches code the hand-rolled four-call body never
executed, and that code cannot run without the domain separation. So `a68` is not
the last step; it is what step 1 needs before it can be MEASURED. FULL-PORT.md 35,
and `tools/port_guard.py` check 4 counts the class (baseline 8 -> 51).



Step 2 introduces it; step 3 is applying it across the unit so every 68K address in
data, every absolute-short, every pointer-table target is typed and every mix is a
compile error rather than a wrong number.

---

## 4. THE TARGET THIS SERVES (why the port is the critical path)

Mike, 2026-09-30: *"We can't have a useful play candidate until the SH-2 port of the
6k code is running. full stop otherwise."*

The measured constraint is NOT compute and NOT bandwidth. The game needs 2,780
instructions a vint and the port costs ~2% of one. What costs is that **every
handoff to the MD side is a RENDEZVOUS** -- serialisation even at zero bandwidth --
and two SH-2s do not help because both sit on the same side of it. VDP DMA cannot
read cart; the SH-2 cannot see MD work RAM.

**The cart is the only medium both CPUs read with no handshake**, and it is already
exploited without having been named: 377 KB of baked MD tiles.

So the architecture that wins is: **the SH-2 owns game state, and the picture is
precomputed where the MD reads it independently.** That collapses the handoff from
per-object-per-frame to once per frame. The port is the prerequisite. It is not the
goal and it drives no pixels yet.

---

## 5. RULES THAT ARE NOT NEGOTIABLE (each from a measured failure)

1. **`tools/port_loop.py` is the only adoption path.** Candidate list fixed before
   it runs; every candidate a BUILD FLAG; revert is the default. Its first run gave
   a clean verdict in seven builds with nothing to unpick -- the only change in this
   arc that cost nothing to be wrong.
2. **`tools/port_guard.py` gates the build.** A written rule depends on being
   remembered when code is written; a failing build does not.
3. **Never change the instrument and the subject in the same step.** Broken on
   2026-10-01 by trimming the gate 256 -> 176 while measuring a change.
4. **No number without its control from the same run.** 40 wrong looks small until
   you know that doing nothing scores 40.
5. **Publish the decomposition beside every total and check it sums.** `missed 0`
   beside 1,168 wrong is arithmetically impossible, and that is what caught a
   612-byte out-of-bounds `.bss` write.
6. **Any Ghidra signature change invalidates BOTH `docs/audit/reg_convention.json`
   and `docs/audit/extraout_producers.json`.** Both are generated from the project
   and both are build inputs. A stale one silently keeps the old model -- it cost
   two build cycles.
7. **No new instruments.** 9 result arrays and 6 readers for one gate is the churn
   engine; 18 of 39 commits on 2026-10-01 were corrections.
8. **Before a pattern test over thousands of sites, state what would make a hit a
   FALSE positive and check the pattern excludes it.** Three false-positive engines
   in one session, one of them inside the guard meant to prevent them.
9. **Ghidra: copy the project first and back up `docs/proof/altbeast.c`** (it is
   gitignored). Verify function count AND parameter count after every decompile --
   a custom-storage edit silently drops a function's parameters every time.

## 6. THE REGISTER CHAIN, as built (do not re-derive)

D3 and D6 are **threaded handler to handler** -- each handler's exit value is the
next one's entry value, proven 3 transitions for 3 -- and `object_dispatcher`
computes neither. `_port_reg[15]` is the file (D0-D7 = 0-7, A0-A6 = 8-14, the order
the dispatch thunks index). A function writes a register parameter back **only if its
body assigns it**, or a function that merely receives D3 would clobber a callee's
update. After a call, parameters are **reloaded** from the file, because a by-value C
parameter is stale and the 68000 had no copy. `CPORT_RELOAD` scopes the reload --
the blanket version overflowed the cart.

**DATA REGISTERS ONLY.** A1 was tried and backed out: the port keeps A-registers as
translated SH-2 pointers while the 68000 kept 24-bit addresses that are sometimes
integers, and one global cannot serve both domains.
