# CARD: ten C-level traps in the decompiled corpus (2026-09-27, decompile -> builder)

Found in one afternoon taking the SH-2 self-test from 3 oracle vectors to 86 of 101 and
the 771-function port from 420 compile errors to 0. Every one produced a CONFIDENT WRONG
ANSWER first. Relevant to the test cart because items 1, 8, 9 and 10 are about instrument
discipline, not about Altered Beast.

## 1. Keep the compiler clean. Suppression hides the bugs you are hunting.

`-Wno-error=int-conversion` and friends were hiding two real defects: a sign fault that
relocated every ROM table read into work RAM, and a type loss that corrupted 10,700
references. **Derive the casts from gcc's own diagnostics instead of writing or
suppressing them** (`tools/cport_cast.py`): run the compiler, read the type it says it
wanted, insert that, repeat until clean. A loop that ends clean cannot have guessed.

**Insert only LOSSLESS casts** -- pointer <-> 32-bit int, pointer <-> pointer. A cast that
loses bits (a pointer into a `char`) is a WRONG DECLARATION wearing a cast; fix the
declaration. Three were found that way.

## 2. Dropping a shadowing pseudo-register declaration corrupts address AND value.

`FUN_000024ec` has `undefined2 *unaff_A6;`, so `unaff_A6[6] = 0x14a0` is the WORD at byte
offset **12**. Bound to a global `byte *unaff_A6`, the identical text means the BYTE at
offset **6** and truncates to `0xA0`. **Ghidra declares the element type PER FUNCTION.**
INITIALISE the declaration instead of deleting it:

    undefined2 *unaff_A6 = (undefined2 *)_r_unaff_A6;

1,463 declarations, and 78 of them were visibly truncating values (`-Woverflow` -> 0).
The address scaling was silent everywhere else.

## 3. An A-register with a `w`/`b` SUFFIX is a VALUE, not an address.

`unaff_A6w` is the word half of A6. Typing it `byte *` and casting to `undefined2`
truncates a pointer to 16 bits.

## 4. One address, three widths, three different authorities.

* **Overlapping symbols**: `_DAT_x` is the LARGER, `DAT_x` the smaller. Ghidra says so in
  its own warning ("Globals starting with '_' overlap smaller symbols at the same
  address"). The width table is keyed by ADDRESS and cannot express both.
* **A numeric suffix** (`DAT_fffff032_1`) is a THIRD symbol at that address.
* **An array base** -- `(&DAT_ffffec80)[i]` -- indexes in its ELEMENT size, which is the
  oracle's observed access width, not the whole-symbol inference.

## 5. 68K absolute-short addressing appears as a NEGATIVE CONSTANT.

`move.b #$FF,($ECC0).w` sign-extends to `0xFFFFECC0` = work RAM `0xFFECC0`, and Ghidra
writes `*(undefined *)(iVar1 + -0x1340)` because `0x10000 - 0x1340 = 0xECC0`. Unrewritten
it dereferences **the SH-2's on-chip register area**.

**The candidate count went 377 -> 143 -> 127 as three classes of false positive were
excluded.** Absolute-short forms its address from an INTEGER CONSTANT, never from:

* a pointer -- Ghidra names them `p<type>Var<N>`; `(puVar8 + -0x1340)` is ordinary
  pointer arithmetic
* an A-register -- `(unaff_A6 + -0x78)` is the 68K's `(-$78,A6)`, a displacement
* `register0x...` -- an UNRESOLVED register, so a small negative displacement off it is a
  STACK FRAME local. All 16 of these were being relocated to work RAM near `0xFFFFF4`.

## 6. The SIGN of a 16-bit absolute address is the REGION SELECTOR.

    0xC880 -> 0xFFFFC880 -> 68K 0xFFC880   work RAM
    0x0100 -> 0x00000100 -> 68K 0x000100   PROGRAM ROM

An unconditional `0xFF0000 | v` relocates every POSITIVE short from ROM into work RAM.
And note the corollary: **if a value is translated on READ it must be translated on
WRITE**, or the round-trip does not close.

## 7. A 68K addressing mode is not a semantic constraint.

We are writing C for the SH-2, not emulating a 68000. A 16-bit stored pointer exists
because the 68K HAD that addressing mode; the port needs the same CELL and may reach it
however it likes. **Rewrite an addressing mode; never touch the game's logic.**

## 8. `abort()` under `-flto` means a NULL pointer, and never stub it.

gcc emits `abort()` for a path it proves unreachable; `-fno-lto` produces no reference at
all, so it is whole-program LTO finding it. The cause was a zero-initialised
`static byte *unaff_A6` that LTO saw dereferenced while still NULL.

**Do not stub `abort()` to record-and-return.** It is undefined behaviour -- gcc treats
everything after the call as dead code. With a stub, a build ran 42 of 54 in ares and
produced **NO BARCODE AT ALL** on the FPGA, and silently dropped its summary counters so
the barcode reported zero passes on a working test. The link error is the guard.

## 9. Instrument discipline, three ways, all measured.

* **A counter written DURING a sweep is not readable.** The same rom reported 27 in an
  ares dump and 36 off the barcode -- two sampling points, and it looked like an
  ares-vs-FPGA disagreement for an hour. Publish a value that cannot change under the
  reader.
* **A FAIL must report the cell that FAILED**, not the first cell compared. Ours showed
  `got == want` on failing cases and said nothing about the cause.
* **Never redirect a generator's stderr while iterating on it.** A C-style `/* */` comment
  inside the Python generator made it a syntax error; three build cycles were spent
  reading a stale generated file and blaming the compiler.

## 10. Two toolchain signals that are not what they look like.

* `ld: error adding symbols: no error` = a STALE object. Delete the `.o` and rebuild.
* Barcode byte 5 has SIXTEEN meanings depending on build flags and the wire format
  carries no mode tag. `tools/rig_barcode.py` now prints `b5`/`b6` unnamed unless given
  `--mode`. An early `sh_pub 3` was read as "3 cases passed" when the counter was 0.

## Also, from Sega's own bulletins (`~/src/32XDK`, reviewed today)

* **#27, a CMD INT can be acknowledged TWICE (32X only).** Needs the FRT's OUTPUT COMPARE
  to drive the external IRQ level. We define `TOCR`/`OCRH`/`OCRL`/`TIER`/`FTCSR` and never
  write them -- only `FRCH`/`FRCL` as a counter and `TCR`. **If the test cart adds FRT
  output compare, this becomes live**, and the corrected handler is in the bulletin.
* **#14**: the VDP shift bit is invalid when a line-table base's low byte is `$FF`.
* **#9**: 68000 interrupts are prohibited while RV = 1. Mike's call is that FPGA evidence
  is sufficient and the rule does not bind us (we install our own vectors rather than
  using the BIOS handoff) -- see `docs/design/SILICON.md` §6a for the one-line test if
  silicon ever shows an interrupt-shaped failure.
* **The SH7604 cache is 4-way, 16-byte lines, 64 entries, entry = `(addr >> 4) & 0x3F`.**
  For records at stride S a fixed field lands in entry `i*(S/16) mod 64`, which covers all
  64 entries only if `S/16` is ODD -- 128/16 = 8 thrashes 8 records into 4 ways, 144/16 = 9
  gives every record its own set. That derives the measured re-stride exactly.
