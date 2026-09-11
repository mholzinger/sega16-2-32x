# Altered Beast System 16B -> 32X. Build layout from 32x-builder (marsdev).
MARSDEV ?= ${HOME}/src/marsdev/mars
MDBIN    = $(MARSDEV)/m68k-elf/bin
SHBIN    = $(MARSDEV)/sh-elf/bin

ROMDIR  := rom
# GAME selects the arcade program (2026-09-05, the kit's second title):
#   make                    -> roms/altbeast   -> rom/s16.32x
#   make GAME=altbeastj     -> roms/altbeastj  -> rom/s16_altbeastj.32x
# The graphics ROMs of the Japanese set are the US bytes in a different
# split (verified byte-equal); the program and MCU differ. -DGAME_<NAME>
# reaches both CPUs and .build_flags, so switching GAME rebuilds.
GAME     ?= altbeast
GAMEROMS := roms/$(GAME)
GAMEDEF  := $(shell echo $(GAME) | tr a-z A-Z)
ifeq ($(GAME),altbeast)
TARGET  ?= $(ROMDIR)/s16
else
TARGET  ?= $(ROMDIR)/s16_$(GAME)
endif
MDTARGET = $(ROMDIR)/md_start

MDCC   = $(MDBIN)/m68k-elf-gcc
MDNM   = $(MDBIN)/m68k-elf-nm
MDOBJC = $(MDBIN)/m68k-elf-objcopy
SHCC   = $(SHBIN)/sh-elf-gcc
SHAS   = $(SHBIN)/sh-elf-as
SHNM   = $(SHBIN)/sh-elf-nm
SHOBJC = $(SHBIN)/sh-elf-objcopy
SHOBJD = $(SHBIN)/sh-elf-objdump

MDCC_VER := $(shell $(MDCC) -dumpversion)
SHCC_VER := $(shell $(SHCC) -dumpversion)

MDPLUGIN = $(MARSDEV)/m68k-elf/libexec/gcc/m68k-elf/$(MDCC_VER)/liblto_plugin.so
SHPLUGIN = $(MARSDEV)/sh-elf/libexec/gcc/sh-elf/$(SHCC_VER)/liblto_plugin.so

MDINCS   = -I$(MARSDEV)/m68k-elf/lib/gcc/m68k-elf/$(MDCC_VER)/include
SHINCS   = -I$(MARSDEV)/sh-elf/lib/gcc/sh-elf/$(SHCC_VER)/include
MDLIBS   = -L$(MARSDEV)/m68k-elf/lib/gcc/m68k-elf/$(MDCC_VER) -lgcc
SHLIBS   = -L$(MARSDEV)/sh-elf/lib/gcc/sh-elf/$(SHCC_VER) -lgcc

# -Werror=return-type is not pedantry: a missing `return 0` at the end of
# bake_find (LOOP 17) handed the caller whatever was in r0, which it used
# as a frame pointer and DREW -- a band of foreign art down the right of
# the ROUND CLEAR screen on ares. gcc warned every build; the warning
# went past in the noise. This class is silent corruption, so it fails
# the build now.
MDCCFLAGS  = -m68000 -mshort -Wall -Wextra -Werror=return-type -std=c99 -ffreestanding -DGAME_$(GAMEDEF)
SHCCFLAGS  = -m2 -mb -Wall -Wextra -Werror=return-type -std=c99 -ffreestanding -DGAME_$(GAMEDEF)
# `make PRESSURE=1` = ares-proxy budgets (cut quiet zone) for pre-handoff
# validation; NEVER ship a PRESSURE build to ares/hardware.
ifdef PRESSURE
SHCCFLAGS += -DPRESSURE_TEST
endif
# `make SPROBE=1` = sprites-off cart-bus contention probe; never ship.
ifdef SPROBE
SHCCFLAGS += -DSPRITES_OFF_TEST
endif
# `make TAILPROBE=1` = MD tail-split + mean handler-span probes (LOOP 6).
# NEVER SHIP: the probes add per-vint work to the overloaded tail and
# shift V-gate outcomes (measured demo 52.1 -> 54.6, demo2 20.9 -> 23.4).
# Diagnose with them, ship without. Decoded by tools/win_probe.lua.
ifdef TAILPROBE
MDCCFLAGS += -DTAIL_PROBE
endif
# `make TAILBURN=1` = LOOP 7 diagnostic: pad the MD handler back out to
# its pre-LOOP-7 length. NEVER SHIP — it exists only to separate "the
# channel changed" from "the 68K now runs more" when reading blit skips.
ifdef TAILBURN
MDCCFLAGS += -DTAIL_BURN
endif
# `make WINSPLIT=1` = LOOP 9 diagnostic: split the master's slot-5
# `blit_preempt` term into blit-only ([23]), post-blit waits ([24]) and
# rows blitted ([25]). NEVER SHIP. Decoded by tools/win_probe.lua and by
# the savestate readers in tools/wait_split.py.
# `make ROWSTALE=1` = LOOP 9: how many master rows are byte-identical to
# the same row one cycle ago ([32] identical / [33] checked). Decides
# whether a dirty-row blit is worth building. NEVER SHIP — it hashes
# every row on top of blitting it.
ifdef ROWSTALE
SHCCFLAGS += -DROWSTALE_PROBE
endif
# `make SPANPROBE=1` = LOOP 9: pickup-V histogram ([34..41]) + blit-span
# histogram ([42..49]) + late restores split by pickup half ([50]/[51]).
# The mean span already fits vblank; this asks what the TAIL looks like
# and whether a late pickup is what makes it. NEVER SHIP.
ifdef SPANPROBE
SHCCFLAGS += -DSPAN_PROBE
endif
# `make BQCHUNK=1` = LOOP 13: bound the band queue's two long
# single-shots (cache_fill adaptive drain, build_maps terminator) to
# per-visit quanta — SPAN_PROBE v3 put 98.9% of missed pickups behind
# them on ares. Falsifier: stage-6 misses collapse in span_hist.py
# without stale-color regression (chunked maps drain 1/window under
# load). Candidate for shipping if it holds; probe until then.
ifdef BQCHUNK
SHCCFLAGS += -DBQ_CHUNK
endif
# `make MDVERIFY=1` = LOOP 13 tick-row probe: the MD receiver reads
# every tile-batch entry back through the data port and tallies
# mismatches (0xFFB0EA; first bad triple at WRAM 0xFFA000). The sky
# tick-row is ares-only, MD-plane-side; this decides whether receiver
# writes are being lost/misplaced there. Doubles the receiver span —
# NEVER SHIP.
ifdef MDVERIFY
MDCCFLAGS += -DMD_VERIFY
endif
# `make TILERATE=1` = LOOP 11: how often the game dirties a tilemap page
# ([54] pages copied, [55] cycles with any dirt, [56] pages pending).
# The MK2 pivot rests on this being rare. tools/tile_rate.py. NEVER SHIP.
ifdef TILERATE
SHCCFLAGS += -DTILE_RATE
endif
# `make IDLETOKEN=1` = LOOP 11 (a): Chaotix's idle-token + poll-and-skip
# handshake instead of raise-FM-and-spin. Needs BOTH sides, so it also
# goes to the 68K shim. Falsifier: MD window/ack span must fall well
# below its ~200-line worst; watch blit skips do not run away.
ifdef IDLETOKEN
SHCCFLAGS += -DIDLE_TOKEN
MDCCFLAGS += -DIDLE_TOKEN
endif
# `make IDLEGRACE=1` = LOOP 11 (a): the dial between "spin forever"
# (baseline) and "skip immediately" (IDLETOKEN). Before skipping the
# window, poll COMM4 for as long as the flip stays legal (V<=0xE2, the
# vblank gate's own bound). Implies IDLETOKEN. A skip costs a whole
# frame; a grace poll costs the lines it actually waits.
ifdef IDLEGRACE
SHCCFLAGS += -DIDLE_TOKEN
MDCCFLAGS += -DIDLE_TOKEN -DIDLE_GRACE
endif
# `make CMDPROBE=1` = LOOP 11: how many lines would interrupt-driven
# window pickup recover? The MD asserts CMD INT after posting the
# command; the SH-2 ISR only TIMESTAMPS (no work, no behaviour change);
# the main loop subtracts at pickup. DIAG[59] max / [60] sum / [61] n,
# ~46 FRT ticks per scanline. Falsifier for the ISR rewrite: if the
# recovered budget is small, do not do it. NEVER SHIP.
ifdef CMDPROBE
SHCCFLAGS += -DCMD_PROBE
MDCCFLAGS += -DCMD_PROBE
endif
# `make CMDINT=1` = LOOP 11: interrupt-driven window pickup. The MD
# raises CMD INT before posting; the SH-2 ISR sets a yield flag (it does
# NO window work); compose strips test it between YIELD_ROWS-row chunks
# and resume where they stopped. Targets the MEASURED 12.1% of ares
# pickups that land past the master's v<=0xE4 accept bound and drop a
# blit phase -- the stale band that reads as green tearing.
# Falsifier: blit skips must fall well below 26.6% of cycles on a LONG
# ares state, with parity statics unmoved.
# `make MDBG=1` = LOOP 11 PIVOT slice 1a: paint a test pattern into MD
# Plane B and leave the 32X BG rows at pixel 0 (the MD-through value).
# Answers the one question the whole pivot rests on and has never been
# tested: does MD video composite through our 32X layer at all?
ifdef MDBG
SHCCFLAGS += -DMD_BG
MDCCFLAGS += -DMD_BG
endif
# MDBGFG0=1 = the FULL pivot configuration: BG *and* FG cat-0 off the
# 32X, which is what section 4 actually proposes. Implies MDBG.
ifdef MDBGFG0
SHCCFLAGS += -DMD_BG -DMD_BG_FG0
MDCCFLAGS += -DMD_BG
endif
# MDBGALL=1 = BG + FG cat-0 off the 32X; TEXT stays on the 32X.
# MD_BG_TEXT was an UNIMPLEMENTED INTENT (2026-08-14): it compiled out
# both 32X compose_text sites with a "text to the MD as well" comment,
# but no MD-side text path was ever built — every MDBGALL build since
# simply had NO text layer (Mike's "missing HUD": score, health, round
# text all gone, MAME-confirmed with play_32x). Text costs ~96 patterns
# on the 32X compose; moving it to the MD Window plane is a real design
# item (W-position regs, priority vs FG cat-0), tracked in LOOP13.
# MDBGTEXT=1 re-adds the flag for whoever implements the MD side.
ifdef MDBGALL
# PG_STICKY folded into the bundle 2026-08-16: Mike's ares verdict on
# 652cb5fe was "nearly flawless"; the stale-truth class it fixes is
# MDBGALL-blocking (LOOP14).
SHCCFLAGS += -DMD_BG -DMD_BG_FG0 -DPG_STICKY
MDCCFLAGS += -DMD_BG
endif
ifdef MDBGTEXT
SHCCFLAGS += -DMD_BG_TEXT
endif
# `make CUTBLANK=1` = LOOP 14 item 2: at a scene-cut claim storm (one
# nt chunk claiming >=80 slots arms a 12-chunk countdown), cells whose
# slot's art has not shipped yet go out as the BLANK slot instead of
# the stale art the slot still holds — the J-field of foreign art at
# cuts becomes blank cells under the fade, filling in as uploads land
# (heal <=1 rotation after each tile lands). Bandwidth-neutral by
# design: the cut drain is at the transport floor (measured 2026-08-15,
# tools/cut_profile.lua: 792 claims drained in 28 windows = 8 discovery
# chunks + 20 batches; the 768-word packet is full at MD_BATCH=40+pal).
# Counters at the 0x28FA0 scrap: [0] blanked cells, [1] cut arms.
# Candidate for the MDBGALL bundle if Mike's pass likes it.
ifdef CUTBLANK
SHCCFLAGS += -DCUT_BLANK
endif
# `make PGROTOR=1` = LOOP 14 item 3 ROOT FIX candidate: background
# tilemap-truth re-verify, 2 pages per k1 through the existing
# cap_drain (budget 3->5). MEASURED (2026-08-15, tools/nt_dump.lua +
# arc_dump.lua at the eyehold anchor): MDBGALL truth pages 0-4 hold
# 6947 stale words vs the arcade (ranking-table cat-1 cells the game
# cleared at the cut — the top/bottom trash bands and the stage-1 sky
# garbage); the SHIPPING flavor at the same anchor is 0-word EXACT.
# The MD_BG window work shifts capture timing so streamed staging
# writes slip past pg_watch's one-quiet-capture drop and are never
# re-marked. The rotor bounds any such miss to <=0.65s instead of
# forever. Cost ~0.1-0.3ms per k1 (full-13 refresh was ~0.7ms).
# DEAD ON ARES (Mike, 2026-08-16, frames 3239-42 + bs9): backgrounds
# CYCLE between scenes' art — the rotor captures unmarked pages
# outside the settled-mark discipline, so on ares timing it takes
# mid-write staging states into truth continuously (exactly the
# half-written-page hazard the k2 merge comment warns about; MAME's
# capture timing happened to land post-write). NEVER SHIP. The root
# fix is PGSTICKY below.
ifdef PGROTOR
SHCCFLAGS += -DPG_ROTOR
endif
# `make PGSTICKY=1` = LOOP 14 item 3 root fix, take 2: sticky watch.
# The original miss: thunks mark at POINTER-LOAD; the budgeted drain
# can capture the page before the stream's writes arrive, see no
# change, and drop it — nothing ever re-marks (the eyehold 6947-word
# staleness, MDBGALL-only because MD_BG work shifts drain timing into
# that too-early window). Fix: every mark puts the page into pg_watch
# (not just pg_pending), and watch drops only after 3 consecutive
# quiet captures. All captures stay inside the settled-mark path —
# no unmarked-page races (the PGROTOR disease), ~zero steady-state
# cost, ~3 extra page captures per transition burst.
ifdef PGSTICKY
SHCCFLAGS += -DPG_STICKY
endif
# `make NTWRAP=1` = LOOP 15 wrap protocol, PHASE A (display math only,
# bandwidth-neutral): cells land at wrapped 64x32 plane positions with
# per-row placement headers; the MD switches to cell-strip hscroll
# (reg 0x0B=0x02) and applies FULL per-strip hscroll + full VSRAM vy —
# per-band fine-x parallax becomes exact for the first time (the old
# path applied ONE full-screen hscroll from strip 0). Phase B (ship
# only incoming columns + slow heal walk; consume mean 47.3 lines ->
# ~10 on the ares WINSPAN meter) builds on this once A gates. NOTE:
# margins no longer exist (whole plane live) — bs9_audit's margin and
# window checks need the md_dbg_base array (0x3E780) to locate rows.
ifdef NTWRAP
SHCCFLAGS += -DNT_WRAP
MDCCFLAGS += -DNT_WRAP
endif
# `make ... EDGE42=1` (needs NTWRAP R60): ship the two cells beyond each
# screen edge as an optional pair after the row's span (w1 bit 15) so a
# column is on the MD plane before it scrolls in; drops the FB cat-1
# edge fallback under CAT1MD.
ifdef EDGE42
SHCCFLAGS += -DEDGE42
MDCCFLAGS += -DEDGE42
endif
# `make SNAP1=1` = LOOP 16 step 1: single-frame snapshot. Sprites push
# after k1 (land at k2); regs+sprite snapshot latch at k2, BEFORE R0's
# compose — all three bands of a displayed frame see ONE game state.
# Kills the band-tear class (sprite halves / scroll seams) by
# construction. Protocol change on BOTH CPUs — whole and gated.
ifdef SNAP1
SHCCFLAGS += -DSNAP_ONE
MDCCFLAGS += -DSNAP_ONE
endif
# `make WIN2=1` = LOOP 16 step 2: the 2-window cycle. The MD posts
# k1,k2 then one IDLE vint (the game keeps it whole); all three bands
# launch at k2 from ONE snapshot (implies SNAP1); blits regroup
# (k1 = R0+R1, k2 = R2 pre-flip); the sprite packet grows to 852 so
# text keeps 2 chunks/cycle. Target: 68K handler mean ~130 -> ~105,
# game ~49% -> ~60% of the MD 68K. Gates: canaries first (dreq_inc,
# DRQR[7], cadence), then parity anatomy, then Mike (speed + text
# freshness + the cycling logo + tearing).
ifdef WIN2
SHCCFLAGS += -DSNAP_ONE -DWIN_TWO
MDCCFLAGS += -DSNAP_ONE -DWIN_TWO
endif
# `make SPRREUSE=1` = LOOP 16 motion-parity probe: per-cycle sprite
# decode-job repeat rate + cost split at the k2 snapshot (the frame
# cache's hit ceiling). Overlays ROWHASH 0x28D00. NEVER SHIP.
ifdef SPRREUSE
SHCCFLAGS += -DSPR_REUSE
endif
# MDPAYOFF=1 adds the transparent-area scan inside blit_half. It reads
# every row a second time, so it inflates the blit -- diagnosis only.
ifdef MDPAYOFF
SHCCFLAGS += -DMD_PAYOFF
endif
ifdef CMDINT
SHCCFLAGS += -DCMD_INT
MDCCFLAGS += -DCMD_INT
endif
# `make ... K2FREE=1` = LOOP 24 step 2: the 68K k2 SPIN DIES. The k2
# shim posts and returns (~5 lines vs 68.2 measured on Y); the SH-2
# owns FM's fall; the V-ISR arms the DREQ channel and still runs the
# flip span. Forced along (census-derived, see docs/log/LOOP24.md K2FREE):
# sprites return to the 0xFF7000 mirror + the SPR_TRUNC push, layer
# regs/rowscroll (text >= 0xE80) return to the 0xFF8000 mirror + the
# prefix-82 packet (patch_game split; glyphs stay FBTEXT-harvested),
# and the MD-plane consumes move to k1 entry (staged: FM=0, not
# vblank). EXCLUDES FBSPR and PKTSLIM by construction — the build
# fails hard rather than let a stale canonical line ship torn
# sprites. Implies VISRFLIP. Pixel-gate pixels with SPRFULL=1
# (full-length record pushes MAME can read — the SPRTRUNC/harvest
# precedent); speed/cost gates on headless ares.
ifdef K2FREE
ifdef FBSPR
$(error K2FREE excludes FBSPR: sprites must live in the 0xFF7000 mirror)
endif
ifdef PKTSLIM
$(error K2FREE excludes PKTSLIM: the prefix-82 family carries live regs)
endif
VISRFLIP = 1
SHCCFLAGS += -DK2_FREE
MDCCFLAGS += -DK2_FREE
endif
# `make ... R60=1` = THE REBUILD (docs/design/REBUILD.md P1/P2): one vint = one
# frame at 60Hz. Evolves the K2FREE endpoint: every vint is the SAME
# window (ISR flip + full 224-row blit + captures + MD-plane publish);
# the k-phase machinery, idle beats, and the two-packet family die;
# sprites+regs+palette merge into ONE per-vint push (R60 family in
# packet_fmt.h) landed against an IDLE master — the 68K consumes and
# pushes at vint entry while the master WAITS, then runs its FM span
# while the game runs. Implies the V-ISR (VISR defsym). Canonical R60
# line: make MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1
# R60=1  (FMGATE supplies the patch-side writer gates + text split;
# WIN2/CUT30/K2FREE/BQCHUNK/BANDSHIFT do not apply).
ifdef R60
# STRIKE S1 (docs/design/PIPELINE.md): TRIED AND REVERTED 2026-08-22, three
# variants, all measured dead. (1) FBSPR=1 game-direct FB upload:
# ZERO sprites - the upload runs inside the master's FM=1 span and
# ares discards MD FB writes at FM=1 (the LOOP24 guard was right).
# (2) shim copies records->FB pre-post: flips 83%->70% - any 68K
# work before the post delays the flip past the edge guard.
# (3) copy after post: sprites lag scroll by one frame (fidelity
# kill, not built). VERDICT: the DREQ push IS the minimal
# same-frame 68K->SH-2 record channel under R60. Do not retry
# without changing the FM ownership model itself.
ifdef K2FREE
$(error R60 replaces K2FREE - do not combine)
endif
ifdef WIN2
$(error R60 replaces WIN2/CUT30 - do not combine)
endif
# SH side: R60 EVOLVES the K2FREE endpoint — it compiles the V-ISR,
# arming, flip-span and snapshot machinery via those defines and
# supersedes their packet paths with the R60 family. MD side: the R60
# shim replaces the whole window/cadence block; only FM_GATE (from
# FMGATE=1 in the canonical line) is needed there.
SHCCFLAGS += -DR60 -DK2_FREE -DVISR_FLIP -DSNAP_ONE -DWIN_TWO -DCUT_30
# TEXTCAP_SLAVE is in the bundle but must be switchable: LOOP28 96 measured
# the master waiting 31.8 scanlines for the slave to PICK UP a capture that
# then takes 4.3. `make ... TEXTCAPMASTER=1` runs it inline instead.
ifndef TEXTCAPMASTER
SHCCFLAGS += -DTEXTCAP_SLAVE
endif
MDCCFLAGS += -DR60
endif
# `make ... PALDELTA=1` = the palette-transport arc's packet diet
# (R60 layout v3). The pal_fm_census verdict (2026-08-30, 12.7k-frame
# full-level rig): palette writes land EVERY frame (glow streamer,
# 75% of writes) but only ~10 words/frame actually CHANGE (p50 8,
# p99 24, p999 81) — the 107-word/push pal payload is ~10x amplified
# by 32-word block granularity + redundant rewrites; and 80% of
# writes land inside the FM span even in MAME, so FM-gated staging
# writes are conservation of pain (the diet-ledger warning, now
# measured). Instead the 68K packer diffs dirty blocks against a
# WRAM shadow (0xFF6000) and ships per-block word deltas
# (mask + changed words); dense blocks and tear recovery ship raw
# (id bit 7). WRAM compares are ~free; the saving is adapter words
# at ~0.65 lines each.
ifdef PALDELTA
ifndef R60
$(error PALDELTA is an R60-family packet layout - needs R60=1)
endif
MDCCFLAGS += -DPAL_DELTA
ifdef ADAPTERCAL
MDCCFLAGS += -DADAPTER_CAL
endif
SHCCFLAGS += -DPAL_DELTA
# PD_VFLOOR — TRIED AND DEAD, OFF BY DEFAULT (attempt A, 2026-08-30).
# Premise: the diet lands the packet 27 lines early (census: chain
# arrival 130.7 -> 103.6 lines) and the phase shift drives the seam
# generation-mixing blinks (0.27 -> 1.54/1000f); hold the chain to
# the old phase. MEASURED at 6100/5600/5300 ticks: bad1 223-274 (vs
# 176 base / ~190 no-floor), compose-skip 56.5-58.8% (vs 53.4/50.9),
# bq and D29 worse everywhere. Baseline's extra 27 lines were a
# DISTRIBUTION (the FIFO still draining, early landings running
# early); a clamp forces every window to the late edge and eats the
# inter-window slack that absorbed jitter — the next push collides
# and tears. A dead spin cannot reproduce a distribution. The fix
# for the seam class is arc B: generation-coherent band shipping.
# The knob and the arrival census (0x28FF8/FFC) stay for probes.
ifdef PDFLOOR
ifneq ($(PDFLOOR),0)
SHCCFLAGS += -DPD_VFLOOR=$(PDFLOOR)
endif
endif
endif
# `make ... CRAMFLIP=1` = ARC B HALF 1: tile/sprite group REMAP
# paints deferred to the flip (an ISR ring, <=32 entries, drained
# right after the reveal — content and CRAM mapping switch together).
# The blink strips were CRAM recolor flashes (cross-correlation:
# zero displacement); census: ~15 remap paints/frame land mid-scan
# of the displayed old generation without this. VALUE paints (fades)
# stay live. Not PALDELTA-specific — the mid-scan remap predates the
# diet; the diet's phase shift made it 5.7x more visible.
ifdef CRAMFLIP
SHCCFLAGS += -DCRAM_FLIP
endif
# `make ... PALSTORM=1` = LOOP 25: the palette-channel storm census —
# per-k2-vint {backlog, newly-dirtied, shipped, storm vints, torn-CRAM
# proxy, tile/sprite-half split} at 0xFFA060 (decoded by state_health
# and by headless-ares wram dumps). LOOP22 sized PKT_PAL_KMAX=4 from a
# steady-state census; this measures the transform/transition storms
# behind the black smoke and the inverted-palette flash. NEVER SHIP:
# two 64-bit popcounts per k2 vint in the push path.
ifdef PALSTORM
MDCCFLAGS += -DPAL_STORM
endif
# `make ... K2FREE=1 SPRFULL=1` = the MAME pixel-gate arm: push all 64
# sprite records (full 596, TE sets, MAME reads the landing). NEVER
# ship to ares — it wastes ~35 lines of 68K push per cycle for MAME's
# benefit only.
ifdef SPRFULL
MDCCFLAGS += -DSPR_FULL
SHCCFLAGS += -DSPR_FULL
endif
# `make ... VISRFLIP=1` = LOOP 24 ISR-cost probe: the master enables its
# OWN V-interrupt and runs the k2 flip-critical span (capture -> truth
# drain -> FBCTL flip -> restore) from the V-ISR at vblank entry, so the
# flip can no longer latch mid-scan behind a late polled pickup (the
# tear: flip-late-latches 7.2% of cycles on the X state). NOT CMDINT
# again: no yield machinery — the ISR is a bounded in-order detour and
# the interrupted compose strip resumes untouched. Probe stage per
# LOOP24's risk register: window body unchanged, 68K k2 spin kept (the
# ISR waits ~<=15 lines for the 68K's post, which guarantees FM=1 and
# in-vblank; on any bail the cycle runs the LOOP23 path exactly).
# Counters DIAG[56..63], see visr_vbi in m_main.c. Gate: MAME structure
# A/B + skips must hold, then ares flip-late-latches -> ~0.
ifdef VISRFLIP
SHCCFLAGS += -DVISR_FLIP
endif
# `make YIELDROWS=n` = chunk size for the yieldable strips. 12 = one
# chunk = no yielding, which isolates the restructure's own cost from
# the cost of chunking.
ifdef YIELDROWS
SHCCFLAGS += -DYIELD_ROWS=$(YIELDROWS)
endif
# (BLITDMA and BLITUNC retired in LOOP 9 with their answers. The DMAC
# blit measured 1.77x SLOWER on ares and BLITUNC exists only to prove
# MAME models no FB write cost — neither is a build anyone should be
# able to ship by accident. docs/log/LOOP.md negatives 20 and 21.)
# `make FMTEST=1` = LOOP 9: does an SH-2 write to the framebuffer land
# while FM=0 (outside the window)? That assumption rules out BOTH the
# shadow bank and composing straight into the FB, and was never tested.
# Writes to dead FB space (0x11A00-0x12000), carries a positive control.
# NEVER SHIP.
ifdef FMTEST
SHCCFLAGS += -DFM_TEST
endif
# `make WAITSPLIT=1` = LOOP 9: inside the pickup->restore span, split the
# master's blit from its SYNC[2] wait on the slave, PER WINDOW. k=1 owns
# 100% of the past-vblank restores and its excess over k=0/k=2 doubled
# under load (3.6 -> 7.9 lines), which 4 extra rows cannot explain.
# NEVER SHIP.
ifdef WAITSPLIT
SHCCFLAGS += -DWAIT_SPLIT_PROBE
endif
# `make PICKUPSRC=1` = LOOP 9: does the slave answer the preempt mailbox
# from inside its concurrent compose (a DATA DEPENDENCY — it is composing
# the very rows it is being asked to blit) or from the idle loop (a
# polling delay)? The two want opposite fixes and 7f died on the wrong
# one. NEVER SHIP.
ifdef PICKUPSRC
SHCCFLAGS += -DPICKUP_SRC_PROBE
endif
# (PIPE2 is now the DEFAULT — it won Mike's play pass and is folded in.
# See slave_concurrent_k: compose a band two windows before shipping it.)
# (BLITBAL retired in LOOP 9 — the even thirds LOST Mike's play pass on
# the seams they cost. Retired rather than left here to mislead, the same
# call BLITBURN got. docs/log/LOOP.md negative 23.)
ifdef WINSPLIT
SHCCFLAGS += -DWIN_SPLIT_PROBE
endif
# (SPINPROBE retired in LOOP 8 with the COMM stream it capped. It did its
# job: N=0 moved the reject band 57.1 -> 39.4% on its own, which is what
# identified the ack-spin as the elastic sink and set this whole arc off.)
MDASFLAGS  = -x assembler-with-cpp -Imd_src -m68000 -Wa,--register-prefix-optional
SHASFLAGS  = -Ish_src --small
# NOTE: this is a plain `=` and it sits BELOW the flag blocks, so any
# `SHASFLAGS +=` written up there is silently discarded. Assembler flags
# must be appended HERE, after the base assignment. CMDPROBE lost its
# --defsym to exactly that and the probe assembled to nothing.
ifdef CMDPROBE
SHASFLAGS += --defsym CMD_PROBE=1
endif
ifdef CMDINT
SHASFLAGS += --defsym CMD_INT=1
endif
ifdef VISRFLIP
SHASFLAGS += --defsym VISR_FLIP=1
endif
ifdef R60
SHASFLAGS += --defsym VISR_FLIP=1
SHCCFLAGS += -DVISR_FLIP
endif
# `make ... BOOTBEACON=1` = 2026-09-07 hardware boot probe: backdrop colour
# per boot stage (md_start.s) + the master SH-2's progress on COMM12
# (mars_start.s). For MiSTer/hardware only; harmless elsewhere.
ifdef BOOTBEACON
SHASFLAGS += --defsym BOOT_BEACON=1
MDASFLAGS += -Wa,--defsym,BOOT_BEACON=1
endif
# `make ... BOOTHALT=1` = paint blue at the 68K's first instruction and halt.
ifdef BOOTHALT
MDASFLAGS += -Wa,--defsym,BOOT_HALT=1
endif
# `make ... BOOTBEACON=1 BOOTSTAGE=1` = a colour per 68K init stage, halt before M_OK.
ifdef BOOTSTAGE
MDASFLAGS += -Wa,--defsym,BOOT_STAGE=1
endif
# `make ... BOOTBEACON=1 BOOTROMCHK=1` = master reads cart ROM above 2 MB; blue = ok, cyan = wrong.
ifdef BOOTROMCHK
SHASFLAGS += --defsym BOOT_ROMCHK=1
MDASFLAGS += -Wa,--defsym,BOOT_ROMCHK=1
endif
# `make ... BOOTSHSTAGE=1` = master SH-2 boot stages: COMM12 tint on the MD backdrop
# (before the 32X display is on), then 32X CRAM 0 colours; exceptions = magenta + halt.
ifdef BOOTSHSTAGE
SHCCFLAGS += -DBOOT_SHSTAGE
MDCCFLAGS += -DBOOT_SHSTAGE
SHASFLAGS += --defsym BOOT_SHSTAGE=1
endif
# `make ... BOOTSLVNOCACHE=1` = the slave jumps into SDRAM with its cache OFF (probe).
ifdef BOOTSLVNOCACHE
SHASFLAGS += --defsym BOOT_SLVNOCACHE=1
endif
# `make ... BOOTSHSTAGE=1 BOOTBANK3HALT=1` = 68K reads the game image via bank 3, white/red, halt.
ifdef BOOTBANK3HALT
MDCCFLAGS += -DBOOT_BANK3HALT
endif
# `make ... BOOTVPULSE=1` = the 68K vint handler alternates 32X CRAM 0 (red/blue once the game runs).
ifdef BOOTVPULSE
MDCCFLAGS += -DBOOT_VPULSE
endif
# `make ... BOOTWSTAGE=1` = master paints 32X CRAM 0 at render-window stages (red/yellow/green/white).
ifdef BOOTWSTAGE
SHCCFLAGS += -DBOOT_WSTAGE
endif
# `make ... BOOTSHSTAGE=1 BOOTFBDMAHALT=1` = VDP DMA from the 32X framebuffer: white ok / red bad, halt.
ifdef BOOTFBDMAHALT
MDCCFLAGS += -DBOOT_FBDMAHALT
endif
# `make ... BOOTFBBAR=1` = master draws a magenta bar into FB rows 0-7 before every flip.
ifdef BOOTFBBAR
SHCCFLAGS += -DBOOT_FBBAR
endif
# `make ... BOOTVISRCHK=1` = after 120 game vints, white if the master's V-ISR ever ran, red if never; halt.
ifdef BOOTVISRCHK
SHCCFLAGS += -DBOOT_VISRCHK
MDCCFLAGS += -DBOOT_VISRCHK
endif
# `make ... BOOTFMCHK=1` = acked window with FM stuck: master paints red, retries the drop, green/blue.
ifdef BOOTFMCHK
SHCCFLAGS += -DBOOT_FMCHK
MDCCFLAGS += -DBOOT_FMCHK
endif
# `make ... BOOTGATECHK=1` = 68K paints its window-gate decline reason every vint (flickering shades).
ifdef BOOTGATECHK
MDCCFLAGS += -DBOOT_GATECHK
endif
# `make ... BOOTPKTCHK=1` = after 120 game vints: red no windows / yellow no whole landings / green ok; halt.
ifdef BOOTPKTCHK
SHCCFLAGS += -DBOOT_PKTCHK
MDCCFLAGS += -DBOOT_PKTCHK
endif
# `make ... BOOTFLIPTEST=1` = master: bar in bank A, flip, bar in bank B, flip, halt (display-path test).
ifdef BOOTFLIPTEST
SHCCFLAGS += -DBOOT_FLIPTEST
endif
# `make ... BOOTFRTCHK=1 BOOTPKTCHK=1` = the master's FRT ticks per vint, classified by the 68K at vint 120.
ifdef BOOTFRTCHK
SHCCFLAGS += -DBOOT_FRTCHK
endif
ifdef BOOTGATEOFF
SHCCFLAGS += -DBOOT_GATEOFF
endif
# `make ... MISTERBOOT=1` = both MiSTer boot changes together: the slave
# SDRAM warm-up stub in mars_start.s AND the slave cache off. SUPERSEDED —
# the warm-up is now default (see below) and it is the only half that
# does anything. MISTERCACHE=1 is the cache-off alone; it has never been
# shown to help. The old note here said the default carries neither and
# that neither fixed the hang; both statements were wrong, and six black
# captures on 2026-09-08 were spent finding that out.
# `make ... FLIPDEFER=1` = LOOP27 9, the 60Hz blocker: the K2FREE edge
# guard DROPS any flip that misses the 38-line vblank, and at one
# game-frame per vint it misses most of them (flips on ~17% of vints).
# The FPGA RTL (srcref/S32X_MiSTer rtl/32X/VDP.sv) latches `FS <= FBCR.FS`
# only when VBLK: real silicon DEFERS a late flip to the next vblank, it
# does not tear. ares latches immediately mid-scan instead, so we do the
# deferral in software — arm on the late arrival, commit at the top of
# the next vblank. Counters: DIAG[6] arms, DIAG[5] commits.
# `make ... BOOTABDRAW=1` = A/B WRITER PROBE (LOOP27 10b). Does the
# MASTER's framebuffer write reach the display on the FPGA? The 68K's
# does (s16_68kdraw), and every SH-2-driven frame is black. Master paints
# MAGENTA at rows 8-15 as the last FB write of the window; the 68K paints
# GREEN at rows 16-23 at vint top as the positive control, with the 32X
# display forced on. Rows 8+ ON PURPOSE: rows 0-7 are off Mike's display,
# which is what made the original BOOT_FBBAR reading worthless.
# `make ... BOOTPALTEST=1` = CRAM residue test: hammer CRAM 1 yellow and
# CRAM 2 red every window, direct stores bypassing cram_set/PALPEN. If
# the screen changes colour our writes land; if it stays magenta/green
# the picture is residue and nothing of ours reaches CRAM.
ifdef BOOTPALTEST
SHCCFLAGS += -DBOOT_PALTEST
endif
ifdef BOOTABDRAW
SHCCFLAGS += -DBOOT_ABDRAW
MDCCFLAGS += -DBOOT_ABDRAW
endif
# `make ... BOOTABREAD=1` = the follow-up to a NO-BAR abdraw result: the
# master still paints its bar, and the 68K reads those FB bytes at FM=0
# and paints the MD backdrop green (found) or red (not found). MD palette
# because FM cannot gate it. Implies the master half of BOOTABDRAW.
ifdef BOOTABREAD
SHCCFLAGS += -DBOOT_ABDRAW
MDCCFLAGS += -DBOOT_ABREAD
endif
# `make ... BOOTABBOTH=1` = the bar written on BOTH sides of the flip, so
# both banks carry it and bank/flip stops being a variable. Implies the
# master half of BOOTABDRAW. Pair it with BOOTGATEOFF.
# `make ... PALVBL=1` = THE FIX for LOOP27 entry 18: 32X CRAM writes are
# silently dropped outside hblank/vblank on real silicon (PEN, RTL
# VDP.sv:170/403/405), and our paints run mid-window = active scan, so the
# whole 32X layer drew black on black on the MiSTer. cram_set now defers
# into a dirty bitmap and the entries are flushed at the flip, inside
# vblank — the idiom 32X240pTestSuite ships (pri_vbi_handler).
# `make ... BOOTTAGBLUE=1` = identity tag: the 68K paints the MD backdrop
# blue every vint, through MD CRAM which neither FM nor the 32X display
# gate can touch. Proves WHICH rom is running before any colour on the
# screen is trusted.
# `make ... BOOTMDPAL=1` = which layer is on screen? The 68K paints the
# WHOLE MD palette red every vint. Red picture = we are looking at the MD
# plane (MDBGALL draws BG/FG-cat0 on the MD VDP), not the 32X layer.
# `make ... BOOTPALWRAM=1` = force the MD background palette through the
# WRAM-staged path instead of the DMA whose source is the 32X
# framebuffer. Tests "does this core serve VDP DMA from the FB", the
# untested suspect behind the wrong MD palette on the MiSTer.
# `make ... BOOTPALPEEK=1` = the 68K reads the 48 palette words the
# master wrote into the FB packet at offset 688 and paints the verdict on
# the MD backdrop: RED all-zero, YELLOW all-identical, GREEN varied.
# Answers "is the palette data even there" instead of inferring it.
# `make ... BOOTPALSHOW=1` = flood the screen with the RAW palette word
# the packet carries for CRAM 17 (the sky pen), instead of a verdict.
# Answers "is the packet's palette the wrong colour, or is the upload
# mangling a right one". Implies BOOTPALPEEK's plumbing.
# `make ... BOOTPALRAMP=1` = master writes a known ramp (0x0100+i) into
# the 48 palette words; the 68K checks it and floods GREEN exact /
# YELLOW-ish shifted (shift in the red level) / RED corrupted. Separates
# "wrong source palette" from "transport shifts" from "transport
# corrupts" in one run.
# `make ... BOOTMOTION=1` = cadence, readable with a wristwatch: the MD
# palette steps through 8 colours every 8 vints, so one full cycle is 64
# vints = ~1 second at 60 Hz. Answers "is hardware streaming, and how
# fast", which no probe in this arc has ever measured.
ifdef BOOTMOTION
MDCCFLAGS += -DBOOT_MOTION
endif
# `make ... BOOTMOTIONGAME=1` = the same wheel driven by the GAME's scene
# timer (0xFFF02A) instead of vints. Cycle time reads the game-frame rate
# straight off a wristwatch; against the vint wheel's 1 cycle/s that
# ratio is the hardware miss rate.
# `make ... BOOTSPAN=1` = measure the 68K vint handler's own length on
# hardware, in MD scanlines, and flood the palette with the bucket:
# green <64, yellow <131, orange <196, red <262, WHITE = overrunning the
# frame. Does not perturb the game, unlike MDCONSUMEOFF.
# `make ... BOOTSTAGEMAX=1` = which stage of the 68K consume is slowest,
# from the four V stamps the shipping code already writes. Floods WHITE
# (nothing slow) / RED entry->scroll / GREEN scroll->DMAs / BLUE
# spans->cells / YELLOW cells->end. The follow-up to BOOTSPAN.
# `make ... BOOTPRECONSUME=1` = how many scanlines pass between the 68K
# entering its vint handler and the consume starting. span2 proved the
# consume itself is only 8-24 lines on hardware, so the frame is going
# somewhere BEFORE it. GREEN <16 / YELLOW <48 / ORANGE <112 / RED >=112.
# `make ... BOOTTAIL=1` = which part of the 68K handler TAIL holds the
# frame, from V stamps the shipping code already writes. WHITE not the
# tail / RED wait-to-post / GREEN the master's window / BLUE the DREQ
# push / YELLOW the flip-hold echo.
# `make ... BOOTPUSHLEN=1` = the DREQ push's LENGTH in scanlines, after
# BOOTTAIL named it the biggest tail stage on hardware. GREEN <48
# (ares-like) / YELLOW <96 / ORANGE <160 / RED >=160.
# `make ... BOOTPUSHWHERE=1` = which of r60_push's five internal stages
# holds the frame, from the PUSH AUTOPSY stamps already in the code.
# WHITE none / RED rotor+compares / GREEN selection / BLUE regs /
# YELLOW pal ship / MAGENTA records ship.
# `make ... BOOTSPIN=1` = the DREQ FIFO-full spin residual (starts 2600,
# decrements once per full-FIFO poll). GREEN 2600 never full (ares) /
# YELLOW >2000 / ORANGE >1000 / RED <=1000 mostly waiting on the master.
# `make ... BOOTNOPOLL=1` = ship the 20 reg words WITHOUT the per-word
# FIFO poll (sound only because the FIFO is provably never full, LOOP27
# 57). Splits "the poll READ is slow" from "the FIFO WRITE is slow".
# MEASUREMENT ONLY — the poll guards against lost words under play load.
ifdef BOOTNOPOLL
MDCCFLAGS += -DBOOT_NOPOLL
endif
# `make ... BOOTREGLEN=1` = the regs-ship stage in scanlines (20 words).
# GREEN <8 / YELLOW <32 / ORANGE <80 / RED >=80. Pair with BOOTNOPOLL to
# split the poll read from the FIFO write by magnitude.
# `make ... BOOTCOMMTIME=1` = time 20 writes to a harmless 32X register
# (COMM2) with the same buckets as the regs stage, to tell "the DREQ FIFO
# is slow" from "every 68K->32X access is slow".
# `make ... BOOTPUSHCUT=1` = ship only the 20 reg words and abandon the
# rest of the packet. Picture will be wrong on purpose; pair with
# BOOTMOTIONGAME and read ONLY the wheel. Tests whether packet size is
# the speed lever on hardware. MEASUREMENT ONLY.
ifdef BOOTPUSHCUT
MDCCFLAGS += -DBOOT_PUSHCUT
endif
# `make ... BOOTSETUP=1` = split the regs stage into SETUP (length reg +
# DREQ enable) and WORDS (the 20 FIFO writes). GREEN both small / RED
# setup dominates / BLUE words dominate / YELLOW comparable.
# `make ... BOOTPUSHDELAY=1` = wait 8 scanlines before starting the DREQ
# push, to test whether the first-words stall is contention with a master
# that is armed but not yet idle. Pair with BOOTREGLEN to read the stage.
# `make ... BOOTVALUE=1` = flood the MD palette with the regs-stage
# duration ENCODED AS A COLOUR (R=d&7, G=(d>>3)&7, B=(d>>6)&3), so the
# exact scanline count is read from one screenshot. Replaces bucketing.
ifdef BOOTVALUE
MDCCFLAGS += -DBOOT_VALUE
endif
# BOOTVALUETOTAL=1 makes BOOTVALUE report the WHOLE push instead of just
# the regs stage — the number that decides whether packet size matters.
# `make ... BOOTFBTIME=1` = time 20 68K writes into the 32X FRAMEBUFFER
# (0x851A00) and report with the value instrument, for direct comparison
# against the 48-line/20-word DREQ FIFO cost. Tests whether the FB is a
# cheaper route for the packet than the FIFO.
# `make ... BOOTFBFREE=1` = probe which framebuffer regions are actually
# unused: the master stamps magic words at 0x12000/0x14000/0x18000/
# 0x1C000, the 68K reads them back a vint later and reports a 4-bit mask
# through the value instrument (bit set = region survived = FREE).
# `make ... BOOTFBXFER=1` = the FB TRANSPORT readback (HANDOFF-DREQ job
# 1). The 68K writes a 13-word sequenced test packet into the FB twice —
# A at 0x12000 before the post (FM=0, pre-flip), B at 0x12040 inside
# r60_push (FM=1, post-flip) — and the master checks BOTH for content and
# for FRESHNESS (sequence advancing by 1 every window), reporting a
# saturating run length per region through the value instrument:
#   d = runA | runB<<3 | 0x40 if both regions' content is correct
#   0x7F = both fresh for 7+ windows running = the transport works
#   d = 42 = the master never ran the check
# BOOTFBXA=1 / BOOTFBXB=1 build the same probe with only ONE writer, to
# bisect which write site blacks the hardware screen.
ifdef BOOTFBXFER
SHCCFLAGS += -DBOOT_FBXFER
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FBXFER -DBOOT_FBX_A -DBOOT_FBX_B
endif
ifdef BOOTFBXA
SHCCFLAGS += -DBOOT_FBXFER
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FBXFER -DBOOT_FBX_A
endif
ifdef BOOTFBXB
SHCCFLAGS += -DBOOT_FBXFER
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FBXFER -DBOOT_FBX_B
endif
# BOOTFBXN=1: the reporting path with NO master-side FB access — the
# control that says whether touching the FB from the master at the window
# body is what blacks the hardware screen.
# BOOTFBXP=1: MD side only, master untouched — the latch is never set so
# the flood must paint the 42 fallback. Black here means the wedge is on
# the MD side; a colour means it is the master's COMM8 post.
# `make ... FBXPORT=1` = THE FB TRANSPORT (LOOP27 67). The r60 packet
# crosses through the 32X framebuffer instead of the DREQ FIFO: the 68K
# writes it at FM=0 before the post (1 scanline per 20 words on hardware,
# against the FIFO's 48), publishes a sequence word LAST, and the master
# copies it into SPR_LAND in its window body — same bytes, same layout,
# the whole harvest downstream unchanged. Not a probe: this is the
# candidate ship route.
# `make ... FLIPCENSUS=1` = count flip outcomes into DIAG. The 32X layer
# changes only at a flip, so flips per V-ISR is the display refresh rate:
#   DIAG[66] flipped   [67] declined past the vblank edge
#   [68] declined nothing drawn   [69] declined nothing shipped
#   [70] V-ISR entries (the denominator)
# FBXLATE=1 moves the FB packet lift BELOW the flip, to measure whether
# the pre-flip position is required or merely assumed.
# `make ... FLIPRATE=1` = report FS writes per 64 vints through the value
# instrument, so the flip rate can be read on the FPGA where no trace
# exists. 64 = a flip every vint; ares measures 27 baseline, 1 FBXPORT.
# `make ... FLIPEDGEOFF=1` = drop the K2FREE vblank-edge guard. It exists
# because ares latches FS mid-scan and tears; the FPGA RTL latches only
# in VBLK and defers a late write itself. Tearing is expected on ares.
# CENCAL=1: bump every census slot once at m_main entry. Each must read
# exactly 1; anything else means that slot is aliased. Run this before
# trusting any new slot.
ifdef CENCAL
SHCCFLAGS += -DCEN_CAL -DFLIP_CENSUS
endif
# NOLANDWAIT=1: remove the pre-blit landing wait on the DREQ build, to
# attribute FBXPORT's flip collapse to it or exonerate it.
# FBXTAIL=1: with FBXPORT, push at the vint TAIL (after the master's ack,
# FM already down) instead of before the post, so the post keeps landing
# inside vblank. Costs one vint of packet latency.
# CLAIMNEW=1: the late claim scans the LIVE sprite list (FB_SPR) rather
# than the snapshot, so a set that arrives while the snap latch is
# skipping a refresh still gets a pair instead of drawing the shadow ramp.
# BOOTBURNW=1 / BOOTBURNR=1: report the SHIMBURN loop's duration when
# executed from 68K WRAM / from cart ROM, through the value instrument.
# ROM minus WRAM is the adapter's fetch tax on 68K code. Pair with
# SHIMBURN=N.
# BOOTENTRYV=1 / BOOTPOSTV=1: LOOP29 146, paint the 68K's vint entry line
# / post line (lines from vblank start) through the value instrument.
ifdef BOOTCONSV
MDCCFLAGS += -DBOOT_VALUE -DBOOT_CONSV
endif
ifdef BOOTENTRYV
MDCCFLAGS += -DBOOT_VALUE -DBOOT_ENTRYV
endif
ifdef BOOTPOSTV
MDCCFLAGS += -DBOOT_VALUE -DBOOT_POSTV
endif
ifdef BOOTBURNW
MDCCFLAGS += -DBOOT_VALUE -DBOOT_BURN_W
endif
ifdef BOOTBURNR
MDCCFLAGS += -DBOOT_VALUE -DBOOT_BURN_R
endif
ifdef CLAIMNEW
SHCCFLAGS += -DCLAIM_NEW
endif
ifdef FBXTAIL
MDCCFLAGS += -DFBX_TAIL
endif
ifdef NOLANDWAIT
SHCCFLAGS += -DNO_LAND_WAIT
endif
ifdef FLIPEDGEOFF
SHCCFLAGS += -DFLIP_EDGE_OFF
endif
ifdef FLIPRATE
SHCCFLAGS += -DFLIP_CENSUS -DFLIP_RATE_POST
ifdef FLIPRATEDIAG
SHCCFLAGS += -DFLIPRATE_DIAG=$(FLIPRATEDIAG)
endif
ifdef FLIPRATEMEAN
SHCCFLAGS += -DFLIPRATE_MEANSUM=$(FLIPRATEMEAN)
endif
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FBXFER
endif
# TEXTCAPEARLY=1 = LOOP29 145. With the slave text capture (TEXTCAPMASTER
# OFF), the master posts the capture at its V-ISR ENTRY instead of after
# the 68K's post; the slave waits for FM itself and copies while the
# master waits for the post. Takes ~10 (ares) / ~20 (FPGA) lines of FB
# reads off the pre-flip path so the FS write lands inside the guard on
# silicon (143/144).
ifdef TEXTCAPEARLY
SHCCFLAGS += -DTEXTCAP_EARLY
endif
# TEXTCAPMASK=1 = LOOP29 147. The game's text writers mark the 4-row
# group they write (patch_game.py TXTMASK, WRAM byte 0xFFA1A6); the shim
# posts the mask in COMM2's high byte (the master reads only bits 0-2 of
# COMM2 for the bank) and clears it once the master has captured; every
# 8th vint the mask is forced full so an ungated writer is never stale
# for more than 8 vints. The master's inline capture copies only the
# marked groups: ~14 text writes a vint touch 1-2 groups of 128 longs
# instead of 928. On the FPGA the full capture was the largest term
# past the guard (144).
ifdef TEXTCAPMASK
SHCCFLAGS += -DTEXTCAP_MASK
MDCCFLAGS += -DTXT_MASK
endif
# TWOPOST=1 = LOOP29 149, the two-post protocol. The 68K posts BEFORE its
# consumes (post A, ~5 lines), the master flips and then drops FM and
# eats the post; the 68K does its VDP DMAs and the packet blast at FM=0
# and posts again (B) for the window, where the text restore now runs.
# On the FPGA the consumes are ~13 lines of the 23-27-line post wait
# (148) and put the FS write past the guard's tail. Needs FBXPEND.
ifdef TWOPOST
SHCCFLAGS += -DTWO_POST
MDCCFLAGS += -DTWO_POST
endif
ifdef TPCONSUMEFIRST
MDCCFLAGS += -DTP_CONSUME_FIRST
endif
ifdef FBXLATE
SHCCFLAGS += -DFBX_LATE
endif
# VBSPAN=1 = LOOP28 94. FRT stamps through flip_span's PRE-FLIP path into
# CEN[24..28] with CEN[29] as the count, so the reader takes means. ~46
# ticks is one scanline and the edge guard is 1650 (38 lines); the whole
# path measured 4717 ticks (~103 lines) on the double-buffered line, which
# is why almost every flip declines. Implies FLIPCENSUS.
# PGFRESH=1 = LOOP28 95. restore_pages replays truth into the bank the
# flip just handed us; measured 2.35 pages per flip of which only 0.10
# came from cycle_dirt — the rest are pg_watch pages whose truth has not
# changed since they were last written into that same bank. This skips
# those. A page is fresh in a bank once restored into it and stops being
# fresh in BOTH the moment cap_page sees its truth change. Needs BLITSKIP
# (fb_draw_par is the bank label).
# CSETCENSUS=1 = LOOP28 99. Distinct System 16 colour SETS drawn per vint
# on the Mega Drive plane, into CEN[39] sum / [40] max / [41] samples.
# The MD plane has 3 lines x 16 pens and an S16 background tile is 3bpp
# (<=8 pens), so six sets fit without eviction. This says whether the
# LRU in mdp_assign_set ever has anything to do. Implies FLIPCENSUS.
# TEXTCAPFULL=1 = LOOP28 100. With TEXTCAPMASTER, capture text EVERY
# frame instead of every other. The inline path's R60 halving is the
# suspected cause of its wrong pixels; the slave path it replaces
# captures every frame.
# TEXTCAPDUAL=1 = LOOP28 101 diagnostic: inline capture PLUS the slave
# post+join, so the barrier is kept and only the capture moves. Separates
# "the join was a barrier" from "the capture is wrong". Saves nothing.
# BGBLANK0=1 = LOOP28 104. Blank a CLEARED background cell (w == 0), not
# just a cleared foreground one. The game zeroes the tilemap at a scene
# change and the background kept drawing tile 0 of set 0, so the previous
# scene survived under the new one — the missing title-screen eye.
# NTPROBE=1 = LOOP28 106. Counts what the name-table pass sees: CEN[47]
# background+foreground cells walked, CEN[48] of them BACKGROUND cells
# whose tilemap word is zero, CEN[49] vints. Answers whether the walk
# reaches the title screen's cleared cells at all.
# DRAINCUT=N = LOOP28 107. Cap the truth drain INSIDE the flip path at N
# pages instead of all 13, so the flip write can reach vblank. The rest
# drains in the body. N=0 empties the flip path of it entirely.
ifdef DRAINCUT
SHCCFLAGS += -DDRAIN_CUT=$(DRAINCUT)
endif
ifdef NTPROBE
SHCCFLAGS += -DNT_PROBE -DFLIP_CENSUS
MDCCFLAGS += -DFLIP_CENSUS
endif
ifdef BGBLANK0
SHCCFLAGS += -DBG_BLANK0
endif
ifdef TEXTCAPDUAL
SHCCFLAGS += -DTEXTCAP_DUAL
endif
ifdef TEXTCAPFULL
SHCCFLAGS += -DTEXTCAP_FULL
endif
ifdef CSETCENSUS
SHCCFLAGS += -DCSET_CENSUS -DFLIP_CENSUS
MDCCFLAGS += -DFLIP_CENSUS
endif
ifdef PGFRESH
SHCCFLAGS += -DPG_FRESH
endif
ifdef VBSPAN
SHCCFLAGS += -DVB_SPAN -DFLIP_CENSUS
MDCCFLAGS += -DFLIP_CENSUS
endif
ifdef FLIPCENSUS
SHCCFLAGS += -DFLIP_CENSUS
MDCCFLAGS += -DFLIP_CENSUS
endif
ifdef FBXPORT
MDCCFLAGS += -DFB_XPORT
SHCCFLAGS += -DFB_XPORT
endif
# BOOTFBXT=1: time the FM=0 FB write (20 words, the comparable count)
# with the value instrument instead of reporting the transport verdict.
ifdef BOOTFBXT
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FBXFER -DBOOT_FBX_A -DBOOT_FBX_TIME
endif
ifdef BOOTFBXP
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FBXFER -DBOOT_FBX_A -DBOOT_FBX_B
endif
ifdef BOOTFBXN
SHCCFLAGS += -DBOOT_FBXFER -DBOOT_FBX_NOFB
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FBXFER
endif
ifdef BOOTFBFREE
SHCCFLAGS += -DBOOT_FBFREE
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FBFREE
endif
ifdef BOOTFBTIME
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FBTIME
endif
# `make ... BOOTGAMERATE=1` = game frames per 64 vints, read exactly off
# the game's scene timer and flooded as a number. 64 = 60 Hz.
ifdef BOOTGAMERATE
MDCCFLAGS += -DBOOT_VALUE -DBOOT_GAMERATE
MDASFLAGS += -Wa,--defsym,BOOT_GAMERATE=1
endif
ifdef BOOTVALUESEL
MDCCFLAGS += -DBOOT_VALUE -DBOOT_VALUE_SEL
endif
ifdef BOOTVALUETOTAL
MDCCFLAGS += -DBOOT_VALUE -DBOOT_VALUE_TOTAL
endif
ifdef BOOTPUSHDELAY
MDCCFLAGS += -DBOOT_PUSHDELAY
endif
ifdef BOOTSETUP
MDCCFLAGS += -DBOOT_SETUP
endif
ifdef BOOTCOMMTIME
MDCCFLAGS += -DBOOT_COMMTIME
endif
ifdef BOOTREGLEN
MDCCFLAGS += -DBOOT_REGLEN
endif
ifdef BOOTSPIN
MDCCFLAGS += -DBOOT_SPIN
endif
ifdef BOOTPUSHWHERE
MDCCFLAGS += -DBOOT_PUSHWHERE
endif
ifdef BOOTPUSHLEN
MDCCFLAGS += -DBOOT_PUSHLEN
endif
ifdef BOOTTAIL
MDCCFLAGS += -DBOOT_TAIL
endif
ifdef BOOTPRECONSUME
MDCCFLAGS += -DBOOT_PRECONSUME
endif
ifdef BOOTSTAGEMAX
MDCCFLAGS += -DBOOT_STAGEMAX
endif
ifdef BOOTSPAN
MDCCFLAGS += -DBOOT_SPAN
endif
ifdef BOOTMOTIONGAME
MDCCFLAGS += -DBOOT_MOTION -DBOOT_MOTION_GAME
endif
ifdef BOOTPALRAMP
SHCCFLAGS += -DBOOT_PALRAMP
MDCCFLAGS += -DBOOT_PALPEEK -DBOOT_PALRAMP
endif
ifdef BOOTPALSHOW
MDCCFLAGS += -DBOOT_PALPEEK -DBOOT_PALSHOW
endif
ifdef BOOTPALPEEK
MDCCFLAGS += -DBOOT_PALPEEK
endif
# `make ... BOOTPALDIRECT=1` = write the MD background palette to CRAM
# with DIRECT stores instead of a VDP DMA. Tests the hypothesis that
# DMA-to-CRAM does not land on the MiSTer core while DMA-to-VRAM and
# direct CRAM writes do — which is what a 9-colour hardware picture
# against a 76-colour ares picture points at. Also a candidate FIX.
# `make ... BOOTCRAMCHK=1` = the 68K READS CRAM BACK after the palette
# upload and counts how many of the 48 entries match what it sent.
# GREEN all 48 / YELLOW some / RED none, flooded at vint top. Settles
# "does the upload land" by reading a value instead of inferring from
# pixels. Needs BOOT_PALPEEK's flood plumbing.
# `make ... DMACENSUS=1` = count the FB-sourced VDP DMAs the consume
# issues per vint and the words they move: 0xFFA240 spans / 0xFFA242
# words / 0xFFA246 tile records / 0xFFA248 words. Sizes whether
# coalescing short spans is worth building. ares-only, no hardware.
ifdef DMACENSUS
MDCCFLAGS += -DDMA_CENSUS
endif
ifdef BOOTCRAMCHK
MDCCFLAGS += -DBOOT_PALPEEK -DBOOT_CRAMCHK
endif
ifdef BOOTPALDIRECT
MDCCFLAGS += -DBOOT_PALDIRECT
endif
ifdef BOOTPALWRAM
MDCCFLAGS += -DBOOT_PALWRAM
endif
ifdef BOOTMDPAL
MDCCFLAGS += -DBOOT_MDPAL
endif
ifdef BOOTTAGBLUE
MDCCFLAGS += -DBOOT_TAGBLUE
endif
# `make ... PALPEN=1` = the palette fix that fits OUR architecture: wait
# for PEN (bit 13 of MARS_VDP_FBCTL) before each CRAM store, so the write
# lands in hblank where hardware accepts it. PALVBL flushed at the flip
# instead — the only vblank+FM point we have — and on hardware the flip
# rarely lands, so nothing reached CRAM at all (LOOP27 22). Bounded spin.
ifdef PALPEN
SHCCFLAGS += -DPAL_PEN
endif
ifdef PALVBL
SHCCFLAGS += -DPAL_VBLANK
endif
ifdef BOOTABBOTH
SHCCFLAGS += -DBOOT_ABDRAW -DBOOT_ABBOTH
endif
# bisect halves of the A/B probe: M = master bar only, K = 68K bar only
ifdef BOOTABDRAWM
SHCCFLAGS += -DBOOT_ABDRAW
endif
ifdef BOOTABDRAWK
MDCCFLAGS += -DBOOT_ABDRAW
endif
ifdef FLIPDEFER
SHCCFLAGS += -DFLIP_DEFER
endif
ifdef MISTERBOOT
SHASFLAGS += --defsym MISTER_BOOT=1
SLVCACHEOFF = 1
endif
# MISTERBOOT bundles TWO independent changes and nothing had ever
# separated them. These split it: MISTERWARM=1 is the SDRAM warm-up stub
# alone, MISTERCACHE=1 the slave cache-off alone.
ifdef MISTERWARM
SHASFLAGS += --defsym MISTER_BOOT=1
endif
# THE WARM-UP IS NOW DEFAULT ON (2026-09-08, LOOP27 69). The 2x2 above was
# finally run on the MiSTer and it is not ambiguous:
#     neither -> black    cache-off only -> black
#     warm-up -> BOOTS    both           -> BOOTS
# The warm-up is the whole fix and the cache-off does nothing. On ares the
# ship line's counters are IDENTICAL with and without it (vints, packets,
# tiles, batches, all columns, frames 200 and 400), so it costs nothing on
# the emulator and it is the difference between a rom that runs on real
# 32X hardware and a black screen. NOSLVWARM=1 takes it back out.
ifndef NOSLVWARM
ifndef MISTERWARM
ifndef MISTERBOOT
SHASFLAGS += --defsym MISTER_BOOT=1
endif
endif
endif
ifdef MISTERCACHE
SLVCACHEOFF = 1
endif
# `make ... SLVCACHEOFF=1` = slave jumps to _s_main with cache OFF (the FB-exec
# workaround variant of the slave SDRAM warm-up; default keeps cache on).
ifdef SLVCACHEOFF
SHASFLAGS += --defsym SLV_CACHE_OFF=1
endif
ifdef BOOTMDMODE
MDCCFLAGS += -DBOOT_MDMODE
endif
ifdef BOOTSLVALIVE
MDCCFLAGS += -DBOOT_SLVALIVE
endif
MDLDFLAGS  = -T md_src/md.ld -nostdlib
SHLDFLAGS  = -T sh_src/mars.ld -nostdlib

MDEXTRA =
SHEXTRA =

MDOBJS  = $(patsubst %.s,%.o,$(wildcard md_src/*.s))
MDOBJS += $(patsubst %.c,%.o,$(wildcard md_src/*.c))
SHOBJS  = $(patsubst %.s,%.o,$(wildcard sh_src/*.s))
SHOBJS += $(patsubst %.c,%.o,$(wildcard sh_src/*.c))

# The baked sprite blob is ~660KB of cart ROM and is dead weight until
# the compose fast path reads it, so it is NOT in the default link —
# the wildcard above would otherwise pull it into every build,
# including the shipping rom.
SHOBJS := $(filter-out sh_src/sprbake_data.o sh_src/md_sprart_data.o,$(SHOBJS))
# `make CAT1INLINE=1` = LOOP 17 A/B arm for the MISSING FG cat-1 layer
# (the tall grass blades that draw OVER sprites; the arcade has them at
# the foot line, we show a flat strip). cat1+text are normally DEFERRED
# to the next window gap through a single-slot record (cat1_valid); this
# runs them inline at the band tail instead, the pre-LOOP-10 order.
# Costs the strobe win back — diagnosis, not a ship.
# `make DRQPROBE=1` = LOOP 17: DOES A SHORT DREQ PUSH REPORT A PARTIAL
# TCR0? Everything about sprite-push truncation (~18 lines of 68K,
# measured) rests on that one hardware fact, and MAME cannot answer it
# (LOOP 13: "MAME never showed it"). The MD pushes ONE RECORD SHORT (588
# words instead of 596, publishing 588) on every 16th sprite packet —
# record 63, which is always past the terminator, so nothing visual
# depends on it — and the master histograms what `landed` reads.
#   partial readable  -> the short pushes land 588 and validate
#   not readable      -> they read 0 and the master skips them (1 frame
#                        of stale sprites in 16, expected on this build)
# Counters at 0x28FBC, decoded by tools/drq_probe.py. NEVER SHIP.
# `make SPRTRUNC=1` = LOOP 17: push only the LIVE sprite records. The
# list is 64 records; the game fills a mean of 12.5 (max 21, measured).
# Under NT_WRAP every pushed word costs two 32X register accesses, and
# the DREQ push is 67 of the 68K's ~107-line handler mean — so the dead
# padding is the single most expensive thing the 68K does.
# Measured on identical scripted play: handler mean 83.8 -> 65.4,
# tail 63.3 -> 46.5. ~18 lines/vint of 68K time back.
#
# *** MAME CANNOT PIXEL-GATE THIS BUILD. *** Precisely this and nothing
# wider: MAME does not model partial DREQ landings, so every truncated
# packet reads landed==0 there and the master correctly skips it — MAME
# shows FROZEN SPRITES and the parity statics move. The build takes a
# DIFFERENT CODE PATH in MAME than on hardware, so a diff measures the
# emulator gap, not the rom. DRQPROBE settled it on ares (233 of 234
# short pushes read their true length; tools/drq_probe.py).
# MAME is still honest about everything else on this build — MD-side
# handler mean and the window/tail split rank fine — and the renderer
# itself pixel-gates normally if you build WITHOUT SPRTRUNC. Gate the
# PIXELS on Mike's ares pass, and keep gating the SHIPPING build in
# MAME as always. (See CLAUDE.md "What MAME is for now": the limit is
# per-flag and there are exactly two of them.)
# `make CUT30=1` = LOOP 17 item 4b, first cut: DROP THE IDLE BEAT.
# WIN_TWO runs k1, k2, then one idle vint the game keeps whole — three
# vints per cycle = a 20Hz display, which is the "slideshow" Mike keeps
# reporting and the reason the Zeus scale animation steps. Skipping the
# idle beat makes it k1,k2,k1,k2 = 2 vints/cycle = 30Hz, halving every
# animation step.
# This is deliberately UNCONDITIONAL rather than cutscene-gated: the
# question that has to be answered first is whether the master can
# sustain a 2-vint cycle AT ALL. If it can, gating it to cutscenes
# (scene state 0xFFF031) is the easy part; if it cannot, the falsifier
# is loud — flip/blit skips and V-gate rejects climb and bands ship
# stale. The cost lands on the 68K too: the game loses the whole idle
# vint it used to keep, so watch the handler mean as well as the eyes.
ifdef CUT30
MDCCFLAGS += -DCUT_30
endif
ifdef SPRTRUNC
SHCCFLAGS += -DSPR_TRUNC
MDCCFLAGS += -DSPR_TRUNC
endif
# `make BLITSKIP=1` = LOOP 18 job 1 (ATTEMPT 2): stop shipping the parts
# of the frame that are empty. In blit_half, OR the 8 longs of each 32px
# group (free — the blit already loads them to store them) and skip the
# 8 stores when the group is zero AND the TARGET BANK already holds zero
# there. Per-bank per-row 10-bit mask, 896B at 0x3A300.
# WHY IT PAYS: 80% of the blit is an FB-write bus-stall floor (LOOP 9;
# cached vs uncached measured 47.34 vs 47.46 us/row, and a DMAC blit was
# 1.77x SLOWER), so the only lever left is writing fewer bytes. ares
# measured 62.7% of groups entirely transparent after MDBGALL moved the
# background to the MD plane — an SDRAM read is ~5x cheaper than an FB
# write, so the break-even is ~25% skippable.
# ATTEMPT 1 CORRUPTED AND WAS REVERTED: it sniffed the bank per call as
# `MARS_VDP_FBCTL & MARS_VDP_FS`, and the k2 flip sits between the k1 and
# k2 blits with BOTH CPUs blitting around an edge the master owns. Now
# the master keeps the parity (fb_draw_par, toggled at its own FS write)
# and publishes it to the slave in blit-command bit 6. Nothing sniffs.
#
# GATE IT LIKE THIS — the two halves go to different tools:
#   PIXELS -> MAME, and MAME is BETTER at this than ares. Build without
#     SPRTRUNC so the transport is faithful, then diff gameplay frames
#     against the same build without BLITSKIP. Any difference is a bug;
#     that is how attempt 1 was caught in one run.
#   SPEED  -> ares ONLY. MAME charges the ~2.7us instruction issue and
#     not the ~47us/row stall, so it shows the cost of the test and none
#     of the saving (attempt 1 measured hmean 65.4 -> 66.1 there).
ifdef BLITSKIP
SHCCFLAGS += -DBLIT_SKIP
endif
# `make BLITSKIP=1 NOSKIP=1` — the CONTROL for the above. Identical code
# path, mask maintained, skip never taken. Any parity movement that
# survives this is NOT the skip: it is the extra instructions shifting
# MAME's SH-2 timing and the pipeline settling to a different phase at
# the anchor frame. Diagnostic only; never ship.
ifdef NOSKIP
SHCCFLAGS += -DBLIT_SKIP_NOSKIP
endif
# `make BLITSKIP=1 SKIPCOUNT=1` — skip-rate meter at 0x3A680, read by
# tools/blitskip_probe.lua. Pixel-identity is worthless without it: a
# mask that credits nothing is trivially correct and buys nothing.
ifdef SKIPCOUNT
SHCCFLAGS += -DBLIT_SKIP_COUNT
endif
# `make BLITSKIP=1 SKIPVERIFY=1` — THE correctness test for this flag.
# Reads every skipped group back from the framebuffer (uncached) and
# counts the times it was not actually zero. Immune to the thing that
# makes a two-build pixel diff useless here: any code added to the blit
# shifts MAME's SH-2 timing, so the text/palette layers settle to a
# different phase and frames differ for reasons that are not bugs.
# 8 uncached FB reads per skip. NEVER SHIP.
ifdef SKIPVERIFY
SHCCFLAGS += -DBLIT_SKIP_VERIFY
endif
# `make ... BLITSOLO=1 WINSPLIT=1` = LOOP 18: what IS the blit's per-row
# fixed cost? Removing 57% of its stores removed only 14% of its cost, so
# ~75% of the blit does not scale with bytes written. This probe silences
# the SLAVE's blit so the master's runs with no concurrent framebuffer
# traffic. Master ticks/row moving = the fixed cost is CPU-vs-CPU bus
# contention and the lever is scheduling; not moving = it is the loads or
# the per-row setup.
# *** HALF THE SCREEN WILL BE WRONG. Timing probe only, never ship,
# never judge feel on it. ***
ifdef BLITSOLO
SHCCFLAGS += -DBLIT_SOLO
endif
# `make ... BLITNOLOAD=1 WINSPLIT=1` = LOOP 18: is the blit READ-bound?
# Stores a value read once per row, so the row costs 80 stores + 1 load
# instead of 80 + 80. Run it WITHOUT BLITSKIP so it compares against the
# 34.2 ticks/row control directly. Picture is a flat smear per row;
# timing only, never ship.
ifdef BLITNOLOAD
SHCCFLAGS += -DBLIT_NOLOAD
endif
# `make ... SBUFCANARY=1` = LOOP 18 step 1, buying region-guard bytes.
# sbuf (336x240 = 80,640B) IS the 0x19000 region; only 88 bytes are
# spare and job 2 does not fit. 320x224 of sbuf is the screen and the
# rest is margin for fine scroll and off-edge sprite draw. This paints
# every margin byte with a per-row signature so a play pass says which
# margin rows/columns are ACTUALLY written; read back by
# tools/sbuf_canary.lua. Shrink to the measured extent, not a guess.
ifdef SBUFCANARY
SHCCFLAGS += -DSBUF_CANARY
endif
# `make ... SPRLINE=1` = can the MD VDP's SPRITE hardware draw these
# sprites, the way MDBGALL moved the background to its tile planes?
# The total count already fits (mean 12.5, max 21 live records vs the
# MD's 80); what decides it is the H40 PER-LINE budget of 20 sprites and
# 320 sprite-pixels. This counts both per scanline over real play, plus
# the sprites that could never go to MD hardware anyway (zoomed, gated,
# shadow). Read with tools/sprline_probe.lua. Measurement only.
ifdef SPRLINE
SHCCFLAGS += -DSPR_LINE_PROBE
endif
# `make BANDSHIFT=n` = LOOP 18: move n rows of every band from the
# MASTER to the SLAVE. Compose is split 112/112 and the blit 56/56, but
# the master alone also does the flip, page drain/restore, CRAM,
# build_maps, the shadow LUT, the sprite snapshot and the band queue —
# so it finishes late while the slave idles ~15,500 polls/cycle (STALE:
# the slave idle meter reads 0.0 polls/vint on the current line,
# LOOP29 118 -- it is saturated). The
# slave's echo PUSHES bands and the master's progress DRAINS them, so
# the queue sheds ~1 band/cycle and the dropped band is always the
# MASTER's rows: rows 72 and 144, exactly where Mike's tears are.
# Uniform speedups cannot fix this (BLITSKIP+DIRTYROW+SPRBAKE moved the
# handler mean 91.3 -> 86.5 and left drops/cycle at 0.95-0.97).
# Watch `deferrals` per cycle, not the handler mean.
ifdef BANDSHIFT
SHCCFLAGS += -DBAND_SHIFT=$(BANDSHIFT)
endif
# RG2SHIFT=n: separate shift for the R2/master boundary (184+n; 40 =
# the master's rg2 slice vanishes entirely). 2026-08-29: Mike's red-
# boxed frames put the persistent band stripes EXACTLY at the master
# half-slice rows (68-71, 140-143); BANDSHIFT=36 deletes those two by
# geometry and RG2SHIFT=40 deletes the third (220-223).
ifdef RG2SHIFT
SHCCFLAGS += -DBAND_SHIFT_RG2=$(RG2SHIFT)
endif
# `make BLITSHIFT=N` (R60 only) — N rows of the in-window blit move
# from the master to the slave. MEASURED DEAD 2026-08-24 (pass 8):
# sweep 24/48/72 left the 68K handler mean UNMOVED (106.2 ->
# 105.5-108.6) and made flips WORSE at 24/48 (isr 3231 -> 2882/3000).
# THE BLIT IS FB-BUS-BOUND, NOT CPU-BOUND: both SH-2s share the one
# FB write path (the ~47us/row stall floor), so moving rows only
# relabels which CPU waits. The master's 777-vs-366 cyc/row premium
# is its cap_drain sharing the same bus, not CPU inefficiency. The
# blit cost yields only to WRITING FEWER FB WORDS (dirty-ROW blit:
# skip rows unchanged since that bank's last blit) or to moving
# cap_drain's FB reads out of the window (the write-log ring the
# copy_pages comment already names). Do not re-sweep this knob.
ifdef BLITSHIFT
SHCCFLAGS += -DBLIT_SHIFT=$(BLITSHIFT)
endif
# `make SPRLATE=1` = LOOP 19: kill the 1-3 frame sprite colour flashes.
# spr_pair[] is rebuilt only by build_maps, whose only caller passes
# `bpar ^ 1` — the map for this parity was built a cycle ago from the
# PREVIOUS sprite list, so a set appearing now reads 0xFF and draws with
# base 15, the shadow ramp. One cycle is three vints, which is the flash.
# This claims only the missed sets, from pairs nothing owns, at k1 before
# apply_cram (which paints out of spr_pair, so they land the same
# window). Counters at 0x3A7D8, read with tools/sprlate_probe.lua:
# [3] is the one that matters and must reach 0.
ifdef SPRLATE
SHCCFLAGS += -DSPR_LATE
endif
# `make GRPRELOC=1` = LOOP 19 job 1. The sprite pair reservation above
# `bound` is not enforced: live TILE singles squat in it, and a pair
# needs both its groups, so 9 reserved pairs yield only 6 usable while
# the worst cycle needs 8. Offline packing cannot help (palpack.py: 18 of
# 19 sprite sets use all 14 pens), so the fix is to free what is already
# reserved. RELOCATE a live squatter to a free group below bound instead
# of evicting it — eviction is what produced the yellow sprite-ghost;
# relocation preserves the colour because tile_grp/text_grp are rebuilt
# from grp_key every cycle. No free low group -> leave it, i.e. exactly
# today's behaviour.
ifdef GRPRELOC
SHCCFLAGS += -DGRP_RELOC
endif
# `make TILEDEDUP=1` = LOOP 19 job 1, the one that actually has slack.
# The tile allocator claims a group per COLOUR SET, but measured over 899
# cycles the live tile colours hold only TEN distinct 8-entry palettes at
# the worst cycle (exact dedup 3.31x) while 19-20 groups are spent. That
# over-spend IS the sprite shortage: tiles ~20 + sprites 16 = ~36 groups
# against 32. If a group already holds a byte-identical palette, point
# the colour at it and claim nothing - no remap, no approximation, and
# unlike the shared_tile fallback the colours are genuinely the same.
ifdef TILEDEDUP
SHCCFLAGS += -DTILE_DEDUP
endif
# `make PAIRHOLD=1` = LOOP 19, the last blocker. With the tile squatters
# cleared by TILEDEDUP, 6 of the 9 reserved sprite pairs are still held
# by sets that are NOT on screen: a departed set keeps its pair until
# age > 90, which is 4.5 seconds at 20Hz. With 9 pairs and 9 sets
# wanting one that starves somebody no matter what else is tuned. The
# long hold is worth keeping when pairs are spare (a set that flickers
# off for a frame should not lose its pen and get recoloured on return),
# so make it demand-aware: 90 while there is slack, 2 once demand
# reaches the reserve.
ifdef PAIRHOLD
SHCCFLAGS += -DPAIR_HOLD
endif
# `make PRHOLD=<n>` — pair-hold sweep: cycles a departed sprite set
# keeps its CRAM pair before release (default 90 in m_main.c).
# `make SHIMBURN=N` = 2026-09-06 sensitivity probe: burn ~N beam lines of
# 68K time in the vint shim (after the push). Decides whether a shim diet
# can give the game its frame back. NEVER SHIP.
# `make PCSAMP=1` = 2026-09-06 68K PC sampler: H-int at 4 lines per frame,
# the three early ones log the interrupted PC (ring 0xFFA200). NEVER SHIP.
ifdef PCSAMP
MDASFLAGS += -Wa,--defsym,PC_SAMP=1
MDCCFLAGS += -DPC_SAMP
endif
# `make POSTLATE=1` = 2026-09-06: post+push after the game's IRQ4 (no FM
# gate spins in the game's vint upload; the flip becomes the body's
# deferred flip). Measured for the 60Hz demo.
# `make FMLATE=1` = 2026-09-06: FM dropped for the push, raised only for the
# master's flip/restore and blit/publish, ack awaited in the shim; game code
# never runs with FM up. The 60Hz lever.
# `make NOBLIT=1` = 2026-09-06 CEILING probe: the blit ships zero rows.
# Picture is stale by design; the question is only what the game's speed
# does if the FB write cost goes to zero. NEVER SHIP.
# `make WRITECOST=1` = 2026-09-06: time 1024 68K word writes/reads to work
# RAM vs to FB staging, at boot. Decides whether the game's own pass is
# paying for FB-resident staging. NEVER SHIP.
ifdef WRITECOST
MDCCFLAGS += -DWRITECOST_PROBE
endif
ifdef NOBLIT
SHCCFLAGS += -DFBROWS_PROBE -DNOBLIT_PROBE
endif
# `make ... ROWSHIP=1` = SESSION 7: rows shipped by blit_half, counted
# UNCACHED at 0x38F00 ([0] y<112, [1] y>=112, [2] calls). Read with
# tools/gameplay_speed.py --extra 0x38F00:16. NEVER SHIP.
ifdef ROWSHIP
SHCCFLAGS += -DROWSHIP_PROBE
endif
# `make ... BLITHASH=1` = SESSION 7 LEVER B: per-bank content skip in
# blit_half (hash the sbuf row, skip the FB write when this bank's last
# ship of the row hashed the same). DIAG[10] = rows skipped. Add
# BLITHASHVERIFY=1 to read every skipped row back from the FB and count
# mismatches in DIAG[11] (DIAG[12] = skips checked) — NEVER SHIP that.
ifdef BLITHASH
SHCCFLAGS += -DBLIT_HASH
endif
ifdef BLITHASHVERIFY
SHCCFLAGS += -DBLIT_HASH_VERIFY
endif
ifdef FBPROBE
SHCCFLAGS += -DFBROWS_PROBE -DFBDMA_PROBE
endif
ifdef FMLATE
MDCCFLAGS += -DFM_LATE
SHCCFLAGS += -DFM_LATE
endif
ifdef POSTLATE
MDCCFLAGS += -DPOST_LATE
MDASFLAGS += -Wa,--defsym,POST_LATE=1
endif
ifdef SHIMNOPUSH
MDCCFLAGS += -DSHIM_NOPUSH
endif
ifdef SHIMBURN
MDCCFLAGS += -DSHIM_BURN=$(SHIMBURN)
endif
ifdef PRHOLD
SHCCFLAGS += -DPR_HOLD_TICKS=$(PRHOLD)
endif
# `make FBSPRPROBE=1` = LOOP 20 step 1: can the sprite list be read in
# place at FB_SPR instead of being pushed over DREQ (48.6 of the 68K's
# 64-line handler)? The in-place read was retired on a 40/64-torn-records
# measurement that PREDATES unpair and Presentation 2.0 - re-measure, do
# not inherit. Content-compares every landed record against FB_SPR's raw
# slots (the push is ordered, so positional compare is invalid). Build
# WITHOUT SPRTRUNC and WITHOUT SPRLATE (counter slot). NEVER SHIP.
ifdef FBSPRPROBE
SHCCFLAGS += -DFBSPR_PROBE
endif
# `make FBSPR=1` = LOOP 20 step 2: the game's sprite upload goes BACK to
# FB staging (patch_game remap 0x85E000) and the SH-2 fills SPR_SNAP by
# reading FB_SPR in place at k1. The DREQ push is left completely intact
# and its sprite payload ignored — so this build gates CORRECTNESS only;
# the 68K saves nothing yet. The speed harvest (dropping the sprite
# packet from the push) is step 3, taken only after this build is
# pixel-clean in MAME and on ares. The write-discard hazard that forced
# the 0xFF7000 reroute is extinct: FM has one raise site and the 68K
# spins inside the whole FM=1 span (see patch_game.py). game_body.bin
# depends on FLAGSTAMP so flag flips rebuild the patched game.
ifdef FBSPR
SHCCFLAGS += -DFB_SPR_READ
MDCCFLAGS += -DFB_SPR_READ
endif
# `make FBTEXT=1` = LOOP 20: text RAM in place, same move as FBSPR. The
# game's text writes land at FB 0x85F000 (the slot the dead FB_PAL
# quarters copy vacated); the SH-2 captures the 2048-word region to
# TEXT_U each k1 and restores it into the fresh bank at the k2 flip
# (text is written SPARSELY, unlike the sprite list, so restore is
# mandatory). Kills the DREQ text chunks and the regs/rowscroll prefix
# apply; the push itself is left intact (payload ignored) until the
# ares verdict, exactly like FBSPR's staging. Requires FBSPR's audit
# result (game code never runs with FM=1) - no new hazard.
ifdef FBTEXT
SHCCFLAGS += -DFB_TEXT_READ
MDCCFLAGS += -DFB_TEXT_READ
endif
# `make ... MDHSCR=1` = LOOP-DECOMPILE 25. The game's two HORIZONTAL scroll
# stores (0x2AD2 foreground, 0x2AEE background) are rewritten to thunks that
# do the original store AND write the MD hscroll table directly, converting
# with MD = S16 - 192 (measured, LOOP-DECOMPILE 24: same sign, and 192 is the
# 24-column visible-window origin). Row scroll is provably unused, so a
# whole-plane value is the whole story (LOOP-DECOMPILE 23).
# ADDITIVE: the shim still writes sc[3]/sc[7] from its own packet, so this
# build should be PIXEL-IDENTICAL to the baseline. That is the gate.
ifdef MDHSCR
MDCCFLAGS += -DMD_HSCROLL_DIRECT
endif
# `make ... MDSPRPROBE=1` = LOOP-DECOMPILE 27. COST PROBE, RENDERS WRONG.
# Blanks the sprite-record copy inside the game's own upload loop while
# keeping the list geometry, to price the copy off the game's missed-frame
# counter. Never ship; never pixel-gate.
# `make ... FLICKFUSE=1` = LOOP 21 flicker fusion. The arcade renders the
# Zeus/orb apparition's translucency TEMPORALLY: the record's presence in
# the sprite list is duty-modulated (measured fade-in 1/8 -> 1/4 -> 1/3,
# hold 2-in-3, fade-out; tools/zeus_list_probe.lua) and the OFF frames
# REMOVE the record outright. Below 60Hz the dither aliases (20Hz strobe,
# 30Hz phase-lock). The master tracks zoomed records across windows and a
# toggling one is drawn EVERY window from a held copy through an ordered
# dither whose coverage is the observed on-ratio — synthesized alpha.
# Requires FBSPR (tracks the in-place snapshot at k1).
ifdef FLICKFUSE
SHCCFLAGS += -DFLICK_FUSE
endif
# `make ... PAL32=1` = LOOP 22 step 1. The k2 push carries a 256-word
# palette region PAIR on 98.9% of cycles (pal_rate: regions 0/1 dirty
# 97%/85% — the colour cycle) to move a measured mean of 9.7 changed
# CRAM words/frame spread over 1.63 32-word blocks (pal_spread, arcade).
# PAL32 refines the dirty granularity 128 -> 32 words (64-bit bitmap at
# 0xFFBA00, thunks regenerated by patch_game) and ships up to 4 dirty
# BLOCKS per push: 82 prefix + 4 ids + K*32 + 2 tail = 88+32K words vs
# 340 — ~200 words/cycle off the 68K tail. Same ship-twice retry and
# round-robin discipline as the pair channel. Requires FBTEXT (packet
# has no text chunks) + WIN_TWO. MAME stays colour-blind (short pushes
# land 0 there — the SPRTRUNC class); colour gates on ares, the win
# gates on TAILPROBE.
ifdef PAL32
SHCCFLAGS += -DPAL32
MDCCFLAGS += -DPAL32
endif
# `make ... PKTSLIM=1` = LOOP 22: drop the dead 80-word prefix and the
# k1 junk record. Under FBSPR+FBTEXT+PAL32 the prefix carries nothing:
# regs ride the text capture, sprites ride FB staging, text chunks are
# gone. Packets become k1 = 4 words (bitmap+tag+tail), k2 = 4 or 8+32K.
# ~168 junk words/cycle off the 68K FIFO feed. Every length/offset
# compiles from md_src/packet_fmt.h (see the whitelist lesson there).
ifdef PKTSLIM
SHCCFLAGS += -DPKT_SLIM
MDCCFLAGS += -DPKT_SLIM
endif
# `make ... FMGATE=1` = LOOP 23: the 68K stops paying for the SH-2
# window. The vint chain becomes an rte trampoline (game vint upload
# runs first at FM=0, part B raises FM and posts AFTER it, nobody
# spins); the game's MAIN-LOOP FB subsystems gate on FM at their ~22
# entry points (generated thunks; derivation tools/fmgate_derive.py);
# the SH-2 drops FM itself at the ack. MAME (FM-lenient) must render
# IDENTICALLY to the ungated build — any pixel diff is protocol
# breakage. The write-discard hazard gates on ares only.
ifdef FMGATE
SHCCFLAGS += -DFM_GATE
MDCCFLAGS += -DFM_GATE
MDASFLAGS += -Wa,--defsym,FM_GATE=1
endif
# `make ... TXTWRAM=1` = LOOP 27 q4: the text writers at the top of the
# game's pass (credit line, health bar; table in tools/game_<GAME>.py)
# stage in the WRAM text mirror instead of the framebuffer; their gates
# become marks and the shim copies each dirty footprint into FB text
# staging at FM=0 (before the raise). Removes the ~60-100-line FM spin
# tools/frame_timeline.py shows at the top of the pass. Probe until
# Mike's pass; gates: speed ladder, read census, attract, mdstatic.
ifdef TXTWRAM
MDCCFLAGS += -DTXT_WRAM
endif
# `make ... SPRMDFREE=1` = LOOP 27 q4 play-pass follow-up: sprite sets
# whose records are ALL rendered by the MD VDP (MDSPR) release their 32X
# CRAM pair — build_maps' used-set scan and the late claim's live mask
# skip MD-claimed records. Census at every failed late claim: ~1 of the
# 14 pairs was held this way, 5 by tile-class groups, ~7 by live sets.
ifdef SPRMDFREE
SHCCFLAGS += -DSPR_MD_FREE
endif
# `make ... SLVPAIR=1` = LOOP 27 q4 play-pass fix: the slave reads the
# sprite pair table (0x3F800) UNCACHED in the compose. It never purges
# its cache, so the master's late pair claims were invisible to it for
# the cycle and the set drew in the shadow ramp (the red silhouettes /
# missing actors of Mike's first 60 Hz pass; census: 100% of ramp draws
# on the slave). One uncached byte per record.
ifdef SLVPAIR
SHCCFLAGS += -DSLV_PAIR_UNCACHED
endif
# `make ... LATESTEAL0=1` = LOOP 27 entry 6 option 1: the late pair claim
# steals a pair whose owner is absent from THIS snapshot at age 0 (not
# >= 1) once 9+ sprite sets are live — the map builder's own demand
# rule, applied to the path every new set takes at one cycle per vint.
ifdef LATESTEAL0
SHCCFLAGS += -DLATE_STEAL0
endif
# `make ... LATEKEEP=1` = LOOP 27 entry 6: the map rebuild keeps pairs
# claimed late this cycle (pr_age 0) instead of wiping them — the
# chunked build completing after the claim was re-ramping claimed sets.
ifdef LATEKEEP
SHCCFLAGS += -DLATE_KEEP
endif
# `make ... DRAWADOPT=1` = LOOP 27 entry 6: an unmapped record adopts its
# set's pair from pr_key (the ownership truth) at draw time, uncached.
ifdef DRAWADOPT
SHCCFLAGS += -DDRAW_ADOPT
endif
# `make ... ARMGATE=1` = LOOP 27 entry 7: torn landings were pushes into an
# UNARMED DMA (the V-ISR bailed stale and never consumed the announce; the
# 68K posted after the ack). SH-2: consume + arm at the ack when an announce
# is pending. 68K: push only after the 0xA001 arm echo (bounded; no echo =
# no packet this vint, counted at 0xFFB0CE).
ifdef ARMGATE
SHCCFLAGS += -DARM_GATE
MDCCFLAGS += -DARM_GATE
endif
# `make ... TEXTCAPSLAVE=1` = LOOP 20: the k2 text capture runs on the
# SLAVE, in parallel with the master's truth drain, instead of adding
# ~10 lines to the master's window. Master posts SYNC[4]=0x4000 at k2
# entry, joins on SYNC[6] after the drain, and falls back to capturing
# itself (late but still pre-flip) if the slave never answers.
ifdef TEXTCAPSLAVE
SHCCFLAGS += -DTEXTCAP_SLAVE
endif
# `make ... DIRTYROW=1` = LOOP 18 job 2: let the blit skip a row WITHOUT
# READING IT. Four probes said the blit is throughput-bound and partial
# removal returns sub-linearly (57% of stores gone kept 86% of the cost),
# so only whole rows pay. Under MD_BG sbuf is explicitly zeroed every row
# every cycle, which is why a "was it written" flag is useless and the
# fact tracked here is "is this row all zeros" — asserted only by the
# clear, revoked by every draw.
# `DIRTYROWVERIFY=1` adds stage A's check: nothing is skipped, but every
# row the marks claim is zero gets read back and disagreements counted.
# Get that to 0 — and falsify it by removing one RL_MARK — before
# enabling any skipping. Read with tools/dirtyrow_probe.lua.
ifdef DIRTYROW
SHCCFLAGS += -DDIRTY_ROW
endif
ifdef DIRTYROWVERIFY
SHCCFLAGS += -DDIRTY_ROW -DDIRTY_ROW_VERIFY
endif
# `make ... PHASECENSUS=1` = BOSSFIGHT pipelining arc, datum #1: split the
# per-generation wall into echo / mtask / close-lag / ship / flip (master
# FRT ticks, scratch 0x28E40 — the audited-free span). Probe-only; the
# arithmetic is ROM-resident so .ramtext pays only the stamps. Read with
# tools/nat_score.py (headless dump) or tools/state_health.py (savestate).
# `make ... MDSPR_WHY=1` = LOOP29 119: WHY does the MD-VDP sprite offload
# claim only 1.0 record per generation (cap 20, MD hardware 80)? Counts
# each rejection reason in the claim loop: [0] zoomed [1] pp!=2 [3] X<=0
# [4] NO BAKED KEY [5] no anchor [6] palette mismatch [7] caps
# [8] claimed [9] live records examined. Counters live in .bss (the
# 0x28Fxx scratch is crowded and this repo has numbered its collisions to
# #15); read the `mdspr_why` symbol out of rom/s16.lst. PROBE ONLY.
# `make ... BGPACK2=1` = LOOP29 121: pack the MD background into TWO
# palette lines instead of three, freeing MD CRAM line 3 for a second
# sprite palette. Measured free on level-1: lines 1-3 carry 40 CRAM
# entries but only 30 DISTINCT colours (9-bit decode), 29-30 at every
# sampled frame, so 2 lines x 15 pens holds it exactly. NOT proven for
# attract (m_main.c:288 claims a worst window of 36 distinct). Gate:
# background colour count and a play pass.
# `make ... MTASKWHY=1` = LOOP29 123: split the master's per-generation
# tail. PHASECENSUS puts mtask at 2.73 of a 2.74-vint wall and the ship
# line's tail is maps-ONLY (NAT_ALL_SLAVE=1). Counts FRT ticks actually
# spent inside the build_maps drain, chunks, visits, gate skips and
# completions -- separating "2.73 vints of work" from "a short drain
# spread across three vints". Counters in .bss; read the mt_* symbols
# from rom/s16.lst. PROBE ONLY.
ifdef MTASKWHY
SHCCFLAGS += -DMTASK_WHY
endif
ifdef BGPACK2
SHCCFLAGS += -DBG_PACK2
endif
# `make ... MDSPRTOP=1` = LOOP29 132: choose the MD sprite palette line by
# RECORD COUNT instead of the baked per-scene anchor. The scene table pins
# the normal scene to set 0x09 (3 records at f3000) while set 0 carries 7,
# so line 0 is spent on the wrong palette. Runs the existing leader
# election in every scene and switches on a 2-record margin held 5 passes
# instead of only when the incumbent owns nothing. Gate: MDSPR_WHY census
# (claims/gen) then a play pass for colour flicker.
ifdef MDSPRTOP
SHCCFLAGS += -DMDSPR_TOP
endif
ifdef MDSPR_WHY
SHCCFLAGS += -DMDSPR_WHY
endif
ifdef PHASECENSUS
SHCCFLAGS += -DPHASE_CENSUS
endif
# PHASEGEN=n: which generation the one-gen event trace records (default 600)
ifdef PHASEGEN
SHCCFLAGS += -DST_ARM_GEN=$(PHASEGEN)
endif
# `make ... LANDPARK=1` = pipelining arc E1: the slave parks only while
# the DREQ landing is in flight (announce -> landing-done) instead of the
# whole FM span. SYNC[12] is cleared at landing-done, not at pickup.
# Trade to measure: gens closing before W1 vs rejects/bad1/hdlr.
ifdef LANDPARK
SHCCFLAGS += -DLAND_PARK
endif
# `make ... PACE30=1` = launch a generation only every 2nd window: a steady
# 30fps instead of the irregular 35 (1-or-2-vint periods). Eye A/B.
ifdef PACE30
SHCCFLAGS += -DPACE30
endif
# `make ... LAUNCHEARLY=1` = pipelining arc step 1: launch the generation
# before apply_cram/publish/ack instead of after (launch offset 0.46v ->
# less). Byte-identical picture; the compose gets the window tail back.
ifdef LAUNCHEARLY
SHCCFLAGS += -DLAUNCH_EARLY
endif
# `make ... LAUNCHEARLY=1 BLITCHASE=1` = pipelining arc step 2: post the
# slave's blit half before the pre-ack work, launch, THEN blit the
# master's half; the slave composes behind a row fence (SYNC[14]).
ifdef BLITCHASE
SHCCFLAGS += -DBLIT_CHASE
endif
# `make ... PENMATCH=1` = C1 done right, part 1: FB tile groups painted
# with the MD's own quantised pens (nearest-pen substitution included)
# so a tile split between the FB and plane A is one colour.
ifdef PENMATCH
SHCCFLAGS += -DPEN_MATCH
endif
# `make ... XDEF=NAME` = one extra -DNAME for probe variants (SHEXTRA is
# the optimisation-flag slot; overriding it drops -O2 and bloats RAMCODE).
ifdef XDEF
SHCCFLAGS += -D$(XDEF)
endif
# `make ... CAT1MD=1` = pipelining arc C1 step 1: FG cat-1 cells are emitted
# on MD plane A with the priority bit instead of blanked. Pixel-neutral by
# construction while the FB keeps composing cat1; step 2 restricts the FB
# cat1 pass to SH-2 sprite rows/strips (the 0.44v/gen slave lever).
ifdef CAT1MD
SHCCFLAGS += -DCAT1_MD
endif
# `make ... ARTTAIL=1` = tile art rides along in every cell chunk (the
# scroll-in pop-in: art used to wait for a tile chunk, phase 0 of the
# 9-window rotation). Both CPUs.
ifdef ARTTAIL
SHCCFLAGS += -DART_TAIL
MDCCFLAGS += -DART_TAIL
endif
# `make ... HSSHIP=1` = MD plane scroll lands on the same vint as the FB
# flip (pending per parity at launch, live at ship, patched at copy).
ifdef HSSHIP
SHCCFLAGS += -DHS_SHIP
endif
# `make ... NEARMERGE=1` = near-pen merge: two pixels of a set within 2/31
# per channel share one MD pen (flattens the sky band's dither like the
# arcade). OFF by default: merging at assignment time is wrong for scenes
# entered through a fade (everything is near black then) — Mike's
# transformation chevrons went flat, the level load-in kept the wrong
# palette. Needs an unmerge-on-drift before it can ship (docs/design/BOSSFIGHT.md).
ifdef NEARMERGE
SHCCFLAGS += -DNEAR_MERGE
endif
ifdef DRQPROBE
SHCCFLAGS += -DDRQ_PROBE
MDCCFLAGS += -DDRQ_PROBE
endif
ifdef CAT1INLINE
SHCCFLAGS += -DNOCAT1DEFER=1
endif
# ROWDEFER=1 = re-arm the row-defer ship gate (historical). It killed
# the purple band on 2026-08-25, but the BG backstop then fixed the
# purple at the ROOT, and the gate's cost surfaced in Mike's corpus
# 2026-08-26: ~26 deferred rows/frame ship TWO-frame-stale — the thin
# displaced strips he boxed in frame 800. With the gate off: purple
# still 0 (backstop holds), BAD1 best-ever 160. Superseded; off by
# default.
ifndef ROWDEFER
SHCCFLAGS += -DNO_ROW_DEFER
endif
# TILECLASS=1 = LOOP19 endgame: static fade-stable tile palette classes
# (sh_src/tile_classes.h, generated by tools/palpack_tiles.py --emit from
# the harvest corpus). Tiles collapse to 11 fixed groups, sprites get a
# fixed 8-pair zone — the S11 over-subscription is gone by construction.
# Corpus covers title + round 1; regenerate the table before later rounds.
ifdef TILECLASS
SHCCFLAGS += -DTILE_CLASS
endif
# SHADCAP=n = max shadow-record height that gets TRUE darkening (taller
# renders silhouette). 48 was the old budget cap; 255 measured clean
# 2026-08-26 (cadence 1.049, bad1 145 best-ever) WITH the stale-LUT
# fallback and the through-stipple — the three ship together.
SHADCAP ?= 255
SHCCFLAGS += -DSHAD_CAP=$(SHADCAP)
# CHAINMETER=1 = probe: chain span (SPRLATE[4]/[5], master FRT ticks
# R0-launch -> close-observed) + slave launch-latency ([8]/[9], clear-
# wait iterations ~16 ticks each). Costs region-guard bytes; NEVER SHIP.
ifdef CHAINMETER
SHCCFLAGS += -DCHAIN_METER
endif
# QCHAIN=0/1/2 = queued chain: master posts R1/R2 to SYNC[10]/[11] at
# launch; the slave pulls them itself. 0 = pull immediately, 1 = pull
# only while FM=1 (68K quietly polling COMM), 2 = only while FM=0.
# Experiment 2026-08-26; the winner may become default.
ifdef QCHAIN
SHCCFLAGS += -DQUEUED_CHAIN -DQPULL=$(QCHAIN)
endif
# ROWGEN=1 = write-tracked row skip (2026-08-26): rows provably
# unchanged (no sprite span, no scroll/text/page/cut delta) skip their
# clear AND their blit. ROWSTALE measured 96% cycle-stable rows; this
# takes the write-knowable subset at zero read cost. Block at 0x39750
# (collides with DIRTYROWVERIFY's probe capture — probe-only overlap).
ifdef ROWGEN
SHCCFLAGS += -DROW_GEN
endif
# ROWGENVERIFY=1 = SESSION 7: every row ROWGEN skips is read back from
# the FB uncached and compared with sbuf; DIAG[11] mismatches over
# DIAG[12] checks. The proof for ROWGEN v2. NEVER SHIP.
ifdef ROWGENVERIFY
SHCCFLAGS += -DROW_GEN_VERIFY
endif
# WAITPROBE=1 = SESSION 7: split the master's ship window — its own
# half, the wait for the slave half, post-to-start latency (0x38E24..).
# NEVER SHIP.
ifdef WAITPROBE
SHCCFLAGS += -DWAIT_PROBE
endif
# PALROTOROFF=1 = SESSION 7 CALIBRATION: the 68K ships no palette
# blocks (rotor/compare/selection skipped; colours freeze). The speed
# it buys is the ceiling of moving the palette compare off the 68K.
# NEVER SHIP.
# `make ... PALNOCMP=1` = the 68K stops COMPARING palette blocks and
# ships every marked block raw. Through the FB transport a redundant
# 32-word block costs ~1.6 scanlines; proving it redundant costs ~5.
# Discovery moves to the SH-2 side; the 68K only pumps.
# `make ... PALNOCMP=1` = the 68K does no palette DISCOVERY: no compare,
# no shadow, no mask walk. It ships each marked block raw and the SH-2
# is the side that knows what changed. (v1 kept the shadow copy and
# measured 74.1%; that was the copy, not the idea.)
ifdef PALNOCMP
MDCCFLAGS += -DPAL_NOCMP
endif
# PALSTREAK=N / PALBACKOFF=M tune the rotor's visit rate: a palette block
# that compared equal N consecutive visits is then visited 1 vint in
# (M+1).  Either flag turns the diet ON (PAL_DIET); it is OFF in the
# shipping build.  UPDATED 2026-09-08 (LOOP28 85-88): the streak counter
# was never written, so before that date BOTH flags were inert for every
# N >= 1 and any measurement of them read the baseline.  With the counter
# implemented, a 26-point sweep on the FBXPORT line found no setting that
# beats the baseline outside the metric's own trajectory spread — read
# LOOP28 88 before sweeping these again.  The ceiling (PALROTOROFF, no
# visits at all, colours freeze) re-measures at 94.7% against 82.3%, not
# the 86.1/82.0 recorded here from the DREQ era.  PALNOCMP (visit but
# ship raw instead of comparing) measured 74.1% and was reverted.
ifdef PALSTREAK
MDCCFLAGS += -DPAL_DIET -DPAL_STREAK_N=$(PALSTREAK)
endif
ifdef PALBACKOFF
MDCCFLAGS += -DPAL_DIET -DPAL_BACKOFF_M=$(PALBACKOFF)
endif
ifdef PALROTOROFF
MDCCFLAGS += -DPALROTOR_OFF
endif
# LAYOUTPROBE=1 = LOOP28 88 CONTROL. Adds 64 bytes of unreferenced .data
# inside r60_push and changes nothing else. Any speed difference it
# produces is the level-1 ladder's sensitivity to code layout, not to a
# change in behaviour. Measured 85.3% vs 82.4% on the FBXPORT line.
ifdef LAYOUTPROBE
MDCCFLAGS += -DLAYOUT_PROBE
endif
# FBXSTAGE=1 = LOOP28 89, THE STAGING SPLIT. Needs FBXPORT. Separates the
# packet BUILD from the framebuffer WRITE: the build runs after the post,
# into 1872 bytes of WRAM (free space audited in LOOP28 84), where FM=1
# does not apply and it overlaps the master's blit; a straight copy moves
# it into the FB at the tail, in the FM=0 window the master's ack opens.
# Costs one vint of packet latency, which the harvest already tolerates.
# The point is that V-at-post stops paying for the build: FBXPORT alone
# posts at V=43 and flips 0.3 times a second.
ifdef FBXSTAGE
MDCCFLAGS += -DFBX_STAGE
endif
# FBXBOTH=1 = LOOP28 91. Needs FBXSTAGE. The packet lives IN the
# framebuffer and the framebuffer swaps: with flips running, the master's
# lift reads an already-seen sequence on one window per flip, because the
# bank it reads is not the bank the blast wrote. This writes the packet
# twice — at the tail and again before the next post, with a flip
# possibly between — carrying the SAME sequence, so a master that already
# lifted it skips it.
ifdef FBXBOTH
MDCCFLAGS += -DFBX_BOTH
endif
# PGSKIPPKT=1 = LOOP29 137. The page truth (cap_page/restore_pages)
# excludes the two packet regions that live inside game tile-RAM pages:
# the R60 packet + publish word in page 0 (0x12000-0x1283F) and MD-plane
# packet B in page 12 (0x1E800-0x1EFFF). Measured on the dblfast line in
# steady play: pg_watch = 0x1001 -- exactly those two pages, watched
# forever because the pipeline's own writes dirty them every vint, 23
# lines of capture and ~20 of restore per flip for bytes that are not
# game truth. The game writes zero tilemap pages in steady level-1 play.
ifdef PGSKIPPKT
SHCCFLAGS += -DPG_SKIP_PKT
endif
# PGKEEPB=1 = LOOP29 139 hardware probe: with PGSKIPPKT, still mirror MD-plane
# packet B (page 12 second half) across banks through the page truth.
ifdef PGKEEPB
SHCCFLAGS += -DPG_KEEP_B
endif
# FBXISRLIFT=1 = LOOP29 137. fbx_lift() runs at the top of flip_span,
# BEFORE the FS write, so it reads the bank the 68K's tail blast wrote
# (no flip can have intervened). That makes FBXBOTH's second blast
# before the post unnecessary -- the 12 lines of 68K FB writes between
# the consumes and the raise. The body's lift stays as a fallback for
# vints where the ISR did not reach flip_span.
ifdef FBXISRLIFT
SHCCFLAGS += -DFBX_ISRLIFT
endif
# FBXPEND=1 = LOOP29 137. Needs FBXSTAGE, replaces FBXBOTH. The tail
# blast runs ONLY when FM is already 0 (a 68K FB write at FM=1 is
# dropped by ares bus-external.cpp:45 and by the FPGA IF.sv:946 alike --
# that is why FBXBOTH's second blast was load-bearing and why ve, built
# without it, landed torn packets and a black screen). When FM is still
# up at the tail the packet stays staged and is blasted once before the
# NEXT post, at FM=0 by construction. On vints whose tail blast landed the
# post keeps its early line; only the vints after a long master window
# pay the 12 lines. Pair with FBXISRLIFT so the lift reads the bank the
# blast wrote.
ifdef FBXPEND
MDCCFLAGS += -DFBX_PEND
endif
# GAMEGATE=1 = LOOP29 141, THE PIVOT. The game's frame release (IRQ4 at
# 0x2AB8, LOOP-DECOMPILE 22) is patched to consult a shim token at WRAM
# 0xFFA0F5: the main loop advances one frame per token, the token is set
# on every flip echo (F102) or after GAMEGATE_MAXWAIT vints without one,
# and a held vint takes the game's own short path uncounted. The game
# runs to our presentation clock instead of racing the beam.
ifdef GAMEGATE
MDCCFLAGS += -DGAME_GATE -DGAMEGATE_MAXWAIT=$(if $(GAMEGATEWAIT),$(GAMEGATEWAIT),4)
endif
# PALSTAMP=1 = SESSION 7: extra HV stamps inside the 68K packet build
# (0xFFA0C4 after the dirty count, 0xFFA0C0 before the rotor loop,
# 0xFFA0C2 after it) next to the push-autopsy stamps 0xFFA0B4..BE.
ifdef PALSTAMP
MDCCFLAGS += -DPAL_STAMP2
endif
# MDCONSUMEOFF=1 = SESSION 7 CALIBRATION: the 68K issues no MD-plane,
# SAT or art DMAs after vint 900 (planes freeze). The handler lines it
# gives back size the MD-upload item of the 68K vint. NEVER SHIP.
ifdef MDCONSUMEOFF
MDCCFLAGS += -DMDCONSUME_OFF
endif
# FBBENCH=1 = direct-draw economics microbench at boot (FRT-timed row
# fills + masked sprite blits, sbuf vs FB). Results at 0x3A790[0..4].
# NEVER SHIP.
ifdef FBBENCH
SHCCFLAGS += -DFB_BENCH
endif
# DIRECTFB=1 = REBUILD stage 1 (2026-08-26, the FBBENCH verdict): every
# compose writer's dst row moves from sbuf to the FB BACK BANK directly
# (cached window; FB writes cost the same as SDRAM writes, 0.99x). The
# clears become FB-row long-fills (the FB IGNORES ZERO BYTE writes — a
# byte clear would be a no-op), blit_half's row loop is inert (the
# machinery stays wired; it dies whole in stage 2), and the k2 flip is
# gated on "this interval actually composed" so an AUTO-30 launch skip
# HOLDS the frame instead of stepping the display backward two frames.
# WHICH BANK needs no plumbing: 0x04000000 maps the DRAW bank by
# hardware and the whole compose interval sits between k2 flips.
ifdef DIRECTFB
SHCCFLAGS += -DDIRECT_FB
endif
# TXTCLASS=1 = static TEXT class (2026-08-29, the green-HUD-digits
# fix): text set 0 — the always-on HUD text, one palette state across
# the whole arcade census — pins to GROUP 0 (the 32nd group, reserved
# since MDBGALL for the through bit and never armed; entries 1-7
# verified virgin). The HUD can no longer lose its palette to the
# 2-deep dynamic zone and draw green. Scene text still rides the
# dynamics. Candidate for the canonical line after Mike's pass.
ifdef TXTCLASS
SHCCFLAGS += -DTEXT_CLASS
endif
# MDSPRSPIKE=1 = P3 M0 spike (docs/design/P3.md): two hardcoded MD hardware
# sprites (SAT prio 1 and 0) + test art + line-0 colors at first
# vint. Proves SAT@0xF000 alignment, sprite-vs-plane priority both
# ways, and FB-vs-sprite mixing in one screenshot. NEVER SHIP.
ifdef MDSPRSPIKE
MDCCFLAGS += -DMDSPR_SPIKE
endif
# MDSPR=1 = P3 (docs/design/P3.md): mob-class sprite records render as MD VDP
# hardware sprites. Art blob (tools/bake_mdspr.py) pinned at cart
# 0x2F0000, 68K uploads it to VRAM 0x8000 at boot; SH-2 claim pass
# marks eligible records (native, pp==2, key+palette resident, caps)
# and builds the SAT packet; the 68K DMAs it beside the existing
# consumes. Claimed records never touch compose or the FB.
ifdef MDSPR
SHCCFLAGS += -DMD_SPR
MDCCFLAGS += -DMDSPR
SHOBJS += sh_src/md_sprart_data.o
# MDSPRDOUBLE=1: bisect aid — claim+SAT+DMA all run but compose ALSO
# draws claimed records (double render). Separates MD-side defects
# from compose-skip side effects. NEVER SHIP.
ifdef MDSPRDOUBLE
SHCCFLAGS += -DMDSPR_DOUBLE
endif
endif
# COHERENT30: DEAD BY MIKE'S ORDER 2026-08-26 ("why on earth are you
# regressing to 30hz") — 30Hz stepping has no place in this
# architecture. Flag deleted; the 60Hz path is the MD-VDP sprite
# offload arc (P3). Do not rebuild this.
# SELFCHAIN=1 = experiment arm: slave self-chains all three compose
# bands from one command. Four variants measured 2026-08-25 (raw,
# DREQ-park, window-park, COMM0-yield, FM-park): ALL bracket rejects
# 10-14% vs the master-chain's 2.0 — the slave's frame-body idle is
# LOAD-BEARING bus quiet, and parking enough to protect it refunds the
# latency win (skipped back to 47%+). DO NOT re-try a fifth park shape;
# the next real lever is shrinking compose itself (§11 pack, MD text).
# Default = master-relaunched chain with the bounded hold.
ifdef SELFCHAIN
SHCCFLAGS += -DSELF_CHAIN_EXPERIMENT
else
SHCCFLAGS += -DNO_SELF_CHAIN
endif
# NATIVE=1 = the whole-frame pipeline (branch native1, docs/design/NATIVE.md,
# 2026-08-31 — Mike's "gates should be for an entire screen update").
# ONE generation in flight and it is the WHOLE frame: launch latches
# regs+records once, the slave composes all three bands from one
# self-chain command with cat1+text inline, the master tail (text
# halves + build_maps) runs as poll-gap chunks, the blit ships ONLY
# closed generations and the ISR flips ONLY freshly-blitted banks.
# Band queue / chain relaunch / ROW_DEFER / cat1 deferral compile
# out. 60Hz when a generation closes in a vint; whole-frame-coherent
# 30Hz when it does not — never a band, never a mixed frame.
# NOT the SELFCHAIN experiment (which kept per-band ships racing the
# compose); see docs/design/NATIVE.md's ledger notes before citing that negative.
ifdef NATIVE
ifndef R60
$(error NATIVE is the R60 whole-frame scheduler - needs R60=1)
endif
SHCCFLAGS += -DNATIVE_FRAME -DNOCAT1DEFER=1 -DNO_ROW_DEFER
endif
# PALSTATIC=1 = per-scene static palettes v1 (docs/design/PALSTATIC.md): baked
# PAL_SH images (tools/palscene_bake.py -> sh_src/pal_scenes.h) load
# WHOLE on scene detect + all tile/text generations bump, collapsing
# the scene-cut palette trickle (Mike's blue-white gravestones) to
# detect latency; a shadow-LUT rush shrinks the silhouette window.
# Tables are level-1 (3 scenes); unknown scenes ride the dynamic
# path unchanged. Candidate for the canonical line after Mike's pass.
ifdef PALSTATIC
SHCCFLAGS += -DPAL_STATIC
endif
# GLOWMASK=1 = PALSTATIC v2 MEASUREMENT PROBE (battery only — the
# glow FREEZES on screen; never hand this to ares eyes as a build).
# The 68K rotor drops the glow blocks' (4,5) dirty marks in steady
# state (ndirty<=24), so their per-vint compare+ship vanishes.
# Heal/force-raw and storm/fade vints bypass the mask. Measures the
# real 68K prize of baking the glow SH-2-side before any animator
# is written (census 2026-09-01: 17 of the 18 live glow words sit
# in blocks 4-5; the 0x36 blink in block 1 stays unmasked).
ifdef GLOWMASK
MDCCFLAGS += -DGLOW_MASK
endif
# PALGLOW=1 = PALSTATIC v2 GLOW BAKE, the real thing: the 68K mask
# (as GLOWMASK) PLUS the SH-2 animator (tools/glow_bake.py ->
# sh_src/glow_tab.h) playing the captured glow rules at 60Hz vint =
# the arcade's own rate (the game's updates are logic-clocked and
# stutter at our 73% 68K). Landing-carried glow blocks pause the
# animator (fade storms, heals); it re-seeds from live PAL_SH.
# Needs PALSTATIC=1 (scene loads must pause/re-seed it) and
# TILECLASS=1 (include site).
ifdef PALGLOW
ifndef PALSTATIC
$(error PALGLOW rides the PALSTATIC scene machinery - add PALSTATIC=1)
endif
MDCCFLAGS += -DGLOW_MASK
SHCCFLAGS += -DGLOW_ANIM
endif
# MDSTATIC=1 = STATIC-SCENE arc (docs/design/STATIC-SCENE.md): per-scene
# static MD pen tables (tools/mdpen_bake.py -> sh_src/pal_scenes_md.h)
# installed at the PALSTATIC scene load, the table's sets pinned against
# eviction and drift-free, and a VRAM slot-map flush at display-off so
# the old scene's tiles never compete with the new scene's. Fixes the
# random-boot flat sky / missing clouds (HANDOFF-SESSION8 4b). Needs
# PALSTATIC=1 (the load site) and MD_BG (the allocator).
ifdef MDSTATIC
ifdef PALSTATIC
SHCCFLAGS += -DMD_STATIC
else ifneq ($(filter ship ship-us ship-jp,$(MAKECMDGOALS)),)
# `make ship-us MDSTATIC=1`: the outer make has no PALSTATIC yet; the
# sub-make carries SHIP_US (PALSTATIC=1) plus this flag and defines it.
else
$(error MDSTATIC installs at the PALSTATIC scene load - add PALSTATIC=1)
endif
endif
ifdef SPRBAKE
SHOBJS += sh_src/sprbake_data.o
# -DSPR_BAKE is what job 3's compose fast path compiles against, and it
# is ALSO what puts SPRBAKE into the flag stamp. A link-only flag is
# invisible to .build_flags, so `make` after `make SPRBAKE=1` said
# "nothing to be done" and shipped the probe rom back as the baseline —
# the exact trap .build_flags exists to prevent.
SHCCFLAGS += -DSPR_BAKE
endif

# FLAG STAMP — objects must depend on the FLAG SET, not just on sources.
# Without this, `make IDLETOKEN=1` right after a plain `make` reuses every
# object whose .c file did not change, so half the build silently keeps
# the old semantics. LOOP 11a lost a full measurement cycle to exactly
# that: the SH-2 published idle tokens while md_main.o, untouched, had no
# poll-and-skip in it at all — and the run scored a perfect 24.26 because
# it WAS the baseline. Any flag build measured before this existed should
# be re-measured before it is believed.
FLAGSTAMP := .build_flags
$(shell f='$(MDCCFLAGS) $(SHCCFLAGS)'; \
        [ "$$(cat $(FLAGSTAMP) 2>/dev/null)" = "$$f" ] || printf '%s' "$$f" > $(FLAGSTAMP))
# (the dependency itself is declared below `all:` — an explicit rule above
# it would make md_src/font.o the default goal)

.PHONY: all release debug clean

all: release

# ---- SHIPPING BUILDS, both titles (2026-09-05, Mike: "parity builds,
# a Japanese rom and a US rom as two build outputs") ----
# SHIP_US is the canonical line Mike accepted on ares as rom/s16_nocat1.32x
# (docs/handoff/HANDOFF-SESSION4.md): CAT1MD stays OFF. Shipped with CAT1MD=1 for a
# few hours on 2026-09-07 after the A/B crops passed Mike's eye; his play
# pass then failed it ("grass feels shimmery", a second palette on the
# ground band through the beast transformation) — the two-renderer
# class docs/design/BOSSFIGHT.md records. Screenshots cannot show it; only play can.
# rom/s16_cat1md_0907.32x (+ .bs1) keeps that build for reference.
# MDSTATIC=1 joined the line 2026-09-07 late ("ship the fixes"): the
# STATIC-SCENE arc (docs/design/STATIC-SCENE.md), Mike's pass "stage 1
# presentation now nearly perfect" + the title regression fixed and
# arcade-graded (HANDOFF-SESSION8.md 4c). Needs discover/palscenes/
# palharv_*.txt + *.bs1 for `make tables` to regenerate pal_scenes_md.h. SHIP_JP is the kit baseline
# for a title whose census-derived tables do not exist yet (TOOLKIT.md):
# the canon minus SPRBAKE TILECLASS TXTCLASS MDSPR PALSTATIC PALGLOW
# PENMATCH (see SHIP_JP below). Objects are shared between titles, so
# `make ship` rebuilds each in turn — SEQUENTIALLY (.NOTPARALLEL: a -j
# run would interleave two flag sets on one object tree); the roms land
# side by side:
#   rom/s16.32x            US  (altbeast)
#   rom/s16_altbeastj.32x  JP  (altbeastj)
# SHIPBLITSHIFT: the ship line's slave/master blit split (session 7:
# `make ship-us SHIPBLITSHIFT=N` — a bare BLITSHIFT=N on the command line
# loses to the literal in this list inside the sub-make).
SHIPBLITSHIFT ?= 24
SHIP_COMMON = MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1 R60=1 \
              CUTBLANK=1 BANDSHIFT=36 RG2SHIFT=40 BLITSKIP=1 DIRTYROW=1 \
              BLITSHIFT=$(SHIPBLITSHIFT) SPRLATE=1 PRHOLD=6 ROWDEFER=1 PALDELTA=1 NATIVE=1 \
              LAUNCHEARLY=1 BLITCHASE=1 EDGE42=1 HSSHIP=1
SHIP_US = $(SHIP_COMMON) SPRBAKE=1 TILECLASS=1 TXTCLASS=1 MDSPR=1 PALSTATIC=1 PALGLOW=1 PENMATCH=1 MDSTATIC=1
# JP ships on the SAME line: the census-derived tables are keyed on art
# the two sets share byte-for-byte (SH-2 folds the JP code/bank layout
# onto the US images), and the reduced "kit baseline" subset is an
# untested build (confetti in MAME like everything else — TOOLKIT.md).
SHIP_JP = $(SHIP_US)
.PHONY: ship ship-us ship-jp
.NOTPARALLEL: ship
ship:
	$(MAKE) ship-us
	$(MAKE) ship-jp
ship-us:
	$(MAKE) GAME=altbeast $(SHIP_US)
ship-jp:
	$(MAKE) GAME=altbeastj $(SHIP_JP)

$(MDOBJS) $(SHOBJS): $(FLAGSTAMP)
# pal_thunks.h is generated by the game_body rule; without this edge a
# flag flip can compile md_main.o against the stale header (the #error
# guard in md_main.c is the backstop that caught it)
md_src/md_main.o: md_src/pal_thunks.h md_src/fmgate_tab.h md_src/game_irq.h
# MDHSCR generates one more header in the same patch_game run; without this
# edge md_main.o compiles before it exists (LOOP-DECOMPILE 25).
ifdef MDHSCR
md_src/md_main.o: md_src/hscr_thunks.h
md_src/hscr_thunks.h: md_src/game_body.bin
endif

# build stamp header: git short hash as u32 -> DIAG[18] at boot, so every
# savestate self-identifies its commit. Regenerated when HEAD changes.
BUILD_HASH := $(shell git rev-parse --short=8 HEAD 2>/dev/null || echo 0)
# git rev-parse --git-path: works in linked worktrees too, where .git
# is a FILE and .git/HEAD does not exist (every worktree agent hit this
# 2026-08-25 — the canonical build failed out of the box).
GITDIR := $(shell git rev-parse --git-dir 2>/dev/null || echo .git)
sh_src/buildstamp.h: $(GITDIR)/HEAD $(wildcard $(GITDIR)/refs/heads/*)
	@echo "#define BUILD_HASH32 0x$(BUILD_HASH)u" > $@
sh_src/m_main.o: sh_src/buildstamp.h


release: MDEXTRA = -O2 -fomit-frame-pointer -flto -fuse-linker-plugin
release: SHEXTRA = -O2 -fomit-frame-pointer -flto -fuse-linker-plugin
release: $(MDTARGET).bin $(MDTARGET).lst $(TARGET).32x $(TARGET).lst

debug: MDEXTRA = -g -Og -DDEBUG
debug: SHEXTRA = -g -Og -DDEBUG
debug: $(MDTARGET).bin $(MDTARGET).lst $(TARGET).32x $(TARGET).lst

$(MDTARGET).lst: $(MDTARGET).elf
	$(MDNM) --plugin=$(MDPLUGIN) -n $< > $@

$(TARGET).lst: $(TARGET).elf
	$(SHNM) --plugin=$(SHPLUGIN) -n $< > $@
	@end=$$(grep -E ' _end$$' $@ | head -1 | cut -c1-8); \
	if [ $$((16#$$end)) -gt $$((16#06019000)) ]; then \
	  echo "FATAL: SH-2 .bss end 0x$$end crosses SDRAM region base 0x06019000"; \
	  echo "(tilemap shadow and friends live there — grow the region map instead)"; \
	  rm -f $@; exit 1; \
	fi

$(MDTARGET).bin: $(MDTARGET).elf
	@$(MDOBJC) -O binary $< $@

$(MDTARGET).elf: $(MDOBJS) | $(ROMDIR)
	$(MDCC) $(MDLDFLAGS) $^ -o $@ $(MDLIBS)

md_src/%.o: md_src/%.s
	@echo "MDAS $<"
	@$(MDCC) $(MDASFLAGS) -c $< -o $@

md_src/%.o: md_src/%.c
	@echo "MDCC $<"
	@$(MDCC) $(MDCCFLAGS) $(MDEXTRA) $(MDINCS) -MMD -MP -c $< -o $@

# .lst prerequisite = the .bss region guard runs BEFORE the rom is
# emitted: a guard failure must never leave a fresh .32x behind (it
# once shipped a build whose .bss overlapped the tilemap shadow).
$(TARGET).32x: $(TARGET).elf $(TARGET).lst
	@# CART GUARD (LOOP 17). .gamehigh is PINNED at 0x02300000 and the
	@# rom image ends at 0x02340000; everything else loads upward from
	@# 0x02000000, so the free space the sprite bake spends is the gap
	@# between them — 743KB measured, not the 4MB no-mapper ceiling.
	@# ld does not police the overlap for us, and a silent overrun
	@# corrupts the 68K high rom, which fails as a GAME bug a long way
	@# from its cause.
	@# --pad-to 4MB with 0xFF: .sprbake (0x340000+) is the last section
	@# now and objcopy stops at its end; the stamp's tail-pad check
	@# needs 0xFF free space at the very end (as .gamehigh's own FF
	@# padding used to provide when it was the top tenant).
	@$(SHOBJC) -O binary --gap-fill=0xff --pad-to=0x400000 $< temp.32x
	@hi=$$($(SHOBJD) -h $< | awk '/^ *[0-9]+ \./ { n=$$2; s=$$3; l=$$5; \
	    getline f; if (f ~ /LOAD/ && n != ".gamehigh") print l, s }' \
	    | while read l s; do echo $$((16#$$l + 16#$$s)); done \
	    | sort -n | tail -1); \
	if [ -n "$$hi" ] && [ $$hi -gt $$((16#400000)) ]; then \
	  printf 'FATAL: cart image ends 0x%08x, past the 4MB cart at 0x00400000\n' $$hi; \
	  echo "(.sprbake at 0x340000 is the top tenant since 2026-09-01;"; \
	  echo " shrink the bake set or the boss frame corpus)"; \
	  rm -f temp.32x $@; exit 1; \
	fi
	@dd if=temp.32x of=$@ bs=8192 conv=sync 2>/dev/null
	@rm -f temp.32x
	@# BUILD STAMP at file offset 0x3C0 (unused header pad): git hash +
	@# epoch + PRESSURE flag. Every savestate self-identifies its build
	@# (tools/build_id.py) — no more provenance arguments.
	@python3 tools/build_id.py stamp $@ $(if $(R60),R60,$(if $(K2FREE),K2FREE,$(if $(VISRFLIP),VISRFLIP,$(if $(FMGATE),FMGATE,$(if $(PKTSLIM),PKTSLIM,$(if $(PAL32),PAL32,$(if $(FLICKFUSE),FLICKFUSE,$(if $(FBSPR),FBSPR,$(if $(BANDSHIFT),SHIFT$(BANDSHIFT),$(if $(SPRBAKE),SPRBAKE,$(if $(DIRTYROW),DIRTYROW,$(if $(BLITNOLOAD),BLITNOLOAD,$(if $(BLITSOLO),BLITSOLO,$(if $(PRESSURE),PRESSURE,$(if $(SPROBE),SPROBE,$(if $(TAILPROBE),TAILPROBE,$(if $(WINSPLIT),WINSPLIT,$(if $(DRQPROBE),DRQPROBE,$(if $(MDPAYOFF),MDPAYOFF,$(if $(BLITSKIP),BLITSKIP,$(if $(SPRTRUNC),$(if $(CUT30),TRUNC_CUT30,SPRTRUNC),$(if $(CUT30),CUT30,normal))))))))))))))))))))))
	@python3 tools/build_id.py show $@

$(TARGET).elf: $(SHOBJS) | $(ROMDIR)
	$(SHCC) $(SHLDFLAGS) $^ -o $@ $(SHLIBS)

$(ROMDIR):
	@mkdir -p $(ROMDIR)

# Patched arcade game body + boot RAM copy, .incbin'd by mars_start.s
md_src/md_start.o: md_src/game_irq.h    # GAME_IRQ4 comes from the patcher
md_src/game_body.bin md_src/boot_copy.bin md_src/game_high.bin md_src/pal_thunks.h md_src/fmgate_tab.h md_src/game_irq.h &: $(GAMEROMS)/prog68k.bin tools/patch_game.py tools/game_$(GAME).py $(FLAGSTAMP)
	@GAME=$(GAME) MDHSCR=$(MDHSCR) MDSPRPROBE=$(MDSPRPROBE) FBSPR=$(FBSPR) FBTEXT=$(FBTEXT) PAL32=$(PAL32) FMGATE=$(FMGATE) K2FREE=$(K2FREE) R60=$(R60) TXTWRAM=$(TXTWRAM) FBXPEND=$(FBXPEND) GAMEGATE=$(GAMEGATE) TXTMASK=$(TEXTCAPMASK) python3 tools/patch_game.py
sh_src/game_body.bin: md_src/game_body.bin
	@cp $< $@
sh_src/game_high.bin: md_src/game_high.bin
	@cp $< $@
sh_src/game_high_data.o: sh_src/game_high.bin
sh_src/boot_copy.bin: md_src/boot_copy.bin
	@cp $< $@
sh_src/sega_blob.bin: md_src/sega_blob.bin
	@cp $< $@

# 68K boot blob is .incbin'd by mars_start.s (resolved via -Ish_src)
sh_src/md_start.bin: $(MDTARGET).bin
	@cp $< $@
sh_src/mars_start.o: sh_src/md_start.bin sh_src/game_body.bin sh_src/boot_copy.bin sh_src/sega_blob.bin

# Arcade tile data: planar ROMs -> chunky 8bpp, .incbin'd by tiles_data.s
sh_src/tiles.bin: tools/gen_tiles.py $(GAMEROMS)/prog68k.bin $(FLAGSTAMP)
	@GAME=$(GAME) python3 tools/gen_tiles.py
sh_src/tiles_data.o: sh_src/tiles.bin

# Arcade sprite data: 16-bit-BE interleave, .incbin'd by sprites_data.s
sh_src/sprites.bin: tools/gen_sprites.py $(GAMEROMS)/prog68k.bin $(FLAGSTAMP)
	@GAME=$(GAME) python3 tools/gen_sprites.py
sh_src/sprites_data.o: sh_src/sprites.bin

# LOOP 17 sprite bake: pre-decoded frames + hash index, .incbin'd by
# sprbake_data.s. bake_sprites.py re-decodes every baked frame through
# a port of the LIVE algorithm and compares it against a replay of the
# baked format at four x positions — so the accuracy gate fails the
# BUILD, before a rom exists to be judged by eye.
# (NOT a `&:` grouped target: this is GNU Make 3.81, which has no such
# thing — it parses `&` as one more TARGET, so a second `&:` rule warns
# "overriding commands for target `&`" and the two rules fight. The .h
# is a side effect of the .bin recipe, expressed the 3.81 way.)
sh_src/sprbake.bin: tools/bake_sprites.py sh_src/sprites.bin \
                $(wildcard discover/*.csv)
	@python3 tools/bake_sprites.py
sh_src/sprbake.h: sh_src/sprbake.bin
	@:
sh_src/sprbake_data.o: sh_src/sprbake.bin

# P3: MD sprite art blob + both indices regenerate from the census
sh_src/md_sprart.bin sh_src/md_sprart.h md_src/md_sprart_info.h: \
		tools/bake_mdspr.py discover/play.csv
	python3 tools/bake_mdspr.py
sh_src/md_sprart_data.o: sh_src/md_sprart.bin

sh_src/%.o: sh_src/%.s
	@echo "SHAS $<"
	@$(SHAS) $(SHASFLAGS) $< -o $@

sh_src/%.o: sh_src/%.c
	@echo "SHCC $<"
	@$(SHCC) $(SHCCFLAGS) $(SHEXTRA) $(SHINCS) -MMD -MP -c $< -o $@

-include $(MDOBJS:.o=.d)
-include $(SHOBJS:.o=.d)

# Public-repo hygiene (2026-09-02): everything derived from the ROM set
# is ignored; a fresh clone regenerates it here. pal_scenes.h needs the
# PAL_SH dumps under discover/palscenes/ (see README "Today").
.PHONY: tables
tables: sh_src/tiles.bin sh_src/sprites.bin sh_src/sprbake.bin \
        sh_src/md_sprart.bin
	@if ls discover/palscenes/*.palsh >/dev/null 2>&1; then \
	    python3 tools/palscene_bake.py; \
	else echo "tables: no discover/palscenes/*.palsh - harvest three PAL_SH dumps first (README)"; fi
	@if ls discover/palscenes/palharv_*.txt >/dev/null 2>&1; then \
	    python3 tools/mdpen_bake.py $(foreach h,$(wildcard discover/palscenes/palharv_*.txt),--harvest $(h)) $(foreach b,$(wildcard discover/palscenes/*.bs1),--state $(b)); \
	else echo "tables: no discover/palscenes/palharv_*.txt - run tools/palharvest_tiles_ares.py first (STATIC-SCENE.md)"; fi

clean:
	rm -f $(MDOBJS) $(SHOBJS) $(MDOBJS:.o=.d) $(SHOBJS:.o=.d)
	rm -f $(MDTARGET).bin $(MDTARGET).elf $(MDTARGET).lst
	rm -f $(TARGET).32x $(TARGET).elf $(TARGET).lst
	rm -f sh_src/md_start.bin sh_src/tiles.bin sh_src/sprites.bin
	rm -f $(FLAGSTAMP)

# ---------------------------------------------------------------------------
# sndtest: the sound-engine lab ROM (docs/sound/SOUND.md P0). Same toolchain, its own
# minimal skeleton (32x-builder lineage, sndtest/): the engine gets built and
# proven here, then the SAME engine sources link into the shipping rom (P5).
# Deliberately outside the shipping build: separate object lists, no
# .build_flags coupling, no LTO (small code; keeps libmem/thread-jump
# hazards out of the lab). `make sndtest` -> rom/sndtest.32x.
SNDTARGET   = $(ROMDIR)/sndtest
SNDMDTARGET = $(ROMDIR)/sndtest_md
SNDMDOBJS   = sndtest/md/md_start.o sndtest/md/font.o sndtest/md/md_main.o
SNDSHOBJS   = sndtest/sh/mars_start.o sndtest/sh/m_main.o sndtest/sh/s_main.o \
              sndtest/sh/hw.o sndtest/sh/libmem.o sndtest/sh/sound.o \
              sndtest/sh/speech_bank.o

# speech bank: WAVs ripped from the MAME oracle tap (tools/snd_tap.lua ->
# tools/upd7759_decode.py -> sndtest/speech/), baked to IMA ADPCM C.
sndtest/sh/speech_bank.c: tools/speech_bake.py sndtest/speech/manifest.json
	python3 tools/speech_bake.py sndtest/speech sndtest/sh/speech_bank.c
sndtest/sh/sound.o sndtest/sh/m_main.o: sndtest/sh/speech_bank.c

# P3 Z80 streaming music player: wla-z80/wlalink assemble, then package
# for the 68K with the mailbox contract parsed out of the asm.
sndtest/z80/player.bin: sndtest/z80/player.asm sndtest/z80/player.linkfile
	cd sndtest/z80 && wla-z80 -o player.o player.asm && wlalink -d player.linkfile player.bin
sndtest/md/z80_player.h: tools/z80_pack.py sndtest/z80/player.bin
	python3 tools/z80_pack.py sndtest/z80/player.asm sndtest/z80/player.bin $@
sndtest/md/test_track.h: tools/mus_testgen.py
	python3 tools/mus_testgen.py $@
# sndmap_data.h (tools/soundmap_build.py over a tools/cmd_sweep.lua
# log) is generated from transient MAME tap captures and committed —
# see docs/sound/SOUND.md P4.
sndtest/md/md_main.o: sndtest/md/z80_player.h sndtest/md/test_track.h \
                      sndtest/md/sndmap_data.h
SNDMDASFLAGS = -x assembler-with-cpp -Isndtest/md -m68000 -Wa,--register-prefix-optional
SNDSHASFLAGS = -Isndtest/sh --small
SNDEXTRA     = -O2 -fomit-frame-pointer

.PHONY: sndtest
sndtest: $(SNDTARGET).32x

sndtest/md/%.o: sndtest/md/%.s
	@echo "MDAS $<"
	@$(MDCC) $(SNDMDASFLAGS) -c $< -o $@

sndtest/md/%.o: sndtest/md/%.c
	@echo "MDCC $<"
	@$(MDCC) $(MDCCFLAGS) $(SNDEXTRA) $(MDINCS) -MMD -MP -c $< -o $@

$(SNDMDTARGET).elf: $(SNDMDOBJS) | $(ROMDIR)
	$(MDCC) -T sndtest/md/md.ld -nostdlib $^ -o $@ $(MDLIBS)

$(SNDMDTARGET).bin: $(SNDMDTARGET).elf
	@$(MDOBJC) -O binary $< $@

sndtest/sh/md_start.bin: $(SNDMDTARGET).bin
	@cp $< $@
sndtest/sh/mars_start.o: sndtest/sh/md_start.bin

sndtest/sh/%.o: sndtest/sh/%.s
	@echo "SHAS $<"
	@$(SHAS) $(SNDSHASFLAGS) $< -o $@

sndtest/sh/%.o: sndtest/sh/%.c
	@echo "SHCC $<"
	@$(SHCC) $(SHCCFLAGS) $(SNDEXTRA) $(SHINCS) -MMD -MP -c $< -o $@

$(SNDTARGET).elf: $(SNDSHOBJS) | $(ROMDIR)
	$(SHCC) -T sndtest/sh/mars.ld -nostdlib $^ -o $@ $(SHLIBS)

$(SNDTARGET).32x: $(SNDTARGET).elf
	@$(SHOBJC) -O binary $< sndtest_temp.32x
	@dd if=sndtest_temp.32x of=$@ bs=8192 conv=sync 2>/dev/null
	@rm -f sndtest_temp.32x
	@echo "sndtest rom: $@"

-include $(SNDMDOBJS:.o=.d)
-include $(SNDSHOBJS:.o=.d)

sndtest-clean:
	rm -f $(SNDMDOBJS) $(SNDSHOBJS) $(SNDMDOBJS:.o=.d) $(SNDSHOBJS:.o=.d)
	rm -f $(SNDMDTARGET).bin $(SNDMDTARGET).elf $(SNDTARGET).32x $(SNDTARGET).elf
	rm -f sndtest/sh/md_start.bin
