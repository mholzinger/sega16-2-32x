# LOOP 8 — Retire the palette scan (LOOP 7 step 2)

> **STATUS: DONE. Falsifier met (tail 92.4 -> 48.2, palscan 45.1 -> 0.1,
> COMM stream retired with it), and ares confirms on a full level-1
> playthrough — every counter improved or held, no colour fault anywhere.
> See "Iteration 8" in LOOP.md. The one MAME `PRESSURE=1` artifact does
> not reproduce on ares; its mechanism and fix are recorded there.**
>
> Two corrections to this document, both measured:
>  - "44 sites total" was WRONG, and not by an accident of counting: the
>    real writers behind 0x3C20 are the register-indirect loops at 0x2DC8
>    and 0x3C5A, which an address-formation scan structurally cannot see.
>    45 sites are thunked. Use `tools/pal_tap.lua` before trusting any
>    future site enumeration taken from patch_report.
>  - "Only 17 are loop bases needing ALL-dirty (0xFFFF)" was too
>    pessimistic. Every loop bound is derivable from the disassembly, so
>    NO site ships an ALL-dirty mask; the two that genuinely cannot be
>    known statically compute their region at runtime instead.

Kickoff doc for a FRESH session. Self-contained: read this + LOOP.md
(iterations 7a-7k and the negatives list), then run.

## Where LOOP 7 got to

The reject band that sat at 57-66% for six iterations is GONE.

    metric          LOOP 7 start   now (light)   now (gameplay)
    vints/cycle         6.99          3.04           3.24
    V-gate rejects     57.1%          1.4%           7.5%
    MD tail (mean)     170.6          92.4            —
    parity mean        43.98          22.09           —   (title 2.43%)

Landed: COMM payloads onto the DREQ packet (7a), packet ordered
critical-first with partial apply (7b), blit in thirds (7d), packet
SPLIT by window phase (7g), missq out of .bss (7h), FS latch wait
bounded (7j).

## Mission

Delete the palette dirty-scan. It is 45 of the 92 remaining tail lines,
it runs on EVERY vint, and in steady state it finds NOTHING — 1024
unavoidable MD-RAM reads (512 mirror at 0xFF9000 + 512 sent-copy at
0xFFA000) to discover that nothing changed. LOOP 6 negatives 3, 4 and 5
proved it cannot be micro-optimised (not division-bound; long compares
don't help on a 16-bit bus; ablation 45.1 -> 0.1 lines confirms the loop
IS the whole cost). It has to STOP EXISTING.

## The mechanism is already proven and the sites are already enumerated

Same as the LOOP 3c tile dirty-bit thunks: patch each address-formation
site to a `jsr` into an MD-RAM thunk that ORs dirty bits into a word,
then runs the displaced instruction and returns. See
`TILE_DIRTY_SITES` and the thunk emitter in `tools/patch_game.py`
(~line 673) — that is the working model to copy.

The palette is already remapped to the 0xFF9000 mirror by the same
patcher scan, so the sites fall out of `tools/patch_report.txt` (grep
`-> 00FF9`). MEASURED SHAPE — this is the good news:

    44 sites total
      24  33fc  move.w #imm,abs.l   <- single word, PRECISE bit
       3  4279  clr.w abs.l         <- single word, PRECISE bit
      13  43f9  lea ...,%a1         <- loop base, extent unknown
       3  41f9  lea ...,%a0         <- loop base
       1  207c  moveal #...,%a0     <- loop base
    21 distinct targets, spanning only 5 of the 16 128-word regions
    (0, 2, 4, 7, 8)

So 27 of 44 sites write exactly ONE word and can set exactly one bit.
Only 17 are loop bases needing ALL-dirty (0xFFFF), the same treatment
the tile sites give table-driven extents.

## The design

1. A 16-bit palette dirty word (one bit per 128-word region, 2048
   words total). Thunks OR into it; the shim clears bits as it ships.
2. The scan DIES. When the dirty word is zero the shim does nothing at
   all — that is the 45 lines, gone, on the overwhelming majority of
   vints.
3. Delivery: ship dirty regions on the DREQ TEXT packet (base word + N
   words), applied by the master into PAL_U with PAL_SETGEN bumped per
   colour set exactly as `slave_service_stream` does today. DREQ costs
   0.063 lines/word against COMM's 1.27 — see THE RATIO in LOOP.md.
   Once that lands, COMM has NO tenants left and the stream can go.
4. The 0xFFA000 sent-copy (4KB) is then dead — free MD RAM.

## The one real design question: WHERE DO THE THUNKS LIVE

44 thunks x 16 bytes = 704 bytes of MD RAM, and the address is
CONSTRAINED: thunks are reached by `jsr (xxx).w`, and 68K abs.w
SIGN-EXTENDS, so the low word must be >= 0x8000 or it targets low ROM
and crashes at the first thunked site (the tile thunks hit this).

Occupied: 0xFF7000 sprite mirror (2KB), 0xFF8000 text (4KB), 0xFF9000
palette (4KB), 0xFFA000 sent-copy (4KB, dead once this lands), 0xFFB000+
shim mailboxes, 0xFFB820 tile thunks (456 bytes), 0xFFB9FE tile bitmap.
0xFF7800-0xFF7FFF is free but its low word is < 0x8000 — UNUSABLE.
Candidates: after the tile thunks; or 0xFFA000 once the sent-copy dies
(but it is still alive while the scan is being replaced, so sequence
matters — thunks first, scan second, sent-copy last).
BEWARE 0xFFB400: the game's own boot code runs from a RAM copy there.

## Gates (every commit)

1. Boots + full attract, NO hang.
2. `tools/parity_run.sh` on a CLEAN probe-free build; no scene regresses
   beyond the known anchor bimodality (negative 10).
3. Region guard passes (`grep ' _end$' rom/s16.lst` — .ramtext counts
   toward it and headroom is ~720 bytes), rom stamped `normal`,
   `make PRESSURE=1` holds.
4. PLAYABILITY PRESERVED.

## Falsifier

`make TAILPROBE=1` + `tools/win_probe.lua`: **MEANpalscan 45.1 -> ~0**
and MEANtotal 92 -> ~47. That is MAME-visible, so this arc does NOT
need an ares round-trip to iterate — unlike everything strobe-related.

## HARD-WON LESSONS (from LOOP 7 — do not re-learn these)

- **A space hack is a change.** MISSQ_CAP was trimmed 192 -> 128 purely
  to fit code under the region guard; ares answered with band deferrals
  48 -> 551. If the guard forces a shave, the shave gets its own gate
  line — or move data out of .bss instead (missq now lives at
  0x0603A000).
- **Single-session ares numbers vary ~14x by content** (negative 16).
  Same build read rejects 7.5% in gameplay and 1.4% in a light scene.
  Only large moves are evidence; match content or compare nothing.
- **Feed strobe_scan.py RAW captures.** Dedup inflates the black rate by
  the dedup ratio and skews the gap histogram.
- **The MAME scoreboard is anchor-bimodal** on demo2 especially (13.0 /
  15.5 / 18.7 / 21.8 / 24.9 across builds). The MD tail split is the
  stable MAME metric — use it.
- **Verify what a number counts before theorising on a mismatch.** A
  3.3x "second cause" turned out to be dedup, and cost a pass.
- **Probes move the scoreboard.** Gate on clean probe-free builds.

## Not in this arc (open, ranked)

1. THE STROBE. ares defers an out-of-vblank FBCTL write to the next
   vblank, showing the never-composed bank for a whole frame. It is a
   LOAD CEILING: 0.5% of blit windows in light scenes, 6.9% under
   gameplay, worst span 143 lines against a 38-line budget. Bounding
   the latch wait (7j) removed ~19 lines/window of stalled 68K but did
   NOT touch the black frame — it stops us PAYING for the deferral, not
   the deferral itself. Fixes ruled out with reasons in LOOP.md: skip-
   on-predict (would skip 100%), atomic ship (copy_pages still reads FB
   staging), shadow bank (SH-2 may only write the FB with FM=1). What
   is left is making the blit FIT: DMAC channel 1 is free, though note
   blit_half's comment that the cached-alias write-buffer path is what
   shipped 32X ports use — measure before believing DMA wins.
2. dreq_incomplete ~9-11% of cycles with push_aborts=0 (down from 47%).
3. blit skips 24-29% of cycles; each is a stale third.
