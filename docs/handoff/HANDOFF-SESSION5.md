# HANDOFF — session 5 kickoff (written 2026-09-06 00:00, after session 4)

Read docs/handoff/HANDOFF-SESSION4.md for the full record of 2026-09-05 (it is long;
its last third is tonight). This file is the state you start from and
the one job: THE ATTRACT-MODE SPLASH, FLAWLESS.

## 0. Roms on disk (both from commit 08fed24, `make ship`, 23:49)

| rom | what | Mike's verdict |
|---|---|---|
| rom/s16.32x | US, accepted nocat1 line + tonight's belts | 3 cold boots clean; "seems resolved" |
| rom/s16_altbeastj.32x | JP, same line, own tables + MCU mailboxes | plays; boss fight pending his pass |
| rom/s16_nocat1.32x | the base Mike accepted 2026-09-05 18:59 | reference: no belts, no gate |

`make ship` builds both. CAT1MD stays OFF (two grass renderers). The
canonical flag line lives in the Makefile (SHIP_US/SHIP_JP); do not
retype it from a handoff.

## 1. What tonight established (each with the state that proved it)

1. Pixels cannot be judged in MAME for ANY current build: no packet
   lands there under R60 (3897/3900 vints torn, SPRFULL or not). MAME's
   68K side (RAM, regs, staging, mailboxes) still reads true. CLAUDE.md
   says so. Every visual gate is Mike's ares + a state + a capture.
2. `tools/state_health.py` now prints three belt lines. Read them
   FIRST on every state:
   - BELT: torn / echoes / 68K re-marks (echoes ≈ torn, re-marks =
     echoes on a quiet run);
   - DISPLAY GATE: blanks and vints held after display-on (our load
     lag per cut; the arcade's is 1);
   - LOST-PUSH: palette words the 68K shadow says shipped but PAL_SH
     still lacks (any nonzero = a set wearing stale colour).
3. Three "blue gravestone" reports were THREE mechanisms, all of one
   family — state that coasted on warm-reset leftovers until the
   cold-boot zero exposed it:
   a. a palette push lost on the FIFO with nothing echoing it (belt v3:
      2-bit push sequence in packet word 20 bits 13-14, echo on +2/+3,
      two-deep 68K id history);
   b. a tile-art upload dropped when the display gate opened the art
      batch to 40 (reverted; batch stays MD_BATCH=12);
   c. a CRAM paint memo hit over a stale paint (sprite set 0 wearing
      the intro flash; PAL_SETGEN[128] never bumped — ROOT STILL OPEN;
      belt = the verify rotor in apply_cram, one slot per window).
4. Two belt variants were built and REVERTED on measurement; the
   reasons are in the code at the echo site. Do not rebuild them.
5. The DISPLAY GATE honours the arcade's video-enable bit (port
   0xC40001 bit 5). The arcade hides every load behind 4 black frames;
   we now do too, plus a settle hold for our own lag (pages drained +
   art backlog ≤ one batch, cap 60). Held ≈ 21-25 vints/cut on the
   last states.

## 2. THE JOB: attract-mode splash, flawless

"Flawless" = frame-for-frame the arcade's ref_arcade/ref_000001..
002400 (docs/design/ORACLE.md): 4 black frames then the WHOLE title card (frame
20); the SEGA screen; the ALTERED BEAST logo appearing whole (200->235)
then cycling blue/white/red; the demo cut at 461-465. Ours today
(screenshots_0905_2312, build 0140ad1f — re-capture on 08fed24 first):
the card and the logo arrive CELL BY CELL over ~40 vints wherever the
arcade keeps its display on; cuts are black for ~25 vints instead of 4.

Both are ONE number: MD-plane tile art reaches VRAM at 12 tiles/vint.
Why (HANDOFF-SESSION4 "Transitions"): art crosses SH-2 -> FB staging ->
68K VDP DMA, and that DMA must finish before the 68K's window post
(the SH-2 V-gate accepts the post only at V=DF..E2) because the FB is
unreadable at FM=1 afterwards; FB reads DMA at ~85 words/line, so 12
tiles is the budget. A ~500-tile scene = ~40 vints.

THE PLAN (a day; measured premises, none of it built):
- After its post the 68K idles 55-70 lines/vint in the ack-wait.
- Cart->VRAM DMA needs no FM and runs in that idle today (the MDSPR
  blob: 512-word chunks at ~8 lines, ares-proven) = ~64 words/line.
- 44 of 128 colour sets already have a fade-stable static pen class
  (sh_src/tile_classes.h, 11 classes; tools/palpack_tiles.py census).
- So: bake every tile those sets use as MD 4bpp with its class pens
  into the cart (new tool beside palpack_tiles.py; same census); the
  SH-2 emits (slot, cart offset) 2-word records for them instead of
  34-word art; the 68K DMAs cart->VRAM INSIDE the ack-wait, ~150
  tiles/vint. Dynamic-class tiles keep today's path. Pixel-neutral by
  construction (same pens, same art) — a byte-diff of VRAM slots
  between the two paths is the correctness gate, in MAME (VRAM is 68K
  side) or from a state.
- Expected: static-scene cuts under 5 vints; the title card and logo
  whole; scroll-in pop-in gone for static classes; the FB batch left
  to the dynamic minority. It is the same lever the frame-rate work
  needs.
Steps: (1) census which tile codes the attract uses per set (MAME can
do this: tile RAM is 68K-side); (2) bake; (3) SH-2 record + 68K DMA in
the wait, behind a flag; (4) Mike's capture, frame-diffed against
ref_arcade at the four anchors (card 20, logo 235, red 340, demo 465).

What NOT to do: widen the FB art batch (measured: 92-line consume,
dropped records); blank animated loads (not the arcade's frames);
ARTTAIL (Mike: "isn't doing what you think").

## 3. Open, in order after the splash

- Round-clear beam: 60Hz sprite flicker locked by 2-vint cadence
  (HANDOFF-SESSION4); the orb "inpainted" (zoomed pp3 sprite path).
- Speed / stutter in play (Mike's words after the nocat1 pass).
- PAL_SETGEN root cause for sprite set 0 (item 1.3c).
- JP: boss fight and later rounds on Mike's pad; MAME altbeastj is the
  oracle for its 68K side; tools/game_derive.py regenerates its tables.
- Region guard: SH-2 .bss end 0x06018e18 — 0x1E8 under the limit.
  Anything new goes ROM-resident (disp_gate is the pattern).

## 4. Laws added tonight

- A cold-only symptom is a fixed-address block nobody initialises:
  grep the 0x2602/0x0603 defines against m_boot_init before anything
  else.
- Any "black sprite / wrong colour set" report: read LOST-PUSH first;
  then the CRAM pair for that set in the state (cram_mirror vs PAL_SH).
- Before blaming a new build in MAME, put the accepted rom through the
  same rig.
- The two MCUs are not the same chip program: per-game tables are
  derived (game_align/game_derive), never assumed.
