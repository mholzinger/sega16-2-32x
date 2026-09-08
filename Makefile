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
# flip span. Forced along (census-derived, see LOOP24.md K2FREE):
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
# `make ... R60=1` = THE REBUILD (REBUILD.md P1/P2): one vint = one
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
# STRIKE S1 (PIPELINE.md): TRIED AND REVERTED 2026-08-22, three
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
SHCCFLAGS += -DR60 -DK2_FREE -DVISR_FLIP -DSNAP_ONE -DWIN_TWO -DCUT_30 -DTEXTCAP_SLAVE
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
# able to ship by accident. LOOP.md negatives 20 and 21.)
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
# call BLITBURN got. LOOP.md negative 23.)
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
# so it finishes late while the slave idles ~15,500 polls/cycle. The
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
# palette. Needs an unmerge-on-drift before it can ship (BOSSFIGHT.md).
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
ifdef PALROTOROFF
MDCCFLAGS += -DPALROTOR_OFF
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
# MDSPRSPIKE=1 = P3 M0 spike (P3.md): two hardcoded MD hardware
# sprites (SAT prio 1 and 0) + test art + line-0 colors at first
# vint. Proves SAT@0xF000 alignment, sprite-vs-plane priority both
# ways, and FB-vs-sprite mixing in one screenshot. NEVER SHIP.
ifdef MDSPRSPIKE
MDCCFLAGS += -DMDSPR_SPIKE
endif
# MDSPR=1 = P3 (P3.md): mob-class sprite records render as MD VDP
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
# NATIVE=1 = the whole-frame pipeline (branch native1, NATIVE.md,
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
# compose); see NATIVE.md's ledger notes before citing that negative.
ifdef NATIVE
ifndef R60
$(error NATIVE is the R60 whole-frame scheduler - needs R60=1)
endif
SHCCFLAGS += -DNATIVE_FRAME -DNOCAT1DEFER=1 -DNO_ROW_DEFER
endif
# PALSTATIC=1 = per-scene static palettes v1 (PALSTATIC.md): baked
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
# (HANDOFF-SESSION4.md): CAT1MD stays OFF. SHIP_JP is the kit baseline
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
SHIP_US = $(SHIP_COMMON) SPRBAKE=1 TILECLASS=1 TXTCLASS=1 MDSPR=1 PALSTATIC=1 PALGLOW=1 PENMATCH=1
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
	@GAME=$(GAME) FBSPR=$(FBSPR) FBTEXT=$(FBTEXT) PAL32=$(PAL32) FMGATE=$(FMGATE) K2FREE=$(K2FREE) R60=$(R60) python3 tools/patch_game.py
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

clean:
	rm -f $(MDOBJS) $(SHOBJS) $(MDOBJS:.o=.d) $(SHOBJS:.o=.d)
	rm -f $(MDTARGET).bin $(MDTARGET).elf $(MDTARGET).lst
	rm -f $(TARGET).32x $(TARGET).elf $(TARGET).lst
	rm -f sh_src/md_start.bin sh_src/tiles.bin sh_src/sprites.bin
	rm -f $(FLAGSTAMP)

# ---------------------------------------------------------------------------
# sndtest: the sound-engine lab ROM (SOUND.md P0). Same toolchain, its own
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
# see SOUND.md P4.
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
