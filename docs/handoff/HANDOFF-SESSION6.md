# HANDOFF — session 6 (2026-09-06, 00:00-04:10, autonomous, Mike away)

Job given: "attract mode screen- and frame-accurate against MAME". This
file is the state, the rig that now measures it, what moved, and the
one hard fact that bounds the rest.

## 0. Roms on disk (commit ad8c27d, `make ship`)

| rom | what |
|---|---|
| rom/s16.32x | US, this session's line (see 2) |
| rom/s16_altbeastj.32x | JP, same line; boots to the Japan warning screen; region guard 0x88 under |
| rom/s16_nocat1.32x | the 2026-09-05 accepted base, untouched (the A/B reference) |

Probe roms (never ship): rom/s16_pcsamp.32x, s16_burn15/30, s16_nopush,
s16_nogate, s16_sprobe — flags PCSAMP, SHIMBURN=N, SHIMNOPUSH (Makefile).

Gate vs nocat1 over 3600 headless frames: cadence 1.033 -> 1.040,
rejects 1.5% -> 2.0%, 68K handler mean 58.5 -> 64.4 lines (+10%, the
second MD-plane consume), consume max 49 -> 48, flip-late 0. Tiles
landed per run 3578 -> 4532. Mike's ares pass NOT yet done on this line.

## 1. THE RIG (no Mike needed)

- **Arcade truth**: MAME altbeast, nvram-free cold boot, no coins, one
  PNG per frame: `tools/arcade_refdump.lua` (RD_LAST=N) and
  `tools/arcade_census.lua` (+ tile/text/work RAM dumps every 20
  frames; `tools/arcade_census.py` decodes visible/resident tile
  demand per frame). The 6000-frame corpus lives in the session
  scratchpad (arc6k/); regenerate in 25 s. NOTE: `ref_arcade/` (Mike's
  corpus) is the CREDIT-IN path ("CREDIT 1"); the attract needs the
  no-coin path — different scene order and text.
- **Ours**: ares-headless (`~/src/ares-debug/build_macos/headless-ui/
  Release/ares-headless --screenshot N:file --dump ...`), frame-exact,
  ~140 fps. Active area of its 1415x243 PNG = crop 1280x224+65+19.
- **Scorecard**: `python3 tools/attract_parity.py rom/s16.32x`
  (`--arc DIR` for the corpus). Aligns on the GAME's timeline (the
  display-enable mailbox 0xFFB001 dropping to 0x80 at the title->demo
  cut, arcade frame 443), then prints mean |luma diff| at lag k after
  each scene's first-whole frame. Floor is ~25-45 (palette + text
  differences); the column where a row collapses to the floor is our
  lag in frames.
- **Counters at a frame**: `python3 tools/ares_diag_at.py rom N ...`
  (builder/consumer census); `tools/ares_gate.py report` on dumps.
- **68K PC sampler**: `make ... PCSAMP=1` puts an H-int at lines
  55/111/167 that only logs the interrupted PC (+V) to a ring at
  0xFFA200; classify against the game map (object loop 0x903984-
  0x9039E7, FM-gate thunks 0xFFBCF4-0xFFBE80).

## 2. What moved (each measured on the rig)

1. **The MD-plane channel ran at HALF RATE on every shipping build.**
   `hs_promote()` (at SHIP) posted a scroll-only stub into the B
   packet slot BEFORE the publish, on 62% of windows (HS_OFFS[10] =
   877 of 1402); the real packet then found the slot unconsumed and
   deferred (DIAG[42]). Stub moved after the publish (`hs_stub()`),
   only into a slot with no staging pending. Effect: 1 real packet
   per 2 vints -> 1 per vint; deferred publishes 740 -> 0.
2. **Two packets per vint** (A and B both built every gap). Costs the
   68K ~3-6 lines (post E8 -> EB); the entry gates read entry-V, not
   post-V, so no reject change.
3. **Blank mode**: while the game blanks (mailbox bit 5) or our gate
   holds, batch 40 per packet and the SH-2 V-gate accepts any post V.
   The 68K keeps MD VDP reg 1 display OFF through OUR hold too
   (packet word 1 bit 13 from the publish): the reverted batch-40 of
   session 4 ran at the active-display DMA rate because the hold left
   the MD display on. FB->VRAM DMA measured ~27 words/line (not 85);
   the VDP queue drains at the next port write, so the "spans done"
   stamp lied by ~8 lines.
4. **Pipeline-armed handshake**: the 68K holds the game's boot until
   the master's V-ISR is live (COMM14: 68K B007, master answers B008
   after seeing it; level-4 enabled during the hold so the shim's vint
   runs). The game's frame 1 now meets a live channel.
5. **Display release** waits for two cell-walk rotations since
   display-on (md_rot) + zero dirty slots + no page pending, and marks
   every FB row (RG_MARK_SPAN) at display-on. The old release fired
   after 2 vints with most of a fresh scene unwalked = the card in
   bands.
6. Pending cells (art not landed) ship the BLANK slot outside cut mode
   too (was: the new slot = foreign art). Keeping the previous entry
   was tried and reverted: page rewrites come with a palette switch,
   the old tiles showed under the new colours.
7. shim_vblank + md_consume moved to WRAM (.data): no measurable
   change — ROM code via the cart window runs at RAM speed here
   (calibrated loop 73 lines both). Harmless; kept.
8. **Stale-paint flush at display-on**: the demo's cat1 sets wore the
   title's 32X CRAM paints for ~14 vints after release (ares "32X
   CRAM" dump: groups 2-5 = the intro-flash family; the grass band
   drew black). disp_gate forgets every cram_keygen slot at display-on
   and on the first settled vint; settle is 4 vints so the repaint
   lands before release. Demo now releases whole WITH its grass band
   at ours 570 = arcade 465 + offset 51 + 54; the boot card 3 frames
   after the arcade's 20 (the game's own boot slips under load, so the
   cut-anchored offset does not fit the boot exactly).

Scorecard now (game timeline): title card WHOLE at the arcade's frame
20 (was 150 frames of bands); logo whole within ~5 frames; white->red
logo rewrite: ~5 frames of blank cells/fragments then whole (676 new
tiles with the display on — physics, see 3); title->demo cut: the
demo appears whole ~54 frames after the arcade's 465 (was ~100); the
release band problem is closed by item 8.

## 3. THE DEMO AT HALF SPEED — corrected (Mike was right, it is not the 68K)

Earlier tonight I wrote "68K-bound". Wrong. The PC sampler with the
object index (PCSAMP=1, ring 0xFFA200: PC, V, 0xFFF109) shows per
two-frame cycle: one object pass (index 0 -> 64) of ~212 lines, then
the main loop WAITING with the pass complete for ~320 lines. The wait
is the game's frame protocol (game_high 0x397E-0x398C): clear
0xFFF01C, spin until IRQ4 sets it; IRQ4 (0x2AAC) sets it and does the
frame work ONLY if it is clear, else counts a miss (0xFFF144) and
skips. So a pass that ends after the vint costs a whole extra vint.

Budget per vint: shim ~60 + game IRQ4 ~35 (entry -> rte 92 median,
unambiguous now via the COMM14 vblank count) + pass ~212 = ~310 vs
262. And part of both the pass and IRQ4 is SPIN in the FM-gate thunks
(0xFFBCF4-0xFFBE80): text (0x3A9A/3AA4/3AAE) and the vint upload
(0x3716) need FM low; FM is up from our post (line 232) to the master's
ack ~60 lines later (22-133).

Measured ladder (scene timer 0xFFF02A per 200 vints, 100 = full):
- ship 8b93866: 50%; shim diet (handler 64 -> 48): 48-50%
- text gates removed (probe): 50%; text gates removed + diet: 62%
- POSTLATE=1 (post+push after the game's IRQ4, flag in tree): 50% —
  the FM window moved into the pass's start, so the text gates spin
  there instead. Pipeline itself fine (cadence 1.034, rejects 1.5%).
- SHIMBURN +73 lines: 48%, +147: 30% (the 2-vint cliff is flat).

THE LEVER: shrink the FM=1 window to what needs the FB. The DREQ
landing needs no FB — raise FM only AFTER the push (45 lines), so the
window is flip + publish + blit (~15 lines, ack census). Then the
game's IRQ4 (from line ~33) waits ~15 lines at most and the pass runs
FM-free. Needs the SH-2 side to flip/capture on the FM rise rather
than on the post (the ISR flips at the post today; at FM=0 the FBCTL
write is ignored). Second lever, additive: text to a WRAM mirror with
dirty-row copy at vint (removes the main-loop text gates). Expected:
pass 170 + IRQ 35 + shim 60 < 262.

## 3b. Afternoon (Mike home, ares desktop capture screenshots/ 15:35)

Mike's three items and what the rig says:
1. "Incomplete tile load everywhere" (his frames 2096-2262, the credit
   attract): reproduced headless with `--input tools/inputs/coin_start_a.csv`
   (START+A held = coin; X does not register). Two of my morning diet
   items made it worse and are REVERTED (f7e978c): one packet per vint
   with the display on (logo rewrite litter 25 -> 100 frames) and the
   rowscroll compare skip (the demo's cloud band rides the per-row
   alt-set bits: 40 frames of stale sky rows after release). What
   remains is the page-rewrite physics of section 4 item 3.
2. "Wrong background palette in gameplay" (frame 476, demo 2 with the
   green zombie): sky and wall shifted to cyan versus the arcade's
   demo-2 frames 1700-2100. Not reproduced headless (ours matches the
   arcade there). BUT the credit path shows a related, deterministic
   fault: from frame ~772 on, the bottom band (grass rows, health
   pips) ALTERNATES EVERY FRAME between the right palette and the
   title's red/white/blue paints (band variance 467 / 6100 on
   alternate frames; the no-coin path is steady at 1564, arcade 1900).
   Named: the MD CRAM ("VDP CRAM" dump) is identical on 776/777/778;
   the 32X CRAM differs in 33 entries over groups 1,2,4,5,13 between
   776 and 777 and is back at 778 — two colour sets contend for the
   same 32X groups every window (the LOOP19 group-thrash family),
   almost certainly the blinking "1 PLAYER START ONLY" text set vs the
   grass/wall cat1 sets. Next: dump grp_key/grp_kind per window on
   those frames and give the blinking set a fixed home. Repro:
   `ares-headless --input tools/inputs/coin_start_a.csv --screenshot
   776:a.png --screenshot 777:b.png rom/s16.32x`. TOP OF THE LIST.
3. "Overdraw tile bleed, right screen side" (frames 32-36, the eye
   pan): a wrong cell in column 40, visible only under fine hscroll,
   for ~2 frames. The EDGE42 pair (cols -1/40) re-ships only when the
   row header moves, a cell is art-pending, or on the 7-window
   backstop; the cell shown is the previous placement's. Small.
Also: the display release settles over 5 vints now (68aca74): the
credit-path demo released with one frame of stale grass paints at 4.

## 3c. WHERE THE FRAME GOES (2026-09-06 evening) — measured, with the
## wrong reading corrected

Ladder on the demo (scene timer 0xFFF02A, f800->f1300; 100% = 60
game-frames/s). Each row keeps the removals above it:

| build | speed |
|---|---|
| ship (rom/s16.32x) | 49.8% |
| NOBLIT=1 (blit ships zero rows) | 59.2% |
| + FM gates stripped | 63.8% |
| + PAL dirty marks stripped | 73.2% |
| + TILE dirty marks stripped | 84.2% |
| + both mark families stripped | 88.0% |

**READ IT AS ONE CURVE, NOT AS ITEM COSTS.** Isolation proves the items
interact: removing the tile marks ALONE on the full ship line measures
49.6% vs 49.8% — nothing. What the mark removal actually does is starve
the SH-2's page copies, and that only shows up once the blit is already
zero. The single quantity that moves the game is THE MASTER'S WINDOW,
because the 68K waits for its ack inside the shim (FM is exclusive).

Master window today = 64.4 lines/frame (diag_add slots, f800-f1300):
blit+ship 37.0, flip+truth+restore 17.3, apply_cram 9.7, rest 0.6.
Regression across the ladder: ~0.42 game-points per master line removed.
So master window 64 -> 0 buys ~27 points; the FLOOR build (every cost of
ours stripped) measures 88.0%, which is the architecture's ceiling. The
last 12% is the game's own pass (~212 lines) + its IRQ4 (~35) at 7.67MHz.

Hypotheses KILLED this session, each in one measurement:
- SH-2 DMAC as a faster FB path: 69 us/row vs the blit's 47-58 (FBPROBE).
- Post/push/FM ordering (FMLATE two variants, POSTLATE): no change.
- FB-resident staging taxing the game's own writes: 68K writes to FB
  staging cost the SAME as work RAM (1.0x; reads 1.1x) — WRITECOST=1.
  So staging placement is not why the game's pass is 212 lines.
- The 68K clock as "the bottleneck": wrong framing, and the ladder shows
  why — half the frame is ours, and it is the master's window.

THE LEVER, in size order (all on our side of the fence):
1. blit 37.0 lines = FB rows composed per frame. Fewer rows: cat1 to the
   MD plane (CAT1MD, off today for the two-grass-renderers look), more
   MD hardware sprite claims (P3/MDSPR claims only on exact palette
   equality — the ONE MD CRAM sprite line is the limiter), and shipping
   CUR|PREV double-ships rows for bank coherency (per-bank row tracking
   would roughly halve it).
2. flip+truth+restore 17.3 lines = the FBTEXT 4KB capture+restore per
   frame.
3. apply_cram 9.7 lines.
Why the window cannot simply overlap the game: FM is exclusive because
the game writes its tile/text staging INTO the framebuffer, and MD FB
writes at FM=1 are discarded (the LOOP24 grave). Moving staging to work
RAM would free the FB but costs the shim a 2KB page copy at ~65 lines
per 1024 words (measured) — worse than what it saves.

## 3d. THE NEXT BUILD (blocked on 0x140 bytes, not on a design question)

`ROWGEN=1` already exists in-tree (Makefile ~1030, 2026-08-26): rows
provably unchanged — no sprite span, no scroll/text/page/cut delta —
skip BOTH their clear and their blit, from write-knowledge, at zero read
cost. It is NOT in SHIP_COMMON, and it aims exactly at the blit's 37.0
lines, the largest single item in the master's window. ROWSTALE measured
96% of rows cycle-stable, so the headroom is real.

It does not fit: `.bss` end goes 0x06018f40 -> 0x06019080, i.e. 0x140
over the 0x06019000 region guard, and the growth is RAMCODE (the RG
bitmaps themselves are fixed-address at 0x39750), from RG_MARK_SPAN
inlined into RAMCODE callers. Moving `restore_pages` out of RAMCODE did
not free it (LTO). So the chore is: pick a genuinely cold RAMCODE
function and make it ROM-resident, the way `disp_gate` was moved for
ARTTAIL, then build `ROWGEN=1` and measure
  (a) demo speed vs 49.8%, (b) the master window vs 64.4 lines,
  (c) a screenshot pair on the credit path — a wrongly-skipped row is a
      stale band, so this needs pixels, not just counters.
Expected from the ladder's ~0.42 points per master line: if ROWGEN
halves the blit (37 -> ~18) that is ~+8 points; with the FBTEXT
capture/restore (17.3) and apply_cram (9.7) after it, ~49.8% -> ~65-70%.
The architecture ceiling is 88% (FLOOR build), so 60Hz in the demo needs
essentially the whole master window gone.

## 3e. THE PIPELINE ANSWER (Mike, after playing the MiSTer core)

Question: the FPGA arcade core is buttery smooth for what is "just a
sprite/pixel game with scaling", and the Genesis-class hardware beats us.
Why?

Because in the demo WE RENDER EVERY SPRITE IN SOFTWARE and the MD's
sprite hardware renders NONE. Measured, ship rom, demo f800-f1300:

    MD hardware sprites claimed .... 0.0 records/frame
    MD SAT entries shipped ......... 0.0 /frame

The arcade and the MiSTer core hand a display list to a sprite chip and
pay nothing per pixel. The Mega Drive VDP is the same CLASS of hardware
(80 sprites, 4bpp, free). We instead rasterize on the SH-2 into the 32X
framebuffer and then pay a second time to blit those rows across the
adapter bus inside an exclusive-FM window that stalls the 68K. That
window is the 64.4 lines of 3c, and it is why the game gets 2 vints.

And the sprites are ELIGIBLE. Decode of SPR_SNAP at demo frame 1300:

    live sprite records .............. 10
    zoomed (MD can never take) ....... 0
    priority ......................... 10 x pp2  (the claim rule's only
                                       accepted value)
    X<=0 (SAT mask trick) ............ 0
    distinct S16 colour sets ......... 3  (112, 114, 115) = 24 colours,
                                       under two MD CRAM lines

Every record passes every hard constraint. They are rejected by two
CONFIGURATION facts, not by the hardware:
1. `mdspr_scenes[]` (sh_src/md_sprart.h) has TWO scenes — "normal"
   (8 keys, anchor set 0x09) and "boss" (14 keys). The attract/level-1
   sprite art was never baked, so the key lookup misses.
2. `mdspr_claim` accepts a set only if `mdspr_pal_equal` finds its 14
   live pens BIT-IDENTICAL to the frame's ONE anchor set. The anchor is
   0x09; the live sets are 112/114/115. Every record fails, every frame.

THE WORK (this is the 60Hz arc, and it is coverage, not architecture):
a. Bake sprite art + keys for the attract/level-1 sets (tools/
   bake_mdspr.py exists; scenes are a table, and the 68K's chunked
   cart->VRAM upload already works on ares).
b. Choose the anchor per scene FROM THE LIVE SETS, and give sprites
   more than one CRAM line — 3 sets fit in 2-3 lines here. MD sprite
   attributes carry a 2-bit palette selector, so per-record line choice
   is free; the squeeze is only that BG tile classes also want lines.
c. Relax "identical 14 pens" to quantised-equal (`mdp_quant` already
   does this for tiles) so near-matches claim instead of falling to the
   FB.
Payoff chain: claimed records never touch compose and leave their sbuf
rows zero, so DIRTY_ROW skips those rows, so the blit (37.0 lines)
shrinks, so the master window shrinks, so the 68K gets its frame back
(~0.42 game-points per master line). This is the same lever as 3c item
1, but the census shows it is nearly free to take in the demo.
What it does NOT fix: zoomed sprites (0 in this demo, but the scale
stepping Mike wants is a separate baked-scale problem) and pp3 records.

## 3f. WHAT MD SPRITE HARDWARE WOULD COST US — the census

Sprite list sampled every 25 frames over the demo (f600-1500, 37
frames, 375 records). Decode: colour set = w4 & 0x3F, zoom = w5 & 0x3FF,
pp = (w4>>6)&3, width = |int8(w2)| * 4 px, span = w0 lo..hi.

    records/frame ............ 10.1 mean
    zoomed (MD cannot take) .. 12.8%
    priority ................. pp2 98%, pp3 2%
    shadow sprites (0x3F) .... 0 in the demo
    distinct colour sets live. 5.4 mean, MAX 7
    MD sprite entries needed . 29.7 mean, 49 max   (MD allows 80)
    worst scanline ........... 19 sprites (limit 20), 424 px (limit 320)

Area is extremely concentrated — four sets carry 91.5% of everything the
SH-2 rasterizes:

    set  0: 31.4% of software sprite area (90 records, 4 zoomed)
    set  2: 21.1%   (cumulative 52.4%)
    set  7: 20.3%   (cumulative 72.7%)
    set  3: 18.8%   (cumulative 91.5%)

Budget if we move the top N sets to MD hardware (zoomed records excluded,
they stay software):

    top 1: -31.4% area, 16 MD entries, worst line  8 spr / 144 px
    top 2: -52.4% area, 22 MD entries, worst line 10 spr / 184 px
    top 3: -72.7% area, 27 MD entries, worst line 12 spr / 260 px
    top 4: -91.5% area, 33 MD entries, worst line 14 spr / 312 px

Geometry and count are NOT the constraint — every option fits inside 80
entries, 20 sprites/line and 320 px/line. THE CONSTRAINT IS PALETTE
LINES. Quantised to MD 3-3-3 each of those sets needs its own line:

    set 0: 13 distinct MD colours    set 2: 14
    set 7: 14                        set 3: 14
    union 0+2 = 26, 2+3 = 22, 0+2+7 = 38, all four = 46

Nothing packs: any two sets exceed the 15 colours of one line. MD has
FOUR lines total, shared with both BG planes (which use 3 today). So
with no BG change, sprites get ONE line = set 0 = 31.4%. Two sprite
lines (52.4%) costs the BG a line and is a fidelity call for Mike.

WHAT WE LOSE by moving a set to MD:
- Colour PRECISION, not colour count: MD is 3 bits/channel vs the FB's
  5. The set still gets all 14 of its pens. The BG planes already run at
  this precision, so it makes the screen consistent rather than mixed.
- Scale: set 0 has 4 zoomed records of 90 (4.4%); those keep the FB path
  per record, which the claim rule already does.
- Priority: pp3 (2% of all records) cannot be expressed; stays FB.
- Resolution: nothing. Same 320x224, same 4bpp cells, same pixels.
- Per-line dropouts: none at the top-1..4 budgets above.

PAYOFF, stated honestly: the direct win is the SLAVE's compose work and
the blit's dirty rows (a claimed record leaves its sbuf rows zero, so
DIRTY_ROW skips them). The master's window breakdown in 3c shows
compose is NOT on the master's critical path (0.2 lines) — so the gain
arrives as fewer blit rows (of 37.0 lines) plus earlier generation
closes, not as a direct subtraction. Build it and measure; the census
says every hard constraint passes and set 0 alone is a third of all
software sprite rasterization.

## 3g. HOW CHARACTERS ARE COMPOSED, AND WHETHER RENDERERS CAN MIX

Per-record decode of consecutive demo frames (f1200-1300):

    rec0 set0  x=205 y=120..184  36x64  addr=1B3F   (addr CONSTANT, x drifts)
    rec1 set0  x=165 y=120..184  36x64  addr=1B3F   = the two standing figures
    rec4 set7  x=395 y=134..184  36x50  addr=8578   body, addr ANIMATES
    rec5 set8  x=403 y=131..149  20x18  addr=7EF0   overlay INSIDE the body span
    rec2 set2  x=294 y=113..184  48x71  addr=9526   larger enemy, animates
    rec3 set9  ... 16x5..16x9 zoom957/693/429/165   a scaling effect, 4 records

So the game ALREADY splits a character into several records, and each
record carries its OWN colour set: body = set 7, its overlay ("the
arms") = set 8, a different palette. Mixing renderers is therefore
natural, because the claim unit is the SET, and three things make it
safe:
1. NO COLOUR SEAM. The hazard would be one set rendered at MD 3-bit on
   some pixels and FB 5-bit on others. That cannot happen if a set goes
   entirely to one renderer. Body and overlay are different sets with
   different palettes, so nothing shared is split.
2. LAYERING IS GLOBAL, SO CLAIM BODIES NOT OVERLAYS. The 32X composites
   its whole FB above or below the MD output (FB pixel 0 transparent);
   there is no per-object interleave. With the FB above MD, an
   FB-rendered overlay draws over an MD-rendered body — the natural
   direction for set 8 over set 7. The inexpressible case is an FB
   object that must sit BEHIND an MD sprite, so: bodies to MD, overlays
   stay FB, never the reverse.
3. ZOOM SPLITS CLEANLY: zoomed records (12.8%; all of set 9) are their
   own records and stay FB by the existing rule.

VRAM IS THE SECOND CONSTRAINT, and for animated sets it binds before
palette does. MD sprite art lives at 0x8000-0xB000 = 12KB = 384 tiles
(BG tiles 0x0000-0x8000, window NT 0xB000, NT A 0xC000, NT B 0xE000,
SAT 0xF000):

    set  0: 31.5% area,  5 art keys x <=40 tiles =  200 tiles ( 6.2 KB)  FITS
    set  2: 21.6% area,  7 keys x <=81 = 567 tiles (17.7 KB)  over
    set  7: 20.8% area, 14 keys x <=64 = 896 tiles (28.0 KB)  over
    set  3: 19.3% area, 15 keys x <=81 = 1215 tiles (38.0 KB) over

CONCLUSION / ORDER OF WORK:
a. SET 0 IS THE CLEAN FIRST CLAIM: 31.5% of all software sprite area,
   5 art keys, 6.2KB of VRAM, ONE palette line (the line sprites
   already own), 8 MD sprites/frame, worst line 8 sprites / 144 px. No
   BG palette change, no streaming, no new mechanism — it is the
   existing MDSPR path with level-1 art baked and the anchor set to the
   live set instead of 0x09.
b. ANIMATED SETS NEED STREAMING, not a bigger bake: one body frame is
   ~32 tiles = 1KB, and the 68K's chunked cart->VRAM upload already
   runs in its ack-wait at ~64 words/line = ~8 lines for 1KB. Uploading
   only the CURRENT animation frame per character per vint fits easily
   and unlocks sets 7/2/3 (another ~62% of area) without needing them
   resident.
c. Palette lines still cap how many sets can be live on MD at once
   (3f): one line today, two if the BG gives one up.

## 3h. BUILT: MD sprite claims turned on (set 0x00) — result, honestly

Change (tools/bake_mdspr.py SCENES): the 'normal' scene baked set 0x09,
which is FOURTH by count in play.csv and is a zoomed effect (not a
character) in the attract. Set 0x00 is the TOP set in BOTH censuses
(play 13760 = 2.6x the next; attract 3823) and is 31.5% of all software
sprite area in the demo. Scene 'normal' now bakes set 0x00 from
play.csv + attract.csv, anchor 0x00: 9 keys, 11840B, inside the 12KB
VRAM window (3 keys dropped by the tool's own overflow rule).

What the set-0 records actually are: the TWO DEMO PLAYER FIGURES, both
36x64, art key 0x1B3F, standing (art address constant while x drifts).

Measured:
    MD hardware claims .... 0.0 -> 0.8 records/frame, SAT 0.0 -> 3.4
    demo speed ............ 49.8% -> 50.8% of 60Hz
    master window ......... 64.4 -> 59.2 lines (blit 37.0 -> 35.7,
                            flip+truth+restore 17.3 -> 13.5)
    gates ................. cadence 1.040, rejects 1.9%, skips 0,
                            flip-late 0; pixels clean (no doubling, no
                            gaps; figures render from MD hardware)

THE LESSON, and it corrects the optimism in 3f/3g: moving sprite AREA to
MD hardware does NOT buy proportional time, because the blit is ROW
granular. The two figures share their row band with other sprites and
cat1 tiles, so those rows still ship. 31.5% of sprite area bought 1.3
lines of blit. Area only converts to time when a whole ROW BAND goes
clean — which needs the majority of sprites AND cat1 off the FB in the
same rows.

Also open: claims measure 0.8/frame where an offline replay of the same
SPR_SNAP dumps against the generated key table says 2.2/frame are
claimable (every gate passes: key+bank+height match, native, pp2,
x>=57, anchor==set). The counters are unconditional at the end of
mdspr_claim, so the gap is CALL RATE — mdspr_claim is running ~0.36x per
frame. Find its enclosing condition (call site ~m_main.c:6413, after the
SPR_SNAP fill) before doing more sprite work; at full rate the same
change is worth ~2-3x what it measured.

## 3i. GAMEPLAY TEST — and what set 0 actually is

Gameplay IS reachable headless: `ares-headless --input
discover/inputs/play_level1.csv` (coin = Y, start = START) reaches
level 1 by frame ~1400 and plays. Use it; no savestate needed.

Measured over 2600 gameplay frames:

    OLD bake (set 0x09) .. claims 0.63 rec/frame, SAT 1.27, speed 48.8%
    NEW bake (set 0x00) .. claims 1.15 rec/frame, SAT 4.50, speed 50.6%

So the new bake wins in gameplay too (+1.8 points, ~2x the claims).

BUT set 0x00 IS NOT THE PLAYER. Decoding the live records in gameplay:
5-7 records per frame, ALL art key 0x1B3F, height 64 — the repeated
WALL STATUES along the level-1 wall (and the pair of statues in the
attract). One art key, drawn 5-7 times: the most VRAM-efficient claim
in the game, which is why it is 31.5% of software sprite area for
1280 bytes of art.

THE PLAYER IS SET 0x09 (play.csv: 49 distinct art keys, 58px tall,
count 5218) — the set the ORIGINAL bake targeted. It claims only 0.63
rec/frame because:
1. ONE sprite palette line exists, so only ONE anchor set claims at a
   time. Set 0 and set 9 cannot both be on hardware today.
2. The player's 49 animation keys are 31.5 KB; the sprite VRAM window
   is 12 KB. Only ~15 poses could ever be resident, so a walking player
   would flip between hardware and software frame to frame — and with
   MD at 3 bits/channel vs the FB's 5, that flip is a VISIBLE colour
   shimmer unless the FB path is forced to quantise set 9 identically.

TO PUT THE PLAYER ON HARDWARE (the next real build, in order):
a. A SECOND sprite CRAM line (take one from the BG's three; the BG pen
   allocator already packs sets, so measure the BG damage first).
b. ART STREAMING for set 9: upload only the CURRENT pose per vint. One
   pose is ~32 tiles = 1 KB; the 68K's chunked cart->VRAM path already
   runs in its ack-wait at ~64 words/line = ~8 lines for 1 KB. This
   removes the 12 KB residency cap entirely and is the same mechanism
   the per-scene blob upload already uses.
c. Quantise the FB path's set-9 pens to MD 3-3-3 so a hardware/software
   flip is invisible (needed even with streaming, for zoomed poses).

## 3j. REVERTED the gravestone claim — the seam Mike caught

Mike's rom/s16.bs1 + screenshots/frame_002269.png: "missing headstones
on gravestones." Diagnosis with the savestate (decoded SPR_SNAP at SDRAM
0x28400) and A/B captures:

- The 7 gravestone-wall blocks ARE set 0x00 / art key 0x1B3F — the set
  3h moved to MD hardware. I mislabeled them "wall statues" in 3f/3g;
  they are the carved-face gravestone row, 7 across, y=120..184.
- They fit MD's limits (14 sprites / 245 px worst line, 28/32 SAT), so
  nothing is dropped and the baked art is complete (all 64 rows, top
  cap present — verified against the sprite ROM).
- The regression is the COLOUR-PRECISION SEAM I flagged in 3f as the
  cost of moving a set to MD: the hardware gravestone paints from MD
  CRAM line 0 at 3 bits/channel and sits directly under the foreground
  ledge (a 5-bit FB tile), so the brown top cap reads wrong where they
  meet. A/B (one stone, 3x): MD version = washed cap; FB version =
  clean. That is a visible fidelity loss for the +1 point 3h measured.

Under accuracy-before-speed this is the wrong trade, so the bake is
REVERTED to the accepted-base target (tools/bake_mdspr.py SCENES back to
set 0x09; md_sprart.{h,bin}, md_sprart_info.h regenerated). Verified:
the reverted gravestone band is pixel-identical to nocat1 again.

WHAT THIS CONFIRMS FOR THE PLAN: a STATIC set that abuts FB geometry is
a bad MD candidate precisely because of the 3-bit seam. The PLAYER (set
0x09) is a better candidate — it moves over the background so a shade
seam is far less legible — but it still needs FB-side 3-3-3 quantise
(3i-c) so a hardware/software pose flip does not shimmer, PLUS a second
CRAM line and streaming (3i-a/b). None of those are free; the sprite
offload only pays once a whole ROW BAND leaves the FB (3h), which the
gravestones could have done but not without the seam. Net: MD sprite
offload is still the 60Hz arc, but it needs the palette+streaming
scaffolding first, not a quick static-set claim.

## 3k. THE WRITE PROBLEM, FULLY MEASURED (Mike: "solve the writes")

The framebuffer blit is the master-window cost that halves the game.
Measured, gameplay (scene timer + a rows-shipped .bss counter in the
blit skip site):

    FB rows blitted/frame ... 37-42 (of 224) = ~31 lines of the 64-line
                              master window
    idle (player standing) .. 34 rows/frame
    walking (scrolling) ..... 50 rows/frame
    of a shipped frame: ~26 sprite-covered rows, ~17 bare (no sprite)

THE BLIT'S ONLY SKIP RULE (ship line) is `was==0x3FF && !ROWLIVE` —
it skips a row ONLY if fully transparent. An opaque row that is
byte-identical to last frame is STILL re-blitted. That is the waste.

TWO LEVERS, and why neither is a flag-flip:

1. ROW_GEN (skip provably-unchanged BARE rows). Ceiling ~17 rows.
   Already in-tree but written for the PRE-R60 slave chain:
   rowgen_build() is called only at slave_concurrent_k rg==0, which the
   NATIVE/R60 scheduler never reaches, so RG_CUR stays 0, every row
   skips, no blit ships, the flip gate declines forever = DEADLOCK
   (measured: 8 vints in 3600). Needs rowgen_build() wired into the R60
   per-frame close BEFORE the slave/master blits, with RG_PEND + sprite
   spans accumulated. Region space for it is now available (move
   cap_drain out of RAMCODE frees >0x80; .bss end 0x18f38). Verify with
   a per-frame pixel diff — a wrongly-skipped changed row is a stale
   band.

2. PER-BANK CONTENT SKIP (hash the sbuf row, skip the FB write if this
   bank already holds that content). Catches the ~26 sprite-covered
   rows too, WHEN STATIC — the idle 34 would drop toward ~10. Needs
   2x224 u32 = 1792B of per-bank hash storage (scrap hunt; .bss has
   only ~0xC0 free) and a hash pass; risk is hash-collision stale
   bands.

THE DEEPER TRUTH (the answer to "why does the Genesis/FPGA win"): a lot
of the write cost is IRREDUCIBLE ON THE FB PATH because the FB holds
SPRITES + CAT1 + TEXT, and those SCROLL with the camera. The arcade and
the MD hardware scroll planes/sprites for free; we re-rasterize the
scrolling content into the framebuffer every frame (walking = +16
rows/frame of pure scroll). Row-skipping cannot skip genuinely-shifted
pixels. The only way scrolling content becomes free is to move it onto
MD HARDWARE:
  - sprites -> MD sprite hardware (player: DONE partial, 2-4 rec/frame;
    static sets abutting FB geometry = the gravestone seam, 3j);
  - foreground cat1 -> the MD plane (CAT1MD, off today = the two-grass-
    renderers look Mike declined).
So the 60Hz arc is: (a) content-skip for the idle/stationary component
(~halves idle), AND (b) push scrolling sprites+cat1 to MD hardware for
the scroll component. (a) is a bounded self-contained build; (b) is the
architecture direction and is where the big frames live.

Nothing shipped this round: ROW_GEN deadlocks unwired, and I would not
ship an unverified blit-skip. Ship rom is the clean accepted sprite
line (a19e0de). cap_drain-to-ROM stayed reverted (only needed for the
ROW_GEN build).

## 3l. Tried and reverted chasing the writes (2026-09-07, don't repeat)

- ROW_GEN flag flip: DEADLOCK (3k). Not wired to R60.
- Per-bank content-skip: blocked on storage — needs 2x224 u32 = 1792B
  and SDRAM has no clean 1792B block (CACHE_C fills 0x29000-0x39000;
  the free gaps are <=0x1000 and scattered). .bss has ~0xC0 free.
  Placing it is a real allocation job, not a quick add.
- Widen player VDP coverage 8 -> 13 poses (play+attract bake, fits the
  12KB window): builds pixel-clean but gameplay speed 48.9 -> 48.7%
  (noise) — MORE claimed sprites do NOT convert to frames because the
  blit is row-granular and the player shares rows. Reverted (also adds
  HW/SW pose-flip shimmer risk, 3i-c). CONFIRMS 3h: sprite offload only
  pays when a whole ROW BAND leaves the FB.

DECISION FOR MIKE (the writes need one of these, both are real builds):
  A. Foreground cat1 -> MD plane (CAT1MD=1, off today = the two-grass-
     renderers look you declined). This takes the scrolling FOREGROUND
     off the FB entirely = the biggest single row-band reduction. The
     cost is the look. Worth an A/B capture to see if the current
     renderer has closed the gap enough to accept it.
  B. Per-bank content-skip for the idle/stationary component (idle 34
     -> ~10 rows). Needs the 1792B allocation solved first, then
     collision-safe hashing + per-frame pixel verification. Helps most
     when the camera is parked (fight zones), nothing while scrolling.
  Neither is a flag flip; both are ~half-day builds. A is bigger and
  gated on your eye for the look; B is safe but bounded.

## 4. Open, in order
0. The credit-path per-frame palette alternation (3b item 2).

1. PAL_SETGEN root (why a set's generation never bumps): item 8 is a
   belt over it, not the fix. ares "32X CRAM" + SDRAM dumps at the
   release frame reproduce it deterministically now.
2. Blank-mode throughput: two FB channels x 40 tiles = ~40 tiles/vint
   (cut hold ~20 vints, ~50 frames behind the arcade's 21-frame cut).
   The 4KB FB hole at bank offset 0x1F000-0x1FFFF (the dead game-
   palette copy; the live palette is WRAM 0xFF9000) fits two more
   1472B channels -> ~80/vint. BLOCKED on SDRAM: each channel is built
   in a 1472B SDRAM staging buffer in the gap (FM=0, the SH-2 cannot
   write the FB then) and the region guard has 0xC0 bytes left. Needs
   the 0x19000 region map moved first.
   Display-on page rewrites (blue field -> logo at 205, white -> red
   at 294) now show BLANK cells and fragments for ~20-25 frames
   (pending cells blank; before: foreign art). Which look Mike wants
   until the whole-scene hold exists is his call.
3. Page rewrites with the display on (white->red logo, face, eye: 160-
   676 new tiles, arcade does it in one frame): either a whole-scene
   hold (old cells AND old CRAM lines kept until the new art lands,
   then switched together) or an FB fallback that draws pending cells
   from cart on the 32X layer. The card+logo (1477) exceed the 1120
   slots, so prefetch is out.
4. Mike's ares pass on this line; then JP.
5. PAL_SETGEN root (session 5 item), round-clear beam — unchanged.

## 5. Laws earned tonight

- A "publish deferred" counter that is a third of the cycles is a
  slot collision, not bank luck: read who else writes that slot.
- The first stamp after a VDP DMA does not include the DMA: the next
  port write pays it.
- The 68000 costs 487 cycles per beam line; a 60-word C compare loop
  is 5-8 lines. Do not estimate 68K work from instruction counts.
- ares-headless answers a "why" in one second. MAME's lua taps and
  its headless debugger do not fire on this build; do not spend time
  there.
- Before blaming the channel for a scene being late, read the game's
  own scene timer against its arcade rate.
