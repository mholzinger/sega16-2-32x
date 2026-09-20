# HANDOFF — slim pipeline, rig-verified mechanisms, and the test harness

Session ending 2026-09-19. Read with `STATE.md` and `LESSONS.md`.

## ADDENDUM 2026-09-20 (read this first)

THE LINE IS UNCHANGED IN BEHAVIOUR AND CHANGED IN BYTES. `make line` now
carries the boot-stack move (md_start.s 0xFFBFF0 -> 0xFF3FF0, md.ld
guard) and two hook call sites that assemble only under PARTB_HOOK
(TILESLIM / CARTREADAT builds). rom/night/lineV.32x == `make line`:
ares 1200-frame play script gives scene timer 718, 0 pixels and 0 VRAM
bytes different from lineT; the stack-fix build rendered on the rig at
99.7%. The `25f1 3fff` invariant no longer holds against lineT (the 68K
image moved); take lineV as the new reference once Mike has played it.

WHAT WAS WRONG (LESSONS, four new entries):
  1. the boot stack sat 184 bytes above the FM-gate thunk table and the
     boot-time vint overwrote the table's tail whenever the vint path
     grew by 32 bytes -- every slim build and DELIVTEST 2/3;
  2. under TILE_SLIM the SH-2's converter fallthrough (unbaked sets)
     emitted pixel words as records; the 68K's computed cart address
     then left the bank window and locked the 68K on silicon (the VDP/
     PSG mirrors), invisibly in ares and MAME;
  3. rig captures at wall-clock offsets compare attract phases, not
     health -- eight "collapses" were healthy title screens;
  4. three probe results were no-ops (define missing / hook on a path
     the line does not run). Verify `.build_flags` and a non-zero
     counter before a rig result counts.

RETRACT from the body below: "renders correctly in ares, FAILS on
hardware" (it failed in both; slim4's 336k-pixel ares diff WAS the red
screen); "volume ruled out at SLIMCAP=8" (measured on the broken base);
"methods 2 and 3 reach no verdict" (they crashed on defect 1; on the
fixed base 2 = GREEN and 3 = RED, the identity map returning other
bytes in ares as LESSONS predicts).

STATE OF SLIM: docs/design/SLIM-PIPELINE.md 1b. Runs on ares and the
FPGA. 73% of shipped tiles are unbaked and go inline.

NEXT:
  1. Mike's play pass on rom/night/slim18.32x (TILESLIM=1 SLIMCAP=40)
     against lineV; scene-anchored captures, not wall-clock ones.
  2. The bake coverage: why 2278 of 3120 tiles fall through to the
     converter on level 1. Until that is fixed the slim pipeline
     removes the SH-2 from 27% of the payload.
  3. SLIMCAP ladder on the rig with the scene timer as the anchor.
  4. Remove the slim diag counters at 0xFF3400 once the play pass is in.

HARNESS: `make line CARTREADAT=18|17|19|20|21 [CARTREADADDR=..]
[CARTREADDELAY=..]` = a 64-read cart burst or a pure delay at a named
vint position; `SLIMNODMA=1` / `SLIMNOFETCH=1` drop one slim step. All
verified against `.build_flags` before use.

## THE LINE (updated 2026-09-20: lineV passed Mike's play pass)

    rom/night/lineV.32x   THE LINE ("playable", Mike 2026-09-20)
    rom/night/lineT.32x   == rom/night/tilesmd4.32x   previous line
    rom/s16.32x           line-equivalent (differs from lineV at 3fff only)
    rig                   lineV

INVARIANT CHECK CHANGED when TILESMD joined the line. It is no longer
`25f0 3fff`:

    cmp -l rom/s16.32x rom/night/lineT.32x \
      | awk '{printf "%x\n",$1-1}' | cut -c1-4 | sort -u
    -> 25f1 3fff     (4 B BUILD_HASH32 at 0x25F13C, 14 B string at
                      0x3FFFD4; 18 bytes total)

`.tilesmd` moved 0x263C00 -> 0x264000 (the `.ramtext` LMA reached
0x263CB3 under TILESLIM and overlapped it). The line was re-verified
pixel- and VRAM-identical after the move. The address now lives in TWO
files and `make tilesmd-addr` fails if they drift.

## PROVEN ON THE RIG, each with a control in the same frame

| mechanism | result |
|---|---|
| VDP DMA sourced from CART | **BLOCKED.** Transfer runs, cart data never arrives |
| 68K cart read, bank window `0x900000`+`0xA15104` | **WORKS** — 25-27% purple every frame of six |
| 68K cart read, identity map `0x200000` at RV=1 | **WORKS** — 9%, one frame of four |
| bank switch alone (`BANKPOKE=1`) | **SAFE** — plays identically to the line |
| VRAM port writes alone (`PORTPOKE=1`) | **SAFE** — renders at 99.2/99.4% non-black |

**ares is BACKWARDS on cart addressing** (it reports identity 0/32,
window 32/32). Any question about how the 68K reaches cart is rig-only.

## RETRACTED THIS SESSION — do not quote these

1. **The MDBATCH throughput sweep (8.82 -> 18.61 tiles/vint) and "the
   packet is the throughput wall".** Read from `0xFFA246`/`0xFFA248`,
   which sit in the block `md_main.c:848` itself calls CLOBBERED, and
   the 16-bit one wraps. A clean counter at `0xFFB200`, validated
   against a second at `0xFFB300` and a sentinel at `0xFFB308`, reports
   3202 tiles / ~398 batches / PEAK 40 **identical to the byte** across
   FB vs SLIM, MDBATCH 24/48/320 and blank-batch 40/300. Three knobs, no
   movement. No throughput claim may be quoted until an instrument
   RESPONDS to a knob.

2. **"68K cart reads are proven on hardware by `mdspr_upload`."**
   Instrumented: `mdspr_upload_pump` executes **ZERO times in 3000
   frames**. Dead code. It proved nothing, and the slim pipeline was
   founded on it. Sprite art arrives by some other path.

## SLIM PIPELINE — state

`TILESLIM=1`. SH-2 emits a 2-word record `(slot|fg<<15, blk*64+code&63)`
and the 68K fetches art from cart itself. Two payload copies instead of
three; the SH-2 leaves the tile payload path.

**Builds, renders correctly in ares, FAILS on hardware.** slim1/2/3
black, slim4 solid red.

Three real bugs found and fixed, none of which fixed the screen:

- **stride** — the second emit site advanced `o` by 17 words per 2-word
  record, corrupting the packet from record one. Now `MD_REC_W`.
- **hscroll** — `} else if (0) {` spliced out the FB branch tail, which
  carried the hscroll table write. Under slim the planes never got a
  scroll value. Now outside both branches.
- **interleave** — cart reads sat between the VDP address write and the
  data writes; hoisted into locals first. This turned BLACK into RED, so
  it was real and was not the whole story.

Every technique is individually proven on the rig (table above). Only
the assembly fails.

**Do not debug the finished rewrite backwards again.** Grow the slim
path FROM the working FB route one mechanism at a time, rig-checked at
each step.

## THE HARNESSES BUILT THIS SESSION

    docs/design/DELIVERY-MECHANISMS.md
        Frame delivery as 15 atomic mechanisms with a test for each, and
        where this session's bugs sat on that map. #15 (side-channels
        RIDING the packet -- hscroll, VSRAM) is the one that matters:
        it is not a delivery mechanism, and no tile test can catch it.

    make ... DELIVTEST=n
        Delivers a known 16-word sample to free VRAM 0xF800, reads it
        back, paints a STICKY verdict into CRAM 0-31 (rig) and WRAM
        0xFFB350 (local dump).
          0 DMA from WRAM   CONTROL, MUST PASS   -> GREEN
          1 port, immediate                      -> GREEN
          2 port, cart bank window               -> NO VERDICT (see below)
          3 port, cart identity map              -> NO VERDICT
          4 DMA from cart   KNOWN BAD, MUST FAIL -> BLUE
        Methods 2 and 3 write nothing at all, so execution does not
        reach the paint. That is the next thing to isolate.

    tools/frametest.py
        Scene-anchored frame-delivery tests. Metrics are the defects we
        actually have: blob_in_cart (FIRST -- an inert feature is
        byte-identical to the baseline and passes everything else),
        tiles_per_vint, black_cells, hot_glyph_cells, scene_colours.
        Each rom captured at ITS OWN emulator frame for a given 68K
        scene-timer value. Slow: it bisects per checkpoint.

    tools/check_tilesmd_addr.py / make tilesmd-addr
        The blob address in mars.ld vs md_main.c. Verified to fail on
        injected drift.

    Build guard in the $(TARGET).32x rule
        Fails on any section with CONTENTS but no ALLOC. Verified both
        ways. This is what `.tilesmd` needed and did not have.

## RIG FACTS THAT CONSTRAIN EVERY PROBE

- **There is no fast frame capture.** `/dev/MiSTer_cmd` tight loop
  coalesces (30 requests in 39ms -> ONE file); paced it is ~1 frame per
  6.8 seconds; `/dev/fb0` reads at ~36fps but is the OSD layer, 99.8%
  black, NOT core video. `/media/fat/burst.sh` carries these numbers.
- **Therefore a rig probe must not FLASH.** Latch the result and hold a
  stable colour. Five cuts of CARTDMAPROBE were unreadable because they
  showed a per-frame value.
- **A negative needs a positive control IN THE SAME FRAME.** Five probe
  cuts died in the READOUT, never in the thing measured.
- **Design the probe so the right answer looks different from every
  wrong answer, including every way the probe itself can fail.**

## OPEN DEFECTS ON THE LINE

- **Zeus message — ONE defect, not two.** The "leftover glyphs" ARE
  fragments of the message: R, M, R, M, U at identical positions in the
  Zeus screen AND mid-gameplay; earlier recorded glyphs were O, Y, N.
  Every letter is in RISE FROM YOUR GRAVE. The message delivers a few
  characters and never clears them; the full line never displays. Two
  symptoms, one writer. Most visible defect on the line.
- **Wolf transformation** — one frame of the character, then flames and
  chevron only. **PRE-EXISTING, confirmed missing on `bldS`.** Sprite
  layer. Not caused by any tile or pen work.
- **Black tile pop-in** — measured 0-4 cells of 1120 per capture on
  mdb48 (0.0-0.4%), down from a 4.8% baseline but NOT gone. A still
  frame undercounts it; it is temporal.
- **Unexplained** — a large solid YELLOW rectangle in the Zeus frame
  (about cells col 10-13, row 15-21). Not text; the lightning draws
  separately.
- rounds 1/2/4 lose 12 pinned sets to overflow vs bldS. Round 0 keeps
  all 30.

## NEXT

1. Extend `DELIVTEST` DOWN to mechanisms #4/#5/#7/#8 — record format and
   packet walk. Pure data checks, no VDP, so they cannot black-screen
   anything. Get the record round-tripping provably before rebuilding
   slim on top of it.
2. Isolate why DELIVTEST methods 2 and 3 reach no verdict.
3. Zeus message — needs no rig time to investigate and is the most
   visible defect.
4. `.ramtext` budget / H7: 18 per-vint SH-2 functions fetch from the
   cart window at a 4-6x stall. Two of the six cheapest are in ROM
   DELIBERATELY (`slave_window_k`, `c1mask_find`) to free `.ramtext`, so
   it is a budget question, not a sweep.

## COMMANDS

    make line                        THE LINE (LINE_FLAGS in the Makefile)
    make line TILESLIM=1 SLIMCAP=40  the slim candidate
    make line DELIVTEST=n            delivery harness, n = 0..4
    make line BANKPOKE=1             line + only the cart bank switch
    make line PORTPOKE=1             line + only VRAM port writes
    make tilesmd-addr                blob address drift check
    make lint                        H7 is deliberately RED until fixed
    tools/mister_push.sh <rom>       deploy + launch
    ssh root@mister.office.local 'echo "screenshot" > /dev/MiSTer_cmd'
    scp root@mister.office.local:/media/fat/screenshots/S32X/<NAME>.png screenshots/
    tools/frametest.py <rom> --baseline rom/night/lineT.32x
    tools/at_gameframe.py <rom> <scene-timer> --input discover/inputs/play_level1.csv

`make -n` REWRITES `.build_flags`; save and restore it.
