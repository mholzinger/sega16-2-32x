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
# `make SPANPROBE=1` = LOOP 9: pickup-V histogram ([34..41]) + blit-span
# histogram ([42..49]) + late restores split by pickup half ([50]/[51]).
# The mean span already fits vblank; this asks what the TAIL looks like
# and whether a late pickup is what makes it. NEVER SHIP.
ifdef SPANPROBE
SHCCFLAGS += -DSPAN_PROBE
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
# (PIPE2 is now the DEFAULT — it won Mike's play pass and is folded in.
# See slave_concurrent_k: compose a band two windows before shipping it.)
# (BLITBAL retired in LOOP 9 — the even thirds LOST Mike's play pass on
# the seams they cost. Retired rather than left here to mislead, the same
# call BLITBURN got. docs/log/LOOP.md negative 23.)
# (SPINPROBE retired in LOOP 8 with the COMM stream it capped. It did its
# job: N=0 moved the reject band 57.1 -> 39.4% on its own, which is what
# identified the ack-spin as the elastic sink and set this whole arc off.)
MDASFLAGS  = -x assembler-with-cpp -Imd_src -m68000 -Wa,--register-prefix-optional
# L2DIR: WHICH ARCADE CAPTURE THE GATE IS SCORED AGAINST.
#
# The three .portdata blobs (seed.bin, deltas.bin, io.bin) are selected by the
# ASSEMBLER INCLUDE PATH -- GNU as searches -I for .incbin -- so a second capture
# needs no edit to sh_src/port_l2_*.s. docs/proof/l2m is the original mid-gameplay
# window (frames 9000-9030). docs/proof/l2attract is the ATTRACT window captured
# 2026-10-03 across arcade frames 1460-1491, which spans the state 0x10 -> 0x14
# transition at frame 1481 -- the one the cold-booted port never makes.
L2DIR ?= docs/proof/l2m
# RELEASE (2026-10-29): the captures are arcade work RAM, so they are never committed. The
# shipping rom does not read them (no literal of _l2_before/_l2_frames/_l2_io in sw344's
# code) and .portdata/.portcode sit at fixed addresses, so a tree without them links three
# EMPTY blobs and every code byte is unchanged.
ifeq ($(wildcard $(L2DIR)/seed.bin),)
L2DIR := build/l2none
L2NONE := build/l2none/seed.bin
endif
SHASFLAGS  = -Ish_src -I$(L2DIR) --small
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
# `make ... BOOTSHSTAGE=1 BOOTBANK3HALT=1` = 68K reads the game image via bank 3, white/red, halt.
ifdef BOOTBANK3HALT
MDCCFLAGS += -DBOOT_BANK3HALT
endif
# `make ... BOOTVPULSE=1` = the 68K vint handler alternates 32X CRAM 0 (red/blue once the game runs).
ifdef BOOTVPULSE
MDCCFLAGS += -DBOOT_VPULSE
endif
# `make ... BOOTSHSTAGE=1 BOOTFBDMAHALT=1` = VDP DMA from the 32X framebuffer: white ok / red bad, halt.
ifdef BOOTFBDMAHALT
MDCCFLAGS += -DBOOT_FBDMAHALT
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
# BOOTTILEVER=1: LOOP29 231. TILE_VERIFY on both CPUs: the 68K consume
# reads every tile record's 16 VRAM words back after its DMA and keeps
# counters at WRAM 0xFFA1E0-0xFFA1F0 (checked, VRAM-zero, VRAM!=source,
# source-zero, FS-changed, slot-out-of-range, SH-2 word, zero index/cnt);
# the SH-2 side (m_main.c TV_BITS) rides packet word 1 bits 8-12/14. The
# value instrument paints whatever md_main.c's BOOT_TILEVER branch
# currently encodes (read the comment there: the layout changed per
# probe, vi76-vi85). Proved the transport intact on the FPGA; the
# zero-record counts are confounded by the level's blank tiles.
ifdef BOOTTILEVER
MDCCFLAGS += -DBOOT_VALUE -DBOOT_TILEVER -DTILE_VERIFY
SHCCFLAGS += -DTILE_VERIFY
endif
# CARTDMAPROBE=1 = 2026-09-17. THE GATE on the architectural shift: can
# the MD VDP DMA straight out of CART ROM into VRAM while we hold RV=1?
# Every vint it DMAs 16 words from the baked blob at cart 0x264140 into
# VRAM 0xF800 -- free (SAT 0xF000+512B, planes 0xC000/0xE000, tile slots
# stop at 0xB000) and read by nothing, so it cannot move a pixel.
# NO WRAM reporting on purpose: the first cut reported to 0xFFA260 and
# read back garbage, because the 0xFFA200 ring and the 48-word CRAM
# shadow at 0xFFA1C0 both cover it (and already overlap each other).
# VERIFY EXTERNALLY: dump "VDP VRAM" and compare 0xF800..0xF820 against
# sh_src/tiles_md.bin offset 0x540 (357A AC77 35AA 7C75 ...). Equal =
# CART DMA WORKS. All zero = the DMA did nothing. Needs TILESMD=1.
# `make ... TILESLIM=1` = 2026-09-19, docs/design/SLIM-PIPELINE.md.
# The SH-2 stops shipping tile PIXELS and ships a 2-WORD record instead;
# the 68K fetches the art from cart through the 0x900000 bank window and
# writes it to VRAM. cart -> 68K -> VRAM is TWO payload copies against
# the three the FB route costs, and 688 packet words then hold ~344
# records instead of ~40 -- the packet is the throughput wall (MDBATCH 96
# collapses), and throughput is residency, which is the black tiles.
# Needs TILESMD=1: it reads the baked blob. VDP DMA from cart is BLOCKED
# (measured, ares + FPGA, control-gated) so port writes are the only way.
ifdef TILESLIM
SHCCFLAGS += -DTILE_SLIM
MDCCFLAGS += -DTILE_SLIM
MDASFLAGS += -Wa,--defsym,PARTB_HOOK=1
endif
# `make line CARTREADAT=n` = 64 cart-window reads per vint at position n
# (18 = top of md_consume, 17 = partb_hook after the game's IRQ4); see
# md_main.c cart_read_burst. VERIFY .build_flags carries CART_READ_AT.
ifdef CARTREADAT
MDCCFLAGS += -DCART_READ_AT=$(CARTREADAT)
MDASFLAGS += -Wa,--defsym,PARTB_HOOK=1
endif
ifdef CARTREADADDR
MDCCFLAGS += -DCART_READ_ADDR=$(CARTREADADDR)
endif
ifdef CARTREADDELAY
MDCCFLAGS += -DCART_READ_DELAY=$(CARTREADDELAY)
endif
ifdef SLIMVALUE
MDCCFLAGS += -DSLIM_VALUE
endif
# `make line PALFIRST=1` = BG palette DMA at the consume's top (2026-09-20)
# `make line BOOTFLIPRATE=1 GLOWRATE=1 MDSVALUE=1` = the SH-2's tile-table install
# state on the value channel, flooded in alternate 4-second windows.
ifdef MDSVALUE
SHCCFLAGS += -DMDS_VALUE
MDCCFLAGS += -DMDS_VALUE
endif
ifdef PALFIRST
MDCCFLAGS += -DPAL_FIRST
endif
# `make line CRAMPROBE=1` = BG palette readback (CRAM vs shadow) with the picture kept
ifdef CENSUS2
SHCCFLAGS += -DCENSUS2
MDCCFLAGS += -DCENSUS2
endif
ifdef CRAMPROBE
MDCCFLAGS += -DCRAM_PROBE
endif
# `make line RIGBARCODE=1` = 80-bit name-table barcode on plane A rows 24-27
# (docs/design/RIG-READOUT.md); decode captures with tools/rig_barcode.py
ifdef RIGBARCODE
SHCCFLAGS += -DRIG_BARCODE
MDCCFLAGS += -DRIG_BARCODE
endif
# bisect the barcode build's ingredients (it wins the level-start race on
# every layout, 2026-09-21): BCNODMA=1 drops its 384-word DMA, BCDMAONLY=1
# drops its CRAM reads/writes and FB reads and keeps the DMA
ifdef BCNODMA
MDCCFLAGS += -DBC_NODMA
endif
ifdef BCDMAONLY
MDCCFLAGS += -DBC_DMAONLY
endif
# BCEND=1 emits the barcode from partb_end_hook (after the FM raise);
# BCROWS=N uses N rows (default 6; the DMA is N*64 words)
ifdef BCEND
MDCCFLAGS += -DBC_END
endif
ifdef BCROWS
MDCCFLAGS += -DBC_ROWS=$(BCROWS)
endif
# race candidates after slim_dma (2026-09-21): DMAWAIT=1 spins on the VDP
# DMA-busy bit; DMADELAY=N spins N x ~12 cycles
ifdef DMAWAIT
MDCCFLAGS += -DDMA_WAIT
endif
ifdef DMADELAY
MDCCFLAGS += -DDMA_DELAY=$(DMADELAY)
endif
ifdef BGVALUE
MDCCFLAGS += -DBG_VALUE
endif
ifdef SLIMVERIFY
MDCCFLAGS += -DSLIM_VERIFY
endif
ifdef SLIMSAMEVINT
MDCCFLAGS += -DSLIM_SAMEVINT
endif
ifdef SLIMNODMA
MDCCFLAGS += -DSLIM_NODMA
endif
ifdef SLIMNOFETCH
MDCCFLAGS += -DSLIM_NOFETCH
endif
# `make ... BANKPOKE=1` = bisect the slim build's hardware-only black
# screen. This is the LINE's working FB tile route with ONE addition:
# the 0xA15104 bank switch and restore every vint. Tiles still ship the
# old way, so a black screen cannot be blamed on missing art.
#   black screen -> the bank switch is the fault (the SH-2 reads cart
#     through that same register; mdspr_upload only gets away with it
#     during scene loads while the SH-2 is quiet)
#   renders fine -> the switch is safe, and the slim build dies on the
#     VOLUME of cart reads and VDP writes inside the vint
# `make ... DELIVTEST=n` = FRAME DELIVERY TEST HARNESS. Validate ONE
# delivery method against a known 16-word sample into free VRAM 0xF800,
# read it back, and paint a STICKY verdict over CRAM 0-31:
#   GREEN every word matched | RED delivered wrong bytes | BLUE nothing
# Methods: 0 DMA from WRAM (the shipping idiom -- MUST PASS, it is the
# harness's own control), 1 port writes immediate, 2 port writes from
# cart via the bank window, 3 same via the identity map, 4 DMA sourced
# from CART (known broken on the rig -- MUST FAIL, or the harness cannot
# detect failure and no pass from it means anything).
ifdef DELIVTEST
MDCCFLAGS += -DDELIV_TEST=$(DELIVTEST)
endif
ifdef PORTPOKE
MDCCFLAGS += -DPORT_POKE
endif
ifdef BANKPOKE
MDCCFLAGS += -DBANK_POKE
endif
# `make ... TILESLIM=1 SLIMCAP=N` = tiles the 68K fetches per vint.
# The uncapped slim build (up to 40) black-screens on the FPGA and
# renders fine in ares; BANKPOKE proved the bank switch innocent, so it
# is VOLUME -- 40 VDP address setups and 640 scattered cart reads a
# vint, against mdspr_upload's 512 reads in ONE contiguous run through
# ONE address setup. ares cannot see adapter wait states, so the rig
# decides: raise N until the screen dies and that is the budget.
ifdef SLIMCAP
MDCCFLAGS += -DSLIM_CAP=$(SLIMCAP)
SHCCFLAGS += -DSLIM_CAP=$(SLIMCAP)   # the emitter caps records per packet at the 68K's per-vint fetch
endif
ifdef CARTDMAPROBE
MDCCFLAGS += -DCART_DMA_PROBE
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
ifdef FBXTAIL
MDCCFLAGS += -DFBX_TAIL
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
# TEXTMASKPKT=1 = NOTES 63 / LOOP29 269. TEXTCAPMASK's mask, carried in
# its own word in the FB packet half (packet_fmt.h FBX_TXM_*) instead of
# COMM2's high byte. COMM2 has FOUR writers on the 68K side and three of
# them write plain BANK_SHADOW, so card L's mask could be cleared before
# the master read it and the capture left TEXT_U half-updated (NOTES
# 60/62). The new word has one writer, is tagged, and an untagged read
# means FULL capture -- the failure direction is slow-and-right, never
# fast-and-wrong. Needs FBXPORT (the packet half is the carrier).
ifdef TEXTMASKPKT
SHCCFLAGS += -DTEXTCAP_MASK -DTXT_MASK_PKT
MDCCFLAGS += -DTXT_MASK -DTXT_MASK_PKT
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
ifdef NTPROBE
SHCCFLAGS += -DNT_PROBE -DFLIP_CENSUS
MDCCFLAGS += -DFLIP_CENSUS
endif
ifdef CSETCENSUS
SHCCFLAGS += -DCSET_CENSUS -DFLIP_CENSUS
MDCCFLAGS += -DFLIP_CENSUS
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
# `make ... BOOTFLIPRATE=1` = LOOP29 232: presented frames per 64 vints on
# hardware (FS bank changes between vint tops), flooded as bias|count.
# GAMERATE reads 64 on every GAMEGATE build; this is the picture rate.
ifdef BOOTFLIPRATE
MDCCFLAGS += -DBOOT_VALUE -DBOOT_FLIPRATE
endif
# `make ... BOOTFLIPRATE=1 ECHOCENSUS=1` = NOTES 45 / LOOP29 258: why 46 of
# 64 vints present no frame. The master tags its declines into the echo
# word (0xF1F1 edge / F1F2 nothing drawn / F1F3 nothing shipped); the 68K
# classifies every vint and the value channel carries eight tagged counts
# per 64 vints (tag 3 bits | count 6 bits, the tag stepping every 8 vints).
ifdef ECHOCENSUS
MDCCFLAGS += -DECHO_CENSUS
SHCCFLAGS += -DECHO_CENSUS
endif
# `make ... BOOTFLIPRATE=1 STAMPCENSUS=1` = NOTES 47 / LOOP29 259: the ISR's
# four pre-flip stamps (post seen / after truth drain / after slave capture /
# at the guard) as mean (tags 0-3) and max (4-7) per 64 vints, 64-tick steps.
ifdef STAMPCENSUS
MDCCFLAGS += -DSTAMP_CENSUS
SHCCFLAGS += -DSTAMP_CENSUS
endif
# `make ... BOOTFLIPRATE=1 STAMP2CENSUS=1` = NOTES 53: stage 2 split -- after
# cram_flush_pen (VBS 1), after the page merge (VBS 2), after the truth drain
# (VBS 3), and pages actually copied (DIAG[54]) per vint; same channel.
ifdef STAMP2CENSUS
MDCCFLAGS += -DSTAMP_CENSUS
SHCCFLAGS += -DSTAMP2_CENSUS
endif
# `make ... BOOTFLIPRATE=1 STAMP3CENSUS=1` = LOOP29 264: the master FRT's ticks per vint (>>10) on the channel -- is the FRT prescale the same on the FPGA?
ifdef STAMP3CENSUS
MDCCFLAGS += -DSTAMP_CENSUS
SHCCFLAGS += -DSTAMP3_CENSUS
endif
# `make ... BOOTFLIPRATE=1 STAMP4CENSUS=1` = LOOP29 264c: per vint on the channel: stale-window bails, edge declines, holds, ISR entries.
ifdef STAMP4CENSUS
MDCCFLAGS += -DSTAMP_CENSUS
SHCCFLAGS += -DSTAMP4_CENSUS
endif
ifdef STAMP6CENSUS
MDCCFLAGS += -DSTAMP_CENSUS
SHCCFLAGS += -DSTAMP6_CENSUS
endif
ifdef STAMP5CENSUS
MDCCFLAGS += -DSTAMP_CENSUS
SHCCFLAGS += -DSTAMP5_CENSUS
endif
# `make ... BOOTFLIPRATE=1 TAILCENSUS=1` = NOTES 49 / LOOP29 260: the 68K's
# IRQ4 tail split in lines (entry->A, batch A, batch B, pump, blast,
# blast->post, idle before IRQ4) as means per 64 vints; tag 7 = vints with
# no idle. The GAMEGATE thunk stamps the frame-done V (patch_game.py).
ifdef TAILCENSUS
MDCCFLAGS += -DTAIL_CENSUS
export TAILCENSUS
endif
# `make ... BOOTFLIPRATE=1 CONSUMECENSUS=1` = LOOP29 262: packet B's consume
# split in lines (preamble / rows / tail / whole) as mean and max per 64 vints.
ifdef CONSUMECENSUS
MDCCFLAGS += -DCONSUME_CENSUS
endif
# `make ... MDSTATE=1` = PLAN-SINGLE-VINT fold 4 (LOOP29 233): the 68K
# posts one state word per vint from IRQ4 on COMM14 (tag E, sequence,
# cutscene byte 0xFFF148, round 0xFFF142); the SH-2 takes the round from
# it (never from COMM10's dirty-mask neighbours) and the cutscene bit
# forces the round off screen with no detector lag. Needs MDROUND.
ifdef MDSTATE
SHCCFLAGS += -DMD_STATE
MDCCFLAGS += -DMD_STATE
endif
# `make ... C1PUNCH=1` = fold 1's hole punch (LOOP29 175/243): sprite
# pixels of priority < 3 are not written into cells the FG holds as
# cat-1 (a 40x28 mask from the master's name-table pass), so the MD's
# plane A HIGH shows the ground over them. Needs CAT1MD + C1NOFB.
ifdef C1PUNCH
SHCCFLAGS += -DC1_PUNCH
SHOBJS += sh_src/cat1hole_data.o
endif
ifdef C1NOMASK
SHCCFLAGS += -DC1_NOMASK
endif#   C1RTOFF=1   the punch code is compiled but never taken at run time
ifdef C1RTOFF
SHCCFLAGS += -DC1_RTOFF
endif
# `make ... C1MASKTAB=1` = Build D (LOOP29 247, NOTES 34): the scene's
# class-2 tile opacity masks (tools/bake_cat1mask.py -> sh_src/cat1mask.h,
# <= 1.5 KB) live in SDRAM from mds_install; the name-table pass stores a
# mask index per class-2 cell and the slave's punched loop reads one SDRAM
# byte per cell row instead of eight cart bytes. Needs C1PUNCH.
ifdef C1MASKTAB
SHCCFLAGS += -DC1_MASKTAB
endif
# `make ... C1STAMP=1` = Card H (NOTES 36, LOOP29 252): the punch as a
# STAMP. The sprite loops compile unpunched (vi95's bytes); after the
# pp<3 pass the slave writes MD-through over the hole cells of every row
# a sprite touched (class 1 whole, class 2 by the cat1mask.h opacity
# bit), then draws the pp=3 sprites. Implies C1MASKTAB. Needs C1PUNCH.
ifdef C1STAMP
SHCCFLAGS += -DC1_STAMP
endif
# `make ... SCANMEMO=1` = Card I (NOTES 33.2, LOOP29 254): each plane's
# baked scan is memoized by (round, pages, vy0, vx0 cell) and re-merged
# while the scroll stays inside the cell. Needs SETCOLS.
ifdef SCANMEMO
SHCCFLAGS += -DSET_COLS_MEMO
endif
# Card J (NOTES 41, LOOP29 256), two flags measured alone then together:
#   SPRRUN32=1  (b) baked runs copied four pixels a longword store
#   SPRLIST=1   (a) the chain's live records read once into a cached list
ifdef SPRRUN32
SHCCFLAGS += -DSPR_RUN32
endif
ifdef SPRLIST
SHCCFLAGS += -DSPR_LIST
endif
ifdef SPRNOPIX
SHCCFLAGS += -DSPR_NOPIX
endif
# `make ... PLOTOFF=1` = THE PLOT ABLATION (2026-10-12). compose_sprites keeps its record
# walk, clipping, c1cov stamp, row walk and vzoom accumulator and draws NO pixels: every
# plot loop is bounded by `x < 504` and the flag sets x there. SPRNOPIX cannot serve --
# it ablates the SPR_RUN32 baked-run path and NOSPRBAKE=1 is on the port build. Exists to
# decide whether the FRT bracket GP[0] (~260 lines/chain) or the ares profile (~112) is
# telling the truth about the sprite plot. NEVER SHIP: the screen loses its sprites.
ifdef PLOTOFF
SHCCFLAGS += -DPLOT_OFF
endif
# `make ... PORTPLOTW=1` = THE WORD PLOT (2026-10-12). The 1:1 sprite loop stores one byte
# per pixel (0x06032c64-0x06032cc2: four mov.b per 4-pixel iteration, ~10 instructions a
# pixel) and that loop is 57% of the compose, 200.6 of the chain's ~353 lines. This writes
# four pixels as one masked longword through a 512-byte byte-indexed table whose bit 7 is
# the mask, so the mask is arithmetic and a second table is not needed. ALIGNED SPRITES
# ONLY -- (xpos-184) % 4 == 0, measured 37.0% of the path's row-pixels in demo A; the
# other three cases straddle and need a 2-word window with a per-case constant shift.
# The old loop is the control at the same HEAD. Gate: pixel identity FIRST.
ifdef PORTPLOTW
SHCCFLAGS += -DPORT_PLOTW
endif
# `make ... PORTPLOTW=1 PORTPLOTWV=1` = THE EXACTNESS PROOF. Two roms cannot be compared
# by screenshot here: the candidate runs the game FASTER, so at a matched game tick the
# two show different generations -- measured, 45% of the differing pixels landed in band 0,
# which carries no sprites at all in demo A. So the word path recomputes, for every
# longword it writes, the four bytes the byte loop would have written, and counts the
# disagreements. PLOTWV at 0x38000: [0] checked, [1] mismatched. Zero is the gate.
ifdef PORTPLOTWV
SHCCFLAGS += -DPORT_PLOTW_VERIFY
endif
# `make ... PORTHOT=1` = THE HOT SET TO SDRAM (2026-10-13). Measured on the line: the master
# spends 267,550 cyc/vint cart-resident at CPI 3.06, and the hottest game functions run at
# CPI 9-13.5 ONLY because .portcode is cart. This moves the named four into .ramtext, which
# mars_start.s already boot-copies -- .portcode selects `.text .text.*` from portfull
# objects, so a function in `.ramtext` is not matched by it and no new output section is
# needed. 4,244 bytes carrying 47,970 cyc/vint = 12.5% of the master.
#   port_object_dispatch  22,053 cyc/vint  CPI  3.93    544 B  (sh_src/port_l2.c)
#   FUN_000103e0           9,361           CPI 12.61  2,136 B
#   FUN_0000669c           8,733           CPI  9.12    344 B
#   FUN_00010824           7,823           CPI 13.33  1,220 B
# FUN_000049ba (8,664, 1,672 B) is the fifth and does NOT fit: .ramtext has 1,228 bytes
# under the old cap and 5,324 under 0x39000, against 5,916 for all five.
# The cap moves into the probe scrap at 0x38000, so PORTHOT excludes its owners.
# `make ... MDDEMOA=1` = DEMO A'S OWN PALETTE TABLE (2026-10-13, option (b)). Demo A's
# on-screen set is a measured, stable 30 csets; six of them already render as backdrop
# because the shipped table gives them no MD line, and the other 24 pack into THREE lines
# [15,14,15]. So demo A gets its own table with line 4 EMPTY -- free for an MD sprite set
# -- at no colour cost beyond what is already lost. Selected on the attract step byte
# 0xFFF031 == 0x0C (m_main.c md_attract_table); demo B keeps table 0 untouched. Regenerate
# the table with `SPLIT_LINES=3 python3 tools/round_split.py --demoa --emit`.
# `make ... C1MASKROUND=1` = the cat-1 masks keyed on the ROUND, not the palette table
# (2026-10-13). mds_install passes the TABLE index to c1mask_install, which refuses
# anything >= CAT1MASK_SCENES = 5 and empties the baked set, so every cat-1 tile rebuilds
# its opacity mask through c1rt_class's 64-pixel variable-shift loop per tile per frame:
# measured +58,039 cyc/vint and 43 game ticks for a table at index 11. The masks come from
# the 68K's per-round table at 0x1CE2 (bake_cat1mask.py:22), so scene == round, and the
# round is 3 bits and cannot exceed the bound. PRECONDITION for any table above index 4.
ifdef C1MASKROUND
SHCCFLAGS += -DC1MASK_ROUND
endif
ifdef MDDEMOA
SHCCFLAGS += -DMD_DEMOA_SPLIT
endif
# MDDEMOASTEP=255 compiles the selector in but makes its compare unreachable -- the A/B
# that separates the selector's PER-CALL cost from the table's effect.
ifdef MDDEMOASTEP
SHCCFLAGS += -DMDDEMOA_STEP=$(MDDEMOASTEP)
endif
ifdef PORTHOT
# THE CAP ONLY HAS TO MOVE FOR THE GENERATED SET. The dispatcher alone is 544 bytes and
# .ramtext ends at 0x06037d54 with it -- inside the original 0x31000-0x38000 slot -- so
# `PORTHOT=1 CPORT_HOT=` coexists with PORTRIG and is the build the RIG can measure.
# Only CPORT_HOT pushes .ramtext to 0x06038bc8, into the probe scrap, and only then do
# the probe owners collide.
ifneq ($(strip $(CPORT_HOT)),)
ifdef GENRING
$(error PORTHOT+CPORT_HOT excludes GENRING: the .ramtext cap moves into 0x38000, which GR claims)
endif
ifdef ROWSHIP_PROBE
$(error PORTHOT+CPORT_HOT excludes ROWSHIP: the .ramtext cap moves into 0x38000, which RSHASH claims)
endif
ifdef PORTRIG
$(error PORTHOT+CPORT_HOT excludes PORTRIG: the cap moves into 0x38000, which RIG_SCRAP claims. Measure the rig on `PORTHOT=1 CPORT_HOT=`, which needs no cap move.)
endif
endif
# CPORT_EXTRA_CFLAGS is overwritten at the cport_build.sh call site (:4889) with
# PORT_TRACE_CF, so PORT_HOT reaches the generated unit through CPORT_DEFS, which that
# same line builds from $(filter -D%,$(SHCCFLAGS)).
SHCCFLAGS += -DPORT_HOT
CPORT_HOT = FUN_000103e0,FUN_0000669c,FUN_00010824
export CPORT_HOT
endif
# `make ... PORTHOT2=1` = THE MASTER'S HOT GAME CODE TO SDRAM, CHOSEN FROM A PROFILE
# (2026-10-24). On the office MiSTer the master's game tick is 126 lines a vint against 42 on
# ares: the port runs from cart and the core charges a full SDRAM access per cart read
# (srcref/S32X_MiSTer rtl/32X/IF.sv RS_MD_WAIT). tools/hot_set.py ranks the profile by cart
# wait per byte and writes tools/hot_set.txt (`name@region bytes wait kind`); this reads it:
# generated functions and thunks go through CPORT_HOT (cport_prep redeclares them with
# PORTHOT2/PORTHOT3, cport_dispatch tags the thunks), runtime ones get -DHOT2_<name>
# (sh_src/port_l2.c). Region 2 = .ramtext2 above .bss (mars.ld), region 3 = .ramtext3.
# tools/hot_check.py fails the link when a listed function is not in its region or the
# dispatch table moved. HOT_SET=<file> points it at another list.
ifdef PORTHOT2
ifdef PORTHOT
$(error PORTHOT2 and PORTHOT both drive CPORT_HOT -- pick one)
endif
HOT_SET ?= tools/hot_set.txt
SHCCFLAGS += -DPORT_HOT2
CPORT_HOT := $(shell awk '!/^\#/ && NF && $$4 != "runtime" {print $$1}' $(HOT_SET) | paste -sd, -)
export CPORT_HOT
SHCCFLAGS += $(shell awk '!/^\#/ && NF && $$4 == "runtime" {split($$1, a, "@"); print "-DHOT2_" a[1]}' $(HOT_SET))
endif
ifdef C1STAMPNW
SHCCFLAGS += -DC1_STAMP_NW
endif
# `make ... C1FAST=1` = Build E (LOOP29 248): a punched sprite run whose
# cells are all class 0 takes the original tight copy; only runs that
# touch a class-1/2 cell walk cells. Needs C1PUNCH.
ifdef C1FAST
SHCCFLAGS += -DC1_FAST
endif
# `make ... C1CACHED=1` = Build F (LOOP29 249): the slave reads the punch's
# cell mask through its cache instead of the uncached SDRAM alias (a
# round trip per cell, per pixel on the 1:1/zoomed paths). Needs C1PUNCH.
# `make ... CACHELOCK=1` = CARD CACHELOCK (docs/design/CARD-CACHELOCK.md).
# Preloads cache ways 0/1 with the .ramtext.lock block and sets CCR.TW so
# they stop being replaced but keep hitting -- 2 KB of the master's hot
# path that never misses. RIG ONLY: ares charges instruction cycles and
# models no cache, so it can neither show the win nor catch a mistake
# here. Readback at 0x26028FC0 = (ways << 16) | tag slots verified (128
# when both ways took).
# `make ... CHEVPROBE=1` = CARD CHEVPEN probe (NOTES 105/106). Counts, at
# 0x26028DE0: mds_onscreen now, vints with glow_chev, mdp_assign_set calls
# and successes for sets 19/20/21, mdp_free_set calls and 2348-guard
# refusals, mds_install calls, and onscreen 0->1 edges. Read with
# ares --dump sdram:0x28DE0:48. PROBE ONLY, never ships.
ifdef CHEVPROBE
SHCCFLAGS += -DCHEV_PROBE
endif
# `make ... CHEVFIX=1` = CARD CHEVPEN (NOTES 111/112). (a) forces the
# chevron sets 19/20/21 to claim EXCLUSIVE pens while glow_chev is up, so
# mdp_pen_rc==1 and the sole-owner in-place recolour (16082) tracks their
# colour ring -- a shared pen falls through to tolerated drift and is
# never repainted. (b) holds those sets against the ~1595 eviction.
ifdef CHEVFIX
SHCCFLAGS += -DCHEV_FIX
endif
ifdef CACHELOCK
SHCCFLAGS += -DCACHE_LOCK
endif
# `make ... CACHELOCKSHOW=1` = put the lock readback on the value channel
# (tag 0 ways locked, tag 1 of 128 tag slots verified). Needs CACHELOCK and
# BOOTVALUE; without it a failed lock and a useless lock read the same.
ifdef CACHELOCKSHOW
SHCCFLAGS += -DCACHE_LOCK_SHOW
MDCCFLAGS += -DCACHE_LOCK_SHOW
endif
ifdef C1CACHED
SHCCFLAGS += -DC1_CACHED
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
# md.ld's WRAMCODE-region-2 fence asserts only when the region is in use, so
# tell the linker when that is. Without this the fence fired on builds that
# never put code there (the test cartridge).
ifdef WRAMCODE2
MDLDFLAGS += -Wl,--defsym,__wramcode2=1
endif
# CARTMB=4|6|8 = the cart image size (2026-10-22). THE RIG TAKES 4 MB AND NOTHING THE SH-2 READS
# MAY SIT ABOVE 0x400000: sw288 (8 MB, port sections at 0x400000) and sw290 (6 MB, same) are a
# black screen on the office MiSTer, sw289's bytes padded to 6 MB (sections under 4 MB) draw, so
# it is the core not serving SH-2 cart reads above 4 MB, not the file size. (Mike: Doom
# Resurrection is 6 MB and runs there; whatever it keeps above 4 MB the SH-2 is not reading it
# straight.) CARTMB=4 is the default: .sfxdata moves into the free run after .sounddata
# (0x292000, 224 KB of 408), which frees 0x300000-0x400000 for the port: .portrom 0x300000,
# .portdata 0x340000, .portcode 0x34D000 (ends 0x3C057C, 254 KB spare). .portrom stays on a
# 256 KB boundary: the first 4 MB packing (sw289, .portrom at 0x335100) smeared every sprite, on
# ares too, and every base that ever worked (0x340000, 0x400000) was 256 KB aligned (the port
# masks 68K ROM addresses with & 0x3FFFF: cport_prep.py:404, port_l2.c:908). CARTMB=6 and
# CARTMB=8 keep the ca51f3a3 layout (sections at 0x400000): ares-only probes, never the rig.
# CART4MB=1 is a synonym for CARTMB=4. The scripts are generated by sed from mars.ld (rules at
# the end of this file; the elf rule depends on $(LDSCRIPT) so the shadow tree generates them).
CARTMB ?= 4
ifdef CART4MB
CARTMB = 4
endif
ifeq ($(CARTMB),4)
LDSCRIPT   = sh_src/mars4.ld
CART_PAD   = 0x400000
CART_HI    = 16\#400000
PORTROM_BASE = 0x02300000
else ifeq ($(CARTMB),8)
LDSCRIPT   = sh_src/mars.ld
CART_PAD   = 0x800000
CART_HI    = 16\#800000
PORTROM_BASE = 0x02400000
else
LDSCRIPT   = sh_src/mars6.ld
CART_PAD   = 0x600000
CART_HI    = 16\#600000
PORTROM_BASE = 0x02400000
endif
SHLDFLAGS  = -T $(LDSCRIPT) -nostdlib

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
# `make ... R60NEW=1` = THE REBUILD (Mike's order, 2026-10-14): the old renderer is DELETED,
# not optimised -- "it only worked for the 68K port". sh_src/m_main.c and sh_src/s_main.c are
# not compiled; sh_src/r60/ provides m_main() and s_main() instead. The port
# (sh_src/portfull, port_l2.c and its gate) and the 68K shim are untouched: the renderer's
# whole public surface is those two entry points, everything else between them and the game
# is DATA in SDRAM (SPR_SNAP, the tilemap, PAL_SH, TEXT), which is why this swap is possible
# at all. docs/design/REBUILD.md section 2 is the design.
ifdef R60NEW
SHOBJS := $(filter-out sh_src/m_main.o sh_src/s_main.o,$(SHOBJS))
SHOBJS += $(patsubst %.c,%.o,$(wildcard sh_src/r60/*.c))
sh_src/r60/%.o: SHCCFLAGS += -Ish_src
SHCCFLAGS += -DR60_NEW
MDCCFLAGS  += -DR60_NEW
# `R60NOFLIP=1` = PROBE ONLY: the V-ISR never writes FS. Isolates the FB BANK from
# everything else when a 68K-side reader disagrees with the master about what is in
# the framebuffer. Never a ship flag -- the picture freezes on one bank.
ifdef R60NOFLIP
SHCCFLAGS += -DR60_NOFLIP
endif
# `R60BARPROBE=1` = PROBE ONLY: a mid-screen test block, half in the renderer's
# reserved pen and half in a pen the game uses, to tell a dropped framebuffer write
# from a dropped CRAM entry when the board disagrees with ares.
ifdef R60BARPROBE
SHCCFLAGS += -DR60_BARPROBE
endif
# `R60MDPROBE=1` = PROBE ONLY: the 68K paints the MD backdrop green every vint, a
# board-visible channel that does not pass through the 32X framebuffer or CRAM.
ifdef R60MDPROBE
MDCCFLAGS += -DR60_MDPROBE
endif
# `R60NOSPR=1` = PROBE ONLY: the slave walks the sprite list and draws NOTHING. The
# freshness test the rig notes ask for -- if the board still shows sprites, the rom
# being measured is not the rom that was pushed.
ifdef R60NOSPR
SHCCFLAGS += -DR60_NOSPR
endif
# `R60SOLID=1` = PROBE ONLY: the slave fills the hidden bank with one pen and draws
# nothing else. Separates "framebuffer writes do not land" from "the flip is not
# double-buffering" when the board disagrees with ares about the picture.
ifdef R60SOLID
SHCCFLAGS += -DR60_SOLID=$(R60SOLID)
endif
# `R60REASSERT=1` = PROBE ONLY: re-write the 32X display mode and the line table every
# presented frame, for a board that shows screen row 0 and nothing else.
ifdef R60REASSERT
SHCCFLAGS += -DR60_REASSERT
endif
# `R60ONEPEN=n` = PROBE ONLY: every sprite pixel drawn in index n. Tells a framebuffer
# holding the wrong indices from a palette holding the wrong colours, which a
# screenshot cannot.
ifdef R60ONEPEN
SHCCFLAGS += -DR60_ONEPEN=$(R60ONEPEN)
endif
# `R60WORDPLOT=1` = the sprite plot stores 16-bit PAIRS through the overwrite image
# instead of bytes. The overwrite image's zero-byte rule is defined for word writes;
# a per-pixel byte write asks for something else, and the board does not honour it.
ifdef R60WORDPLOT
SHCCFLAGS += -DR60_WORDPLOT
endif
# `R60SOLIDSPR=1` = PROBE ONLY: each sprite record drawn as a solid 32-column bar with
# NO art read at all. Separates the framebuffer writes from the CART READ that feeds
# them, which is the one input a correctly-shaped but sparse picture leaves open.
ifdef R60SOLIDSPR
SHCCFLAGS += -DR60_SOLIDSPR
endif
# `R60LONGSPR=1` = PROBE ONLY: the sprite plot stores LONGWORDS through the direct
# image, four pixels at a time, the old blit's idiom and the only framebuffer writer
# this repo has proven lands on the FPGA.
ifdef R60LONGSPR
SHCCFLAGS += -DR60_LONGSPR
endif
# `R60NOVDPOVERLAP=1` = PROBE: the master makes NO VDP-region access while the slave is
# drawing. The MiSTer core shares ONE SH-side VDP access path between both SH-2s and
# its in-flight guard is commented out (IF.sv:974-983), so an access from one CPU
# re-latches over the other's pending one and that one is lost.
ifdef R60NOVDPOVERLAP
SHCCFLAGS += -DR60_NOVDPOVERLAP
endif
# `R60PACE=n` = n nops after every framebuffer store. The MiSTer core's FB write FIFO
# drains one 16-bit write per ~3 SH-2 cycles and loses the excess (no in-flight guard,
# IF.sv:974-983), so a sustained loop must not outrun it. Costs nothing on silicon,
# where the same loop stalls instead.
ifdef R60PACE
SHCCFLAGS += -DR60_PACE=$(R60PACE)
endif
# `R60BAROVR=1` = PROBE: paint the bars through the OVERWRITE image instead of the
# direct one, same rows. Separates "the direct image is dead on the board" from
# "those rows are dead on the board".
ifdef R60BAROVR
SHCCFLAGS += -DR60_BAROVR
endif
# `R60FBCACHED=1` = BISECT STEP 1: the slave writes the framebuffer through the CACHED
# write-through window 0x04000000 (marks2's blit_half path, the only writer proven on
# the MiSTer) instead of the uncached alias 0x24000000. Reads stay uncached.
ifdef R60FBCACHED
SHCCFLAGS += -DR60_FBCACHED
endif
# `R60TICKLATE=1` = the master runs the game tick AFTER issuing GO, while the slave draws
# from its own copy of the sprite table (REBUILD section 2: tick and draw in parallel).
ifdef R60TICKLATE
SHCCFLAGS += -DR60_TICKLATE
endif
# `R60SLAVEWIN=1` = the slave owns the frame: draws a snapshot, then does the window
# (packet, CRAM, flip request, FM down) itself; the master only ticks and snapshots.
ifdef R60SLAVEWIN
SHCCFLAGS += -DR60_SLAVEWIN
endif
ifdef R60PLOT2
SHCCFLAGS += -DR60_PLOT2
endif
ifdef R60PLOT3
SHCCFLAGS += -DR60_PLOT3
endif
ifdef R60NOBARS
SHCCFLAGS += -DR60_NOBARS
endif
ifdef R60TEXT
SHCCFLAGS += -DR60_TEXT
endif
# `R60MBAND=1` = the master draws the sprite rows above a per-snapshot split row while the slave
# draws the rest (revived 2026-10-23 on the ROWCLR/KEEPSPR/PRIO line: per-core span lists, the
# keep decisions published, the master's boxes merged; r60_main.c "R60_MBAND"). `R60MBANDSPLIT=n`
# = n/16 of the sprite rows to the master (default a third); `R60MBANDW=1` weights rows by width.
ifdef R60MBAND
SHCCFLAGS += -DR60_MBAND
endif
ifdef R60MBANDSPLIT
SHCCFLAGS += -DR60_MBAND_NUM=$(R60MBANDSPLIT)u -DR60_MBAND_DEN=16u
endif
ifdef R60MBANDWAITX
SHCCFLAGS += -DR60_MBAND_WAITX=$(R60MBANDWAITX)u   # the master's cap (master FRT ticks, 184 a line) on waiting for a CLEARED that is on its way
endif
ifdef R60MBANDTUNE
SHCCFLAGS += -DR60_MBAND_TUNE   # the split share retuned every pass from the band's and the slave's measured durations (r60_band_tune)
endif
ifdef R60MBANDYIELD
SHCCFLAGS += -DR60_MBAND_YIELD   # the master's plane walk runs a posted sprite band at every row / 8 art records (r60_band_yield)
endif
ifdef R60MBANDSTEAL
SHCCFLAGS += -DR60_MBAND_STEAL   # the band claim (MSTART/SSTEAL): a claimed band is waited out, an unclaimed one refused after 60000 ticks so the master aborts instead of drawing late; R60MBANDSTEALDRAW adds the slave drawing it
endif
ifdef R60MBANDSTEALDRAW
SHCCFLAGS += -DR60_MBAND_STEALDRAW   # PROBE (with R60MBANDSTEAL): the slave draws an unclaimed band itself; loses band rows on ares (attract 2994-3005), cause not found
endif
ifdef R60SPLITFAST
SHCCFLAGS += -DR60_SPLITFAST   # r60_split_row's histogram as a prefix sum (same split; ~4.8 master lines a vint less)
endif
ifdef R60MBANDW
SHCCFLAGS += -DR60_MBAND_WEIGHT
endif
ifdef R60MBANDNOWAIT
SHCCFLAGS += -DR60_MBAND_NOWAIT   # PROBE: the master does not wait for CLEARED before its plane walk (the band runs from the idle loop only)
endif
ifdef R60MBANDFLIPA
SHCCFLAGS += -DR60_MBAND_FLIPA   # PROBE: the window's flip requested with the span (the line's order) instead of after the 68K's ack
endif
ifdef R60MBTIME
SHCCFLAGS += -DR60_MBTIME   # PROBE: the band timeline ring at sdram 0x29440 (r60_main.c R60_MBTIME); the master's plane walk to cart for the room
endif
ifdef R60MBANDNULL
SHCCFLAGS += -DR60_MBAND_NULL   # PROBE: the master posts MDONE without drawing (splits a slave-side fault from the band)
endif
ifdef R60ROWFAST
SHCCFLAGS += -DR60_ROWFAST_OPT   # the hoisted per-sprite row loop (2026-10-23); see R60_ROWFAST in r60_main.c
endif
ifdef R60CRAMPROBE
SHCCFLAGS += -DR60_CRAMPROBE
endif
ifdef R60RECTCLR
SHCCFLAGS += -DR60_RECTCLR
endif
ifdef R60ZOOM
SHCCFLAGS += -DR60_ZOOM
endif
# `R60ARTMAX=N` = art records a plane a window, bounded by the packet's free space
# (r60_art_fit); default 10 = the 2026-10-14 cap.
# `R60FILL32=1` = under the display gate's hold the 32X paints the planes into the bank
# (band by band, scrolled by the canvas offset) instead of blanking; a CANDIDATE, not the
# line: on ares the hold then runs ~0.3 flips/vint for ~90 frames (sw99).
# `R60PLANREMARK=1` = a palette plan change re-marks moved slots dirty and invalidates
# the shadow instead of wiping the slot cache (no holes, no full re-ship).
ifdef R60PLANREMARK
SHCCFLAGS += -DR60_PLANREMARK
endif
ifdef R60FILL32
SHCCFLAGS += -DR60_FILL32
endif
ifdef R60XOFFPROBE
SHCCFLAGS += -DR60_XOFFPROBE
endif
ifdef R60XOFFEVEN
SHCCFLAGS += -DR60_XOFFEVEN
endif
ifdef R60XOFFCONST
SHCCFLAGS += -DR60_XOFFCONST=$(R60XOFFCONST)u
endif
ifdef R60ARTMAX
SHCCFLAGS += -DR60_ARTMAX=$(R60ARTMAX)u
endif
# `R60FATCAP=N` = art records a window under the GAME'S OWN blank (DISPHOLD 1): both
# planes' art rides packet A (3072 words at FB 0x17000) while the slave idles; default 160.
# The 68K's consume then runs ~1.25 lines a record, so a cap that pushes its vint past
# a frame halves the game's frame-flag-paced load (sw114: 49 frames vs the arcade's 24).
# `R60FATSPAN=N` = vints the slave leaves FM down for that consume (default 4; sw113 at
# 2 timed out 9-11 spans a cut and the scene landed with garbage art).
ifdef R60FATCAP
SHCCFLAGS += -DR60_FATCAP=$(R60FATCAP)u
endif
ifdef R60FATSPAN
SHCCFLAGS += -DR60_FATSPAN=$(R60FATSPAN)u
endif
# `R60CHURNCAP=N` = fat packet A (N records) for a window after one that rejected
# >= R60CHURNMIN cells (default 100) with the display up: the title logo rewrite and
# scene tile churn; costs cadence for those windows only. Candidate, 2026-10-16.
# `R60KEEPSTALE=1` = a cell whose new art did not ship this window keeps LAST window's
# cell instead of tile 0: the title logo recolour rolls white->red with no black holes,
# scene rewrites show the old scene's tiles until the new art lands. 2026-10-16.
# `R60TXTPOOL=1` = the text colours live in the sprite sets' unused pens 0 and 15 (31 CRAM
# entries, allocated per window to the (group, pen) pairs the visible text uses) instead
# of sprite sets 12-14, which the arcade's sprites DO use (MAME census: all 16 sets, 12-14
# from frame 1733; the white wolf drew in text colours). 2026-10-16.
# `R60FSPROBE=1` = 68K probe: count A consumes during which FS (0xA1518A bit 0) changed
# (0xFFA07E), FS at the start (0xFFA080), A consumes (0xFFA082). 2026-10-16.
# `R60PENIDX=1` = MD pens owned per S16 colour index (shared across sets only by index) and
# recoloured in place when their owners' colours move: the title logo's white->red is a CRAM
# write instead of a 900-tile re-bake. Costs pens in busy scenes (no within-set dedupe). 2026-10-16.
ifdef R60PENIDX
SHCCFLAGS += -DR60_PENIDX
endif
# `R60PLOT4=1` = the sprite plot's four pens by table (r60_pt, 16 KB): two byte loads and two
# table loads a word instead of four extracts, tests and packs; PLOT3's stores. `R60PLOT4CHECK=1`
# runs the on-rom identity check at the first sprite (r60_plot4_bad, bit 31 = ran). 2026-10-17.
ifdef R60PLOT4
SHCCFLAGS += -DR60_PLOT4
endif
# `R60PLOT5=1` (needs R60PLOT4=1 for the tables) = per-row register loops, one carried store a
# group; `R60PLOT5CHECK=1` replays every row both ways into RAM and counts differences
# (r60_plot5_bad: low 16 = rows differing, high = end-x differing; r60_plot5_rows). 2026-10-17.
# `R60TXTFAST=1` (with R60TXTPOOL) = text rows skipped by content/pending counts and a 256-entry
# converted-glyph cache keyed on (pal, code, LUT generation). 2026-10-17.
# `R60ROWCLR=1` (with R60PLOT5) = the bank clear zeroes each drawn sprite row's own span
# instead of the sprite's box: the box clear cost 57 lines a pass in the demo. 2026-10-17.
# `R60ROWFILL=1` (with R60ROWCLR) = the span clear uses the VDP fill engine instead of CPU stores.
# `R60CRAMMASK=1` = the master hands the slave a mask of sprite colour sets whose words changed
# (cached compare on the writer); the commit skips the rest. 2026-10-17.
# `R60KEEPSPR=1` (with R60ROWCLR) = a sprite unchanged since this bank last drew it, touched by no
# changed sprite's box and no pending text cell, keeps its pixels (no clear, no draw). 2026-10-17.
ifdef R60KEEPSPR
SHCCFLAGS += -DR60_KEEPSPR
endif
ifdef R60CMSTATS
SHCCFLAGS += -DR60_CMSTATS
endif
ifdef R60CRAMMASK
SHCCFLAGS += -DR60_CRAMMASK
endif
ifdef R60RAWPENS
SHCCFLAGS += -DR60_RAWPENS   # preserve distinct S16 colours through palette transforms while pens fit
endif
ifdef R60TXTTOUCH
SHCCFLAGS += -DR60_TXTTOUCH   # restore text touched by new sprite draws and priority cuts
endif
ifdef R60ROWFILL
SHCCFLAGS += -DR60_ROWFILL
endif
ifdef R60ROWCLR
SHCCFLAGS += -DR60_ROWCLR
endif
ifdef R60TXTFAST
SHCCFLAGS += -DR60_TXTFAST
endif
ifdef R60EVPROBE
SHCCFLAGS += -DR60_EVPROBE   # PROBE: the master's tick start (9) and planes-prepare end (10) in the event ring
endif
ifdef R60TXTPLANFAST
SHCCFLAGS += -DR60_TXTPLANFAST   # the text diff scans longword pairs first; the plan's pen demand is rescanned only when the text moved (r60_tx_scan/r60_tx_demand)
endif
ifdef R60SPEECHSLAVE
SHCCFLAGS += -DR60_SPEECHSLAVE   # the speech/SFX fill on the slave's idle spins, SP_CHUNK samples a call (speech_pump_chunk), off the master's prepare
endif
ifdef R60TXTLUTPAL
SHCCFLAGS += -DR60_TXTLUTPAL   # a text LUT change marks only the moved pals' cells pending in both banks instead of forcing every glyph (r60_txt_palmark)
endif
ifdef R60PLANFAST
SHCCFLAGS += -DR60_PLANFAST   # the line plan's census reads each named page once, weighted by its quadrant count (same hist/bgu)
endif
ifdef R60PLANNEW
SHCCFLAGS += -DR60_PLANNEW   # a hold-time line plan that packs a set the old pack lacks is a change: adopt it (the green eye, 2026-10-24)
endif
ifdef R60TXTHEALLATE
SHCCFLAGS += -DR60_TXTHEAL_LATE   # PROBE (with R60TXTHEAL): the spread heal marks where the forced heal ran, after the keep decisions
endif
ifdef R60TXTHEAL
SHCCFLAGS += -DR60_TXTHEAL   # the text heal spread over the passes (8 cells a pass, cursor) instead of a whole-layer force every 16 (r60_txt_heal)
endif
ifdef R60PLOT5
SHCCFLAGS += -DR60_PLOT5
endif
# `R60BLANKFILL=1` = a BG cell whose art cannot ship this window names a SOLID tile of its set's
# colour 0 (slots 1016-1023, one per line/pen) instead of the blank over the black backdrop; a
# stale cell is kept only when it is the same art. The title cut's black blocks. 2026-10-17.
ifdef R60BLANKFILL
SHCCFLAGS += -DR60_BLANKFILL
endif
# `R60PRIO=1` = priority FG tiles over sprites: after each sprite the slave zeroes its pixels
# under the opaque pixels of every priority (bit 15) FG cell it overlaps (jts16_prio.v:63-86).
ifdef R60PRIO
SHCCFLAGS += -DR60_PRIO
endif
# `R60IDPENS=1` = a set arriving with a provisional palette (<= 3 colours, display up) gets a pen per
# index; identical sets in the following windows share its map (the title logo's ten sets): the
# fade-in and the red flip become CRAM writes instead of a 622-tile re-bake. 2026-10-17.
ifdef R60IDPENS
SHCCFLAGS += -DR60_IDPENS
endif
# `R60HOLDDIRECT=1` = under the game's blank the master writes packet A straight into the framebuffer
# (11264 words, 500 art records a window; odd parity at 0x11800 behind a stub at 0x17000), the slave
# stops flipping once the shown bank is black. Cuts fill in 2-3 windows instead of ~10. 2026-10-17.
ifdef R60HOLDDIRECT
SHCCFLAGS += -DR60_HOLDDIRECT
MDCCFLAGS += -DR60_HOLDDIRECT
endif
ifdef R60STAGEDIRECT
SHCCFLAGS += -DR60_STAGEDIRECT
endif
ifdef R60IDLECONSUME
MDCCFLAGS += -DR60_IDLECONSUME
endif
# `R60LIVEPAD=1` = live MD pads: the 68K posts both pads (COMM2) and the service bits (COMM8, tag 0xA5)
# every vint; the port feeds 0xC41003/0xC41007/0xC41001 and the MCU coin mailbox from them. 2026-10-18.
# Pad: A punch, B kick, C jump, START = start, Y (or START+A) = coin; the dips want two coins a play.
ifdef R60LIVEPAD
MDCCFLAGS += -DR60_LIVEPAD
SHCCFLAGS += -DPORT_LIVEPAD
endif
# `PORTSTARTCOIN=1` = START also coins while the credits cannot pay for a start (needs R60LIVEPAD). 2026-10-26, for pads
# the Miyoo / Analogue Pocket cores hand over without X/Y/Z/MODE. Edge-latched in sh_src/port_l2.c l2m_io_live.
ifdef PORTSTARTCOIN
SHCCFLAGS += -DPORT_STARTCOIN
endif
# `PORTREGION=N` = the region byte's power-on value (sh_src/port_l2.c port_region): 0 = the
# reference set, 1 = altbeastj (games/altbeast/glue.c). 2026-10-30.
ifdef PORTREGION
SHCCFLAGS += -DPORT_REGION=$(PORTREGION)
endif
ifdef R60PADBAR
SHCCFLAGS += -DR60_PADBAR
endif
ifdef R60LOGOPROBE
SHCCFLAGS += -DR60_LOGOPROBE
endif
# `R60SOUND=1` = the game's music: the sound lab's Z80 player on the 68K side (md_src/md_sound.c),
# streams from the cart's .sounddata blob (tools/sound_blob.py), commands from the port's MCU
# mailbox via COMM10. Needs NOTILESMD=1 (the blob takes the MD tile blob's room). 2026-10-18.
ifdef R60SOUND
MDCCFLAGS += -DR60_SOUND
SHCCFLAGS += -DPORT_SOUND
SHASFLAGS += --defsym R60_SOUND=1
endif
# `R60SNDTEST=94` = probe: the 68K music engine injects sound command 0x94 at vint 300, so the
# music lane can be heard (MAME -wavwrite) without reaching gameplay. Needs R60SOUND=1.
# `R60SPEECH=1` = the arcade's speech (uPD7759 utterances) on the slave SH-2's PWM mixer
# (sh_src/r60/r60_speech.c, the sound lab's engine), bank = sh_src/speech.bin beside the music blob.
# MAME 32X segfaults on PWM-DREQ DMA1 roms (docs/sound/SOUND.md): gate on ares + the rig only.
ifdef R60SPEECH
SHCCFLAGS += -DR60_SPEECH -DPORT_SPEECH
SHASFLAGS += --defsym R60_SPEECH=1
endif
# `R60SFX=1` = the arcade's YM sound effects as PWM voices beside the speech (needs R60SPEECH=1):
# bank sh_src/sfx.bin = tools/sfx_bake.py over MAME captures of every effect command
# (build/sfx/arc_<HEX>.wav from tools/arcade_wav.lua), cart .sfxdata at 0x2FE400.
ifdef R60SFX
SHCCFLAGS += -DR60_SFX
SHASFLAGS += --defsym R60_SFX=1
endif
# `R60CUTPF=1` = preload the demo's transformation-cut art (pages 10/11) before the switch
# (sh_src/r60/r60_planes.c r60_pf_step). 2026-10-19.
ifdef R60CUTPF
SHCCFLAGS += -DR60_CUTPF
endif
# `R60PALMAP=1` = sprite palettes 0-63 mapped onto the 16 32X colour banks per pass (sw262):
# without it palette n drew with the colours of (n & 15) (hit flashes, power-up forms).
# `R60NORECOLOUR=1` = the recolour/split pass off (sw293, 2026-10-22): the rig A/B for the
# jutter Mike reported on sw292; the pass scans PAL_SH and the owner tables every 4th window,
# memory traffic ares does not charge (LESSONS: ares charges instructions only).
ifdef R60NORECOLOUR
SHCCFLAGS += -DR60_NORECOLOUR
endif
ifdef R60PALMAP
SHCCFLAGS += -DR60_PALMAP
endif
# `R60PHBARS=1` (with `R60NOBARS=`) = PROBE: the bottom bars show the slave's average scanlines
# per pass for prep / sprites / text / whole pass, one pixel a line (sw263, the speed card).
ifdef R60PHBARS
SHCCFLAGS += -DR60_PHBARS
endif
# Read-only camera/enemy/record footer for physical scroll-lock diagnosis.
# No game RAM writes or forced cleanup; absent from ordinary candidates.
ifdef R60LOCKPROBE
SHCCFLAGS += -DR60_LOCKPROBE
endif
# `R60RIGSPLIT=1` (with `R60NOBARS=`) = PROBE: both cores' phase split as bars in rows 196..223,
# one row a value, 1 px a scanline (2026-10-24, the rig phase split; map in r60_main.c R60_RIGSPLIT).
ifdef R60RIGSPLIT
SHCCFLAGS += -DR60_RIGSPLIT
endif
# `R60CLRPROBE=1` = PROBE: FRT timers and counts for the ROWCLR clear (it runs before R60_PH[5]++,
# so no PHBARS report times it): total, FEN waits by site, CPU short spans, fills, the span-length
# histogram, in r60_clrp[24] (.bss; map in r60_main.c). 2026-10-24, the clear lane on sw309.
# R60CLRPROBE=2 = the light form (totals and counts, waits untimed) that fits beside R60CLRMERGE.
ifdef R60CLRPROBE
SHCCFLAGS += -DR60_CLRPROBE=$(R60CLRPROBE)
endif
# `R60CLREXT=1` = the ROWCLR clear zeroes ONE run per row (its cleared spans' extent, cut around the
# row's kept extent) instead of one fill per span (2026-10-24). MEASURED DEAD on the way: the exact
# per-row union (R60CLRMERGE, linked lists + insertion sort) halved the fills and cost more in
# bookkeeping than it saved (fight 0.61 -> 0.59 passes/vint).
ifdef R60CLREXT
SHCCFLAGS += -DR60_CLREXT
endif
# `R60CLRPRE=1` (with R60CLREXT) = the per-row extents of the bank the next pass clears are computed at
# the top of the slave loop, where it waits for the flip and the snapshot; the clear walks only the
# kept sprites' spans (2026-10-24).
ifdef R60CLRPRE
SHCCFLAGS += -DR60_CLRPRE
endif
# `R60CLRIDENT=1` (with R60CLREXT and R60CLRPROBE=1) = PROBE: before each run is zeroed, its nonzero
# longwords are classified against the per-span clear's list (r60_clrp[14..17]; r60_main.c).
ifdef R60CLRIDENT
SHCCFLAGS += -DR60_CLRIDENT
endif
# `R60PUNCHCACHED=1` = A/B ONLY: the FG punch's read-modify-write reads the framebuffer through the
# cached window again (the pre-2026-10-24 form). A cached FB read answers from a line filled one
# or more passes earlier, often in the other bank, and the punch wrote those bytes back: stray
# sprite pixels outside every span (r60_main.c FB_RMW). The default reads uncached.
ifdef R60PUNCHCACHED
SHCCFLAGS += -DR60_PUNCHCACHED
endif
# `R60PKTODDHI=1` (with R60STAGEDIRECT) = the odd-parity direct hold packet moves from FB 0x11800
# (68K 0x851800), which overlapped canvas rows 222-223 and left its words there as pixels, to
# 0x11E00 (68K 0x851E00), past the canvas and its spill (2026-10-24). Both CPUs.
ifdef R60PKTODDHI
SHCCFLAGS += -DR60_PKTODDHI
MDCCFLAGS += -DR60_PKTODDHI
endif
# `R60PUNCHBAND=1` = the FG-priority span punch resolves each tile cell once per tile-row band
# instead of once per row (2026-10-23: the punch read 66-70 lines a pass in the demo fight).
# `R60STAGEGATE=1` (with R60STAGEDIRECT) = a staged direct hold packet is built only while the hold
# still stands after this window's gate decision; the window that releases builds a normal packet.
# Without it that packet was copied into the FB with the display up, beside the slave's pass: torn
# on the MiSTer (shared VDP path, no in-flight guard). 2026-10-24, the rig's hold-scene tear.
ifdef R60STAGEGATE
SHCCFLAGS += -DR60_STAGEGATE
endif
# `R60HTPROBE=1` = PROBE (2026-10-24, the rig's hold-scene tear): five rows of blocks at the top-left
# (r60_main.c R60_HTPROBE): span backstops, staged copies outside the hold handshake, torn counts, and
# the 68K's consume verdicts in COMM12 (FM up / FS changed under a consume, stale skips, A/B unpaired).
ifdef R60HTPROBE
SHCCFLAGS += -DR60_HTPROBE
MDCCFLAGS += -DR60_HTPROBE
endif
ifdef R60PUNCHBAND
SHCCFLAGS += -DR60_PUNCHBAND
endif
# `R60PRIOROW=1` (with R60PRIO, R60PLOT5) = the FG-priority mask applied in the PLOT5 store from a
# per-row keep array built from the cached tile row masks; the box punch stays for zoomed sprites
# (2026-10-23). `R60PRIOROWCHECK=1` re-runs the box punch after each masked sprite over a checksum
# of its box and counts any byte it still changed (r60_pr_bad / r60_pr_checked).
ifdef R60PRIOROW
SHCCFLAGS += -DR60_PRIOROW
endif
ifdef R60PRIOROWCHECK
SHCCFLAGS += -DR60_PRIOROWCHECK
endif
# `R60PUNCH2=1` = the FG-priority box punch with its row loop hoisted (one instantiation per
# window shift, FB pointer walked by row, stats and prio_ymin/ymax summed per cell; r60_punch2
# in r60_main.c, 2026-10-23). Byte-identical in effect to the old punch. `R60PUNCH2CHECK=1`
# runs the old punch beside it on every box, in alternating order, and compares what each WROTE
# (a hash of address + keep mask per store, in store order: the FB itself moves under the slave,
# the master fills rows into it), the store count, the rows hit and prio_ymin/ymax; any
# disagreement counts in r60_punch2_bad (r60_punch2_checked boxes, r60_punch2_why[4] says which,
# r60_punch2_fb[3] = the FB checksum's own movement and its no-punch noise floor); a check rom
# is correctness only.
ifdef R60PUNCH2
SHCCFLAGS += -DR60_PUNCH2
endif
ifdef R60PUNCH2CHECK
SHCCFLAGS += -DR60_PUNCH2CHECK
endif
# `R60PRIOMASK=1` (with R60PRIO, R60PLOT5) = the FG priority mask applied in the sprite plot (a
# cached line buffer, masked, copied out through the overwrite image) instead of the punch's
# framebuffer read-modify-write after it (sw263, the speed card).
ifdef R60PRIOMASK
SHCCFLAGS += -DR60_PRIOMASK
endif
# `R60PRIOINLINE=1` = the same mask applied inside the plotter's longword store (a per-row
# longword mask, no line buffer, no copy-out); implies the R60PRIOMASK bookkeeping (sw264).
ifdef R60PRIOINLINE
SHCCFLAGS += -DR60_PRIOMASK -DR60_PRIOINLINE
endif
ifdef R60SNDTEST
MDCCFLAGS += -DR60_SNDTEST=0x$(R60SNDTEST)
endif
ifdef R60SKIPPROBE
MDCCFLAGS += -DR60_SKIPPROBE
endif
ifdef R60PLOT5CHECK
SHCCFLAGS += -DR60_PLOT5CHECK
endif
# `R60ZOOMFAST=1` = zoomed sprite rows by source word through the PLOT5 pen table (the
# per-pixel zoom loop cost ~120 instructions a word; Neff's entrance smoke, 19 zoomed
# sprites, ran six vints a picture, 2026-10-23). `R60ZOOMCHECK=1` replays every zoomed
# row both ways into RAM and counts differing bytes (r60_zoom_bad / r60_zoom_rows).
ifdef R60ZOOMFAST
SHCCFLAGS += -DR60_ZOOMFAST
endif
ifdef R60ZOOMCHECK
SHCCFLAGS += -DR60_ZOOMCHECK
endif
# `R60RAMTEXTPEEK=1` = PROBE, NEVER RUN: links with the .ramtext cap raised into the art
# lists' slot so the .lst can be read for sizes when the slot overflows.
ifdef R60RAMTEXTPEEK
SHLDFLAGS += -Wl,--defsym,__ramtext_cap_override=0x0603A000
endif
ifdef R60PLOTSTATS
SHCCFLAGS += -DR60_PLOTSTATS
endif
ifdef R60PLOT4CHECK
SHCCFLAGS += -DR60_PLOT4CHECK
endif
# `R60EMPTYPKT=1` = the master flags a packet with nothing in it (bit 12); the 68K takes it
# without the walk (an empty A consume cost 8 68K lines of the span). 2026-10-17.
ifdef R60EMPTYPKT
SHCCFLAGS += -DR60_EMPTYPKT
MDCCFLAGS += -DR60_EMPTYPKT
endif
ifdef R60SPANPROBE
MDCCFLAGS += -DR60_SPANPROBE
endif
ifdef R60FSPROBE
MDCCFLAGS += -DR60_FSPROBE
endif
ifdef R60TXTPOOL
SHCCFLAGS += -DR60_TXTPOOL
endif
ifdef R60KEEPSTALE
SHCCFLAGS += -DR60_KEEPSTALE
endif
ifdef R60CHURNCAP
SHCCFLAGS += -DR60_CHURNCAP=$(R60CHURNCAP)u -DR60_CHURNMIN=$(if $(R60CHURNMIN),$(R60CHURNMIN),100)u
endif
ifdef R60FILLCLR
SHCCFLAGS += -DR60_FILLCLR
endif
ifdef R60CRAMDELTA
SHCCFLAGS += -DR60_CRAMDELTA
endif
# `R60CRAMFLOOD=1` = PROBE ONLY: every 32X CRAM entry forced bright, so a sprite that
# is drawn but invisible can be told from one that is not drawn.
ifdef R60CRAMFLOOD
SHCCFLAGS += -DR60_CRAMFLOOD
endif
# `R60RBPROBE=1` = PROBE ONLY: the 68K reads the framebuffer bank it can see at FM=0 and
# reports what it found as the 32X background colour (green = the slave's writes land,
# yellow = partly, red = not at all). The one question the board can be asked directly.
# `R60BARSFIRST=1` = PROBE: draw the instrument bars BEFORE the sprite loop instead of
# after it, to test whether the board loses writes by their position in the pass.
# `R60FZ=1|2` = PROBE: the master stops issuing GO once the 68K has posted 8 times in
# 64 vints (1), or once the span has hit the backstop 8 times in 64 vints (2). The
# picture then FREEZES. Bar instruments are dead on the board, so the probe answers by
# stopping -- change-over-time is the one channel the board has been shown to carry.
ifdef R60FZ
SHCCFLAGS += -DR60_FZ=$(R60FZ)
endif
# `R60PLANES=1` = the MD background plane: the slot allocator carried from native1
# (m_main.c:19180-19280) with a new walk, two packets a frame (type 0 art, type 1 name
# table), seven plane rows each so a 28-row screen refreshes in four frames.
ifdef R60PLANES
SHCCFLAGS += -DR60_PLANES
endif
ifdef R60BARSFIRST
SHCCFLAGS += -DR60_BARSFIRST
endif
ifdef R60RBPROBE
SHCCFLAGS += -DR60_RB_PROBE
MDCCFLAGS += -DR60_RB_PROBE
endif
# `GOVERNOR=n` = the REBUILD pacing divider (section 2): 2 for bring-up (clean 30 Hz),
# 1 is the ship state. A divider on the same identical frame, not a phase machine.
ifdef GOVERNOR
SHCCFLAGS += -DR60_GOVERNOR=$(GOVERNOR)
endif
# `R60PKTPROBE=1` = PROBE ONLY: md_consume records, per buffer and unconditionally,
# what it actually reads at the packet's first word. 0xFFA020 cannot answer that --
# both consume calls write it and B runs second.
ifdef R60PKTPROBE
MDCCFLAGS += -DR60_PKT_PROBE
endif
endif

# `make line NOSPRBAKE=1 PORTFULL=1` = RUN THE DECOMPILED 68K GAME ON THE SH-2
# (docs/design/FULL-PORT.md 12). The port is 351 functions reached from the frame
# body plus the object table, and it needs the cart window .sprbake occupies --
# hence NOSPRBAKE=1, which SHIP_US honours the same way it honours NOGLOW.
#
#   sh_src/portfull/   generated: tools/cport_build.sh + cport_layout.py +
#                      cport_dispatch.py. NOT hand-edited, NOT committed as source.
#   sh_src/port_l2.c   the L2 driver and the hand-written object dispatcher.
#   layout.ld          places the 68K's absolute symbols at real addresses:
#                      work RAM 0x0602CC00 (the proven .portmirror slot), MMIO
#                      stubs 0x06019000, program ROM 0x02340000. Symbol
#                      assignments only, no SECTIONS, so it composes with mars.ld.
#
# The two 16 KB captures ride .portdata; port_l2_data.o is filtered out of every
# other build because the wildcard above would otherwise put 32 KB of oracle
# capture in the shipping rom.
ifdef PORTFULL
SHCCFLAGS += -DPORT_FULL
SHOBJS += sh_src/portfull/port.o sh_src/portfull/objdispatch.o \
          sh_src/portfull/defs.o sh_src/portfull/l2seed.o sh_src/portfull/dispregs.o
# the program image rides sh_src/port_rom_data.s (.incbin), NOT the objcopy-made
# romimg.o -- that produced an EMPTY section, see the comment in that .s file
SHLDFLAGS += -T sh_src/portfull/layout.ld
# __portfull moves .tilesmd (and .porttest) up 8 KB for THIS BUILD ONLY: the port's
# driver pushes .ramtext's ROM image past the blob's old cart address, and the
# accepted line must stay byte-identical. sh_src/mars.ld says why in full.
SHLDFLAGS += -Wl,--defsym,__portfull=1
ifdef PORTHOT
ifneq ($(strip $(CPORT_HOT)),)
# AFTER the SHLDFLAGS assignment at :1707, which would otherwise discard this. Only the
# CPORT_HOT set needs the cap raised; the dispatcher alone fits the original slot.
SHLDFLAGS += -Wl,--defsym,__ramtext_cap_override=0x06039000
endif
endif
# THE MD SIDE MUST SEE PORT_FULL TOO. md_main.c picks TILESMD_CART 0x26C000 under
# PORT_FULL and 0x26A000 otherwise; with the define only in SHCCFLAGS the 68K
# fetched art 8 KB below the blob (rd1, 2026-10-04: the compiled bytes carried
# `addal #0x96AA00`, not 0x96CA00). tools/check_tilesmd_addr.py reads the SOURCE and
# cannot see which branch was compiled; `make tilesmd-bytes` checks the binary.
MDCCFLAGS += -DPORT_FULL
else
SHOBJS := $(filter-out sh_src/port_l2.o sh_src/port_l2_data.o sh_src/port_l2_frames.o sh_src/port_l2_io.o sh_src/port_rom_data.o,$(SHOBJS))
endif


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
# `make PCSAMP=1 PCSAMPN=15` (2026-09-24) samples every 16 lines instead of
# every 56: 13 samples a vint, a finer grid against the pass's fixed order
# (at 3 fixed lines the same PCs recur). N+1 must divide 224.
# RIGSPAN=18 (2026-09-24): the sampler's class histogram over the barcode; needs the 16-line sampler.
ifeq ($(RIGSPAN),18)
PCSAMP = 1
PCSAMPN ?= 15
endif
PCSAMPN ?= 55
ifdef PCSAMP
MDASFLAGS += -Wa,--defsym,PC_SAMP=1 -Wa,--defsym,PC_SAMP_N=$(PCSAMPN)
MDCCFLAGS += -DPC_SAMP -DPC_SAMP_N=$(PCSAMPN)
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
ifndef FOLD5
SHCCFLAGS += -DFB_TEXT_READ
MDCCFLAGS += -DFB_TEXT_READ
endif
endif
# `make ship-us ... FOLD5=1` = NOTES 62/63, the text path. Takes the
# game's text writes OFF the framebuffer and back onto the 0xFF8000 WRAM
# mirror, where the r60 packet ships them like everything else. Negates
# FBTEXT on BOTH sides (the -DFB_TEXT_READ above and the FBTEXT=... handed
# to patch_game below, whose remap() then sends the whole 0x410000 text
# page to 0xFF8000 instead of splitting glyphs to FB 0x85F000).
#
# What it removes, all three at once (LOOP29 266b-c, 268):
#   - the master's 928-longword FB_TEXT -> TEXT_U snapshot inside the flip
#     guard's window (~830 FRT ticks of the ~1650 budget; the ablation ran
#     18 -> 31 presented frames per 64 vints);
#   - the post-flip text RESTORE into the fresh draw bank (text is sparse,
#     so an in-place FB copy needs one; a single WRAM copy does not);
#   - COMM2's high-byte row mask from the text path entirely, which is the
#     race card L died on (NOTES 62).
# The DREQ text chunks and the 80-word regs/rowscroll prefix come back on
# (they were killed by LOOP 20, and every #ifndef FB_TEXT_READ arm in
# md_main.c/m_main.c is that path, intact). Full text refresh is 4 vints
# at WIN_TWO's two 256-word chunks a packet; a dirty-group selector is
# step 2, not step 1 -- step 1 is the transport, measured alone.
ifdef FOLD5
SHCCFLAGS += -DFOLD5_TEXT_WRAM
MDCCFLAGS += -DFOLD5_TEXT_WRAM
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
# `make ... MISSKEEP=1` = LOOP-DECOMPILE 68. PROBE: nops the clr.w at
# 0x930 so the game stops wiping its own missed-frame counter at
# 0xFFF144. A zero reading on the shipping build is not evidence of zero
# misses; this makes the counter mean what it says.
# `make ... SCENESEL=N` = LOOP-DECOMPILE 66. PROBE: forces every round to
# load scene N (0-4) by rewriting the round->scene table at 0x1CDA. Lets
# the later scenes be measured without playing to them — no input script
# reaches past scene 0. Tile/palette measurement only; the actors are
# still the round's, so it is NOT valid for a sprite census.
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
# `make ... MDSNOFLUSH=1` = 2026-09-17. mds_flush() wipes ALL 1024 md_tag
# entries at every display-gate close. Measured: 1824 live tags over 3
# flushes in 3000 gameplay frames against 2767 total claims -- two thirds
# of all claims are re-claims forced by it, and each re-claim re-ships art
# at 24/batch, which is the noslot/dirty window the rig sees as black
# tiles. Over-broad by mds_install's own invariant (a slot is stale iff
# its set's line or pen map changed): mds_install's changed[] wiped 2 tags
# where the flush wiped 1824. This drops the blanket wipe.
# `make ... TILESMD=1` = 2026-09-17. Every tile is baked to MD 4bpp with
# its round's pen map already applied (tools/bake_tiles_md.py), so
# md_emit_art becomes a 32-byte copy instead of 64 ROM reads + 64 table
# lookups + 32 writes per tile per ship. Proven byte-identical: 941/941
# live VRAM slots re-emitted through the tool's port of the C loop.
# 425KB into the measured 593KB free gap at 0x263C00 (mars.ld places it
# explicitly and asserts on overrun).
# NOTILESMD=1 drops it again, the way NOSPRBAKE drops the sprite blob. The PORTFULL
# probe needs it: the blob is 519 KB and .tilesmd's load address is FIXED at
# 0x26A000, so .ramtext's ROM image -- which follows .data, and therefore moves with
# every byte of .text -- runs into it. Measured 2026-10-01: the port's generated unit
# plus the 398 register thunks pushed .ramtext 0x77B past 0x26A000 and the link
# failed with ".ramtext ROM image reached .tilesmd". Moving .tilesmd instead would
# mean changing the four hardcoded 0x26A000 references in md_src/md_main.c; dropping
# a blob this probe never reads costs nothing.
ifdef TILESMD
ifndef NOTILESMD
SHCCFLAGS  += -DTILES_MD
SHASFLAGS  += --defsym TILES_MD=1
SHOBJS_EXTRA += sh_src/tiles_md_data.o
endif
endif
ifdef MDSNOFLUSH
SHCCFLAGS += -DMDS_NOFLUSH
endif
# `make ... MDALLOCWHY=1` = LOOP29 153: the MD residency allocator's own
# counters, 16 longs at 0x3A680 (see MDALLOC in m_main.c). Entry 152
# could not tell "the cell never claimed a slot" from "it claimed and
# something wiped it" because DIAG[39]/[50]/[53] are each written by
# three subsystems including the DREQ landing path. Read with
# tools/md_alloc_why.py. PROBE ONLY (collides with SPRLINE).
# `make ... MDALLOCWHY=1 MDAWATCH=41` aims mdalloc_id[] at a specific
# colour set (default 33, LOOP29 158). The watch arms once, at the first
# mdp_free_set of that set, and records: [0] cset [1] its MD line [2] the
# owner set it conflicts with [3] on-screen cells naming its slots
# [4] tiles wiped, then 16 tile codes. That is how you find out whether a
# freed set owned anything VISIBLE at the moment it was freed.
# `make ... NTHASH=1` = 2026-09-17. The name table takes 8 of the 9
# transport phases and re-ships all 2240 cells every rotation, while the
# tile art the rig census says the black cells are waiting on gets the
# other 1. Skip the SHIP of a chunk whose payload is byte-identical to the
# last one shipped for that chunk (sc[5] = 0; the consumer's loop is
# `i < cnt` so zero is a no-op and the header still lands).
# NOT NT_SKIP: the walk always runs, so mdp_s_stmp stays fresh and the
# evict gate is untouched, and the decision is a byte compare of what the
# walk produced rather than a proxy key that omits allocator state.
# NTHASHMAXAGE=n forces a ship every n visits (default 32).
# Counters: DIAG[32] shipped, DIAG[33] skipped.
ifdef NTHASH
SHCCFLAGS += -DNT_HASH
endif
ifdef NTHASHMAXAGE
SHCCFLAGS += -DNT_HASH_MAXAGE=$(NTHASHMAXAGE)
endif
ifdef MDAWATCH
SHCCFLAGS += -DMDA_WATCH=$(MDAWATCH)
endif
ifdef MDALLOCWHY
SHCCFLAGS += -DMD_ALLOC_WHY
endif
# `make ... DRIFTMEAS=1` = LOOP29 154: do not free a colour set on the
# co-owner palette drift, only count it. 153 measured that EVERY md_tag
# relocation in a 4000-frame run is that one path (LRU frees: zero) and
# that it destroys resident tiles exactly as fast as the name-table walk
# claims them. This is the A/B that says whether the background misses
# are that loop. COLOUR IS WRONG on the drifted cells by construction --
# it is a measurement, never a ship.
ifdef DRIFTMEAS
SHCCFLAGS += -DDRIFT_MEASURE_ONLY
endif
# `make ... DRIFTVOL=1` = LOOP29 154, the shippable half of DRIFTMEAS.
# mdp_claim_pen has always preferred an EXCLUSIVE pen for a set with
# mdp_s_vol >= 2, and mdp_s_vol was never incremented anywhere -- zeroed
# at boot and at every scene install, read once, dead. A colour-set drift
# is the event that means "this set must stop sharing a pen", so count it
# there. Unlike DRIFTMEAS this does not make colour worse: an exclusive
# pen shows the set's OWN colour. Gate: mdalloc relocations, tag
# residency, and a play pass for background colour.
ifdef DRIFTVOL
SHCCFLAGS += -DDRIFT_VOL
endif
# `make ... TAGKEEP=1` = LOOP29 155. A tile's shipped pattern depends on
# its colour set's (line, pixel->pen map) and NOT on the pen colours, so
# mdp_free_set's wipe of every md_tag entry carrying the set is needed
# only if the re-assign actually moves it. Defer the wipe to the
# re-assign and compare. mdalloc [22] = resolved identical (wipe
# skipped), [23] = moved (wiped then).
ifdef TAGKEEP
SHCCFLAGS += -DTAGKEEP
endif
# `make ... PENHOLD=1` = LOOP29 158, needs TAGKEEP. mdp_free_set releases
# the set's CRAM pens; another set takes them; TAGKEEP's re-assign then
# cannot land where it was (4 of 16). Keep the refcount so the pens stay
# reserved across the free/re-assign gap. PROBE: a set that never comes
# back leaks its pens until a release timeout exists.
ifdef PENHOLD
SHCCFLAGS += -DPEN_HOLD
endif
# `make ... PENREPAINT=1` = LOOP29 164, needs TAGKEEP+PENHOLD. The
# churning sets FADE, so when one comes back its colour no longer
# matches the pen it left and the old placement is rejected even though
# the pen is still exclusively ours. A tile's pattern depends on the pen
# INDEX, not its colour: repaint the pen and keep the index.
ifdef PENREPAINT
SHCCFLAGS += -DPEN_REPAINT
endif
# IDLEOFF=1 (2026-09-24 A/B, never a ship): the master builds no MD packets at all
# (tile batch + NT chunk skipped, their cart reads with them). With NOMAPS=1 the
# master's idle loop is nearly empty: does the 68K's pass shorten on the FPGA?
ifdef IDLEOFF
SHCCFLAGS += -DIDLE_OFF
endif
# `make ... NOMAPS=1` = LOOP29 168 ablation. build_maps_chunk returns
# "done" instantly, so the master's maps drain costs nothing. RENDERS
# WRONG BY CONSTRUCTION -- the only meaningful output is the generation
# wall from tools/nat_score.py. Decomposes a 1.57-vint wall against a
# 1.09 slave compose and a 0.44 master drain. NEVER SHIP.
ifdef NOMAPS
SHCCFLAGS += -DNO_MAPS
endif
# `make ... NOCLEAR=1` = LOOP29 169 ablation. Skip the per-row FB clear
# entirely. RENDERS WRONG (last frame's pixels persist); the only
# meaningful output is the generation wall. NEVER SHIP.
ifdef NOCLEAR
SHCCFLAGS += -DNO_CLEAR
endif
# `make ... NTSKIP=1` = LOOP29 170. Skip the name-table walk for a row
# whose inputs have not changed. 169 measured the walk's answer identical
# in 56 of 56 rows at five points in play (MD_BG scrolls with the VDP's
# own hscroll, so a scroll never rewrites the table), and the master's
# maps drain is worth 0.29 of the 1.60-vint generation wall. Respects the
# four hazards in 169: md_ref is re-stamped from the mirror, the skip
# sits after the NT_WRAP shift and before the CAT1_PEND clear, and
# cap_page bumps a tilemap generation so any tile write invalidates
# every row key. Gate: the arcade pixel diff, and NTS skip/walk counts.
ifdef NTSKIP
SHCCFLAGS += -DNT_SKIP
endif
# `make ... NTKEY8=1` = key the name-table row memo on vxr>>3 instead of
# the full 10-bit vxr, so a sub-tile camera move does not re-key a row
# whose cells are unchanged. Needs NTSKIP. PIXEL-GATE IT: the sub-tile
# bits drive the mirror shift (hazard 2).
ifdef NTKEY8
SHCCFLAGS += -DNT_KEY8
endif
# `make ... NBUILD1=1` = one MD packet build per gap instead of two.
# Brake for NTSKIP's transport flood (LOOP29 170).
ifdef NBUILD1
SHCCFLAGS += -DNBUILD1
endif
# `make ... NTMAXAGE=n` = LOOP29 171. How many windows a name-table row
# may go unwalked before NTSKIP forces a walk regardless of its key.
# The skip trusts cap_page's content compare; on the FPGA that trust is
# not earned, and an unbounded skip renders missing tiles there while
# reading clean in ares. Default 16.
ifdef NTMAXAGE
SHCCFLAGS += -DNT_MAXAGE=$(NTMAXAGE)
endif
# `make ... C1NOFB=1` = LOOP29 175 / PLAN-TILES-TO-VDP step 3. Needs
# CAT1MD. Removes the FB cat-1 pass entirely so MD plane A's priority
# bit carries cat1 on its own. Sprites then wrongly cover cat1 where
# they overlap, because the 32X layer wins per pixel wherever the FB
# wrote one -- so this is the step-3 MEASUREMENT and not a ship. Read
# the percentage of single-vint frames.
# `make line ... C1FB=1` = RESTORE the FB cat-1 pass on the line (2026-09-27).
# C1NOFB's own comment above says it "is the step-3 MEASUREMENT and not a ship",
# and it is in LINE_FLAGS -- which is why a cat-1 cell refused a VRAM slot has
# NOTHING drawing it and shows as Mike's black tiles (LESSONS 12:20; the refusal
# rule at m_main.c:17700 relies on "the FB keeps the cell instead", and C1_NOFB
# removed the pass that would). This overrides it from inside `make line`, because
# hand-reconstructing LINE_FLAGS to drop a flag hits the documented reduced--D trap
# (CLAUDE.md: bare `make ship-us` "is NOT the line"; it failed with md_round
# undeclared exactly as warned).
# `make line ... PORTTEST=1` = RUN DECOMPILED 68K CODE ON THE SH-2 (2026-09-27).
# sh_src/port_selftest.c is GENERATED by tools/gen_selftest.py: decompiled 68K
# functions exactly as Ghidra emitted them, checked against the arcade oracle's
# own vectors. Verified on ares and on the MiSTer FPGA core -- NOT real silicon.
# Results in port_test_out[]; the symbol address comes from rom/s16.elf, and the
# rig reads the pass count from barcode byte 5 (rig_barcode.py --mode porttest).
#
# -fno-strict-aliasing IS REQUIRED, not hygiene. The decompiled bodies type-pun
# freely -- the case setup stores a cell through `volatile ushort *` and the body
# loads the same cell through `int *` -- which is a strict-aliasing violation, so
# at -O2 gcc may assume they do not alias and fold the load to the zero that
# wram_arm() wrote. Measured 2026-09-27: FUN_00003f32 is CORRECT (all four oracle
# vectors verified by hand) and reported 3 of 4 FAIL with the read-back showing
# base == 0, while its first vector passed -- the same code, folded at one call
# site and not another.
ifdef PORTTEST
SHCCFLAGS += -DPORT_TEST -fno-strict-aliasing
endif
# `make line ... PORTTEST=1 RIGBARCODE=1 PORTTIME=1` = PRICE the decompiled code
# instead of grading it. Barcode byte 5 stops carrying the pass count and carries
# the sweep's FRT cost >> 3 instead (tools/porttest_read.py reads the full 16-bit
# value out of an ares dump; the rig only has one reliable byte). ares reads
# 669-769 us for 99 calls = 4.0-4.6% of a vint, but ares charges INSTRUCTION
# CYCLES ONLY -- the FPGA's excess over it is the memory-traffic cost of running
# 68K code over a 16 KB working set, which is SILICON.md 6b and the thing that
# decides whether the game logic fits in a vint. Build BOTH roms: without
# PORTTIME to prove the code correct, with it to price it.
ifdef PORTTIME
SHCCFLAGS += -DPORTTIME
ifdef PORTTIMESHIFT
SHCCFLAGS += -DPORTTIME_SHIFT=$(PORTTIMESHIFT)
endif
endif
ifdef C1NOFB
ifndef C1FB
SHCCFLAGS += -DC1_NOFB
endif
endif
# `make ... MDSPROFF=1` = LOOP29 176. Drop the MD-VDP sprite offload.
# PLAN-TILES-TO-VDP's palette pack uses ALL FOUR MD CRAM lines for the
# tile layers, which leaves none for sprites -- and that is consistent,
# because with tiles on the VDP the sprites stay in the framebuffer and
# need no MD palette line at all. MDSPR claims ~1 record of 37 anyway
# (LOOP29 119). Wins back the line the pack needs.
# `make ... MDATTRACTSPLIT=1` = the attract's two tables, keyed on the game's own scene byte
# 0xFFF022 (0x0C static / 0x09 demos). Needs a pal_rounds_md.h that defines
# MDR_ATTRACT_TABLE -- tools/round_split.py --emit writes it. See m_main.c's
# md_attract_table(): without it the whole attract runs as round 0 on one table and every
# static-half set is refused a line and drawn as backdrop (the eye's iris).
ifdef MDATTRACTSPLIT
SHCCFLAGS += -DMD_ATTRACT_SPLIT
endif
ifdef MDLINES4
SHCCFLAGS += -DMDP_LINES4
# AND THE 68K SIDE, 2026-10-10. Without this the SH-2 publishes a fourth line and the 68K
# never consumes it: md_consume's fourth-line DMA is inside #ifdef MDP_LINES4 and MDCCFLAGS
# carried no such define, so build/port/.build_flags had MDP_LINES4 on the SH-2 list only
# and VDP CRAM line 0 read zero in every dump. The 4f69372d hunk did not carry this line.
MDCCFLAGS += -DMDP_LINES4
endif
# `make ... MDSREFUSE=1` = LOOP29 190. A tile colour set that is not in
# the scene's baked table is REFUSED an MD line instead of being handed
# to the dynamic allocator. 189 measured that pinning MORE sets makes the
# churn worse, because everything outside the table still goes dynamic
# and the lines fill up. Needs MDSTATIC. Refused cells currently render
# as backdrop -- the framebuffer fallback for BG/FG-cat0 does not exist
# yet -- so this is a measurement first: read mdalloc [30] (refusals)
# against the churn, and look at a frame.
ifdef MDSREFUSE
SHCCFLAGS += -DMDS_REFUSE
endif
# `make ... MDROUND=1` = LOOP29 193. Drive the static MD pen tables from
# the GAME'S ROUND (its own scene variable, published by the 68K in
# COMM10 bits 13-15) instead of PALSTATIC's palette-DETECTED scene. The
# detected space has two slots for a five-round game, which is why
# MDSREFUSE blanked level 2 and the transition (191). Tables come from
# sh_src/pal_rounds_md.h, baked from each round's own gated dumps (192).
# Needs MDSTATIC. Pair with MDSREFUSE=1 for the zero-churn result.
ifdef MDROUND
SHCCFLAGS += -DMD_ROUND
MDCCFLAGS += -DMD_ROUND
endif
# `make ... GENSKIP=1` = LOOP29 178. Do not launch a generation whose
# INPUT is byte-identical to the last one launched: the game writes no
# scroll and no sprite upload on a vint it did not advance, so the frame
# would be identical to the one on screen. Hash is the staged sprite
# list plus the latched layer regs plus per-page tilemap generations,
# 298 longs against a 0.6-vint compose. GENMAXAGE=n (default 8) forces a
# launch that many windows on regardless, bounding anything the hash does
# not cover. Read GNS at 0x28FE0: [0] skipped [1] launched.
ifdef GENSKIP
SHCCFLAGS += -DGEN_SKIP
endif
ifdef GENMAXAGE
SHCCFLAGS += -DGEN_MAXAGE=$(GENMAXAGE)
endif
# `make ... GATEFREE=1` = LOOP29 181. GAMEGATE grants the game's frame
# release inside the window path, so it can only fire on a window vint
# and the game tracks the WINDOW cadence, not vblank -- GAMEGATEWAIT=1
# measured the game at 50% of vints for this reason. GATEFREE also
# releases on a vint with no window, so the game's logic and input run at
# 60 Hz regardless of the display rate. Expect MORE tearing (the game
# writes its staging twice as often); expect game-frames/vints to go to
# ~100%. Gate: gameplay_speed's scene-timer rate, and Mike's hands.
ifdef GATEFREE
MDCCFLAGS += -DGATE_FREE
endif
# `make ... MISSKEEP=1` = LOOP29 182. NOP the game's own clear of its
# missed-frame counter at 0x930 so 0xFFF144 accumulates. Every 0.0%
# this repo has read off that address was taken with the instrument
# being reset by the main loop (LOOP-DECOMPILE 67). MEASUREMENT ONLY.
ifdef MISSKEEP
export MISSKEEP
endif
# `make ... RELBANK=n` = LOOP29 183. The game's frame wait CLEARS its
# release byte before spinning, so a release that arrived while it was
# working is discarded and a pass taking slightly over one vint costs
# exactly two -- the 50.2% every build has measured
# (LOOP-DECOMPILE 67). Replaces the clear with a decrement, capped at n
# (default 1), so the game can honour a banked release and run at its own
# rate. Gate: gameplay_speed's scene-timer rate, and a play pass -- this
# changes the game's frame pacing, so it is Mike's call.
ifdef RELBANK
export RELBANK
endif
# `make ... PASSCOUNT=1` = LOOP29 185. One tick per GAMEPLAY frame at
# 0xFFA0EC, counted at the main loop's own wait call (0x922) rather than
# in the shared wait routine. The only trustworthy game-rate instrument
# in this repo; see the patcher comment for why the other three are not.
ifdef PASSCOUNT
export PASSCOUNT
endif
# `make ... FRAMEDONE=1` = LOOP29 186 / LOOP-DECOMPILE 70. The game knows
# exactly when its frame is complete -- the instant its main loop reaches
# its wait -- and the pipeline has been inferring it from windows and
# phases, with 0.42 of the echo phase spent waiting on a launch it could
# have had earlier. A thunk at the gameplay loop's own wait call raises
# COMM10 bit 15, and the master launches on that. Needs the SH-2 side:
# FRAMEDONE=1 sets both. FRAMEDONEWAIT=n (default 3) is the timeout for
# scenes whose wait is not the gameplay loop's.
# LATENCY patch, not throughput: the slave does the same work, sooner.
ifdef FRAMEDONE
export FRAMEDONE
SHCCFLAGS += -DFRAME_DONE
# LOOP29 187: the MD side needs it too, and I shipped a build with it on
# the SH-2 ONLY -- so the thunk had no `ori` and the publishes did not
# preserve the bit. It measured 0% told, which was my flag plumbing and
# not the idea.
MDCCFLAGS += -DFRAME_DONE
endif
ifdef FRAMEDONEWAIT
SHCCFLAGS += -DFRAMEDONE_MAXWAIT=$(FRAMEDONEWAIT)
endif
# `make ... PALAPOST=1` = LOOP29 166, a PATCHER change (no SH-2 flag).
# The colour cycler's dirty mark (PAL_THUNK_A, 0x30C2) fires BEFORE its
# four stores at 0x30F8, so a consume landing in that gap ships the old
# colours and clears the bit -- the SH-2 mirror then holds the PREVIOUS
# rotation step for ever. Measured stale on colour sets 19, 20 and 21 at
# every sampled frame, which is ~8% of level 1's tilemap cells painted
# one cycle behind (LOOP29 165). This adds a second mark AFTER the
# stores, at 0x3100, recovering the block from A1.
# Gate: tools diff of WRAM 0xFF9000 against SDRAM 0x27000 must go to
# zero mismatches. A cycling palette is invisible in a still, so there
# is no pixel gate for this -- only the memory diff and Mike's eye.
ifdef PALAPOST
export PALAPOST
endif
# `make ... DRIFTTOL=n` = LOOP29 160: the squared-colour-distance at which
# a co-owner set is declared drifted and FREED (default 18, both sites).
# The free destroys ~46 tile slots, 95% of them on screen (156), so this
# constant trades attract-screen colour against background stability.
ifdef DRIFTTOL
SHCCFLAGS += -DDRIFT_TOL=$(DRIFTTOL)
endif
ifdef PHASECENSUS
SHCCFLAGS += -DPHASE_CENSUS
endif
# `make ... PHASECENSUS=1 GENRING=1` = THE GENERATION STAMP RING (2026-10-12, P2's
# yield gate). PHASECENSUS reports its six events as SUMS and MAXES in FRT ticks, and
# a mean cannot answer the one question a one-vint frame turns on: what SHARE of
# generations CLOSE inside the vint they launched in. GENRING records the V-ISR vint
# number (DIAG[49]) at each of launch / slave echo / master tail / close / ship / flip,
# per generation, 64 entries at 0x38000 (m_main.c GR). Read with tools/gen_ring.py.
# PROBE-ONLY: it shares 0x38000 with PORT_RIG and ROWSHIP_PROBE and the build fails
# hard rather than let either corrupt it. Needs PHASECENSUS -- the stamps are its.
ifdef GENRING
SHCCFLAGS += -DGEN_RING
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
# R60TIGHT=1: r60_push's two equality scans (palette pre-scan, rowscroll
# compare) as cmpm.l/dbne and the lost-push belt copy as longs. Same
# packet, fewer 68K instructions (LOOP-DECOMPILE 97). R60TIGHTCHECK=1 is
# the probe: runs the C scans as well, ships on the C result, and counts
# disagreements at WRAM 0xFFA1B0 (pal) / 0xFFA1B2 (rowscroll); 0xFFA1B4
# counts pre-scans.
ifdef R60TIGHT
MDCCFLAGS += -DR60_TIGHT
endif
ifdef R60TIGHTCHECK
MDCCFLAGS += -DR60_TIGHT -DR60_TIGHT_CHECK
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
# FBXECHO=1 = 2026-09-21, the level-start race. The master echoes the
# sequence of the last 68K packet it lifted in its own packet header
# (word 5 bits 12-15); the 68K re-blasts a packet not echoed two vints
# after its blast (the packet sat in the bank the master does not read).
# Boot-storm packet 2 -- palette blocks 15-29, the level-1 palette the
# game preloads -- was lost that way on the losing layouts (ares,
# docs/design/RIG-READOUT.md).
ifdef FBXECHO
SHCCFLAGS += -DFBX_ECHO
MDCCFLAGS += -DFBX_ECHO
endif
# C1RTALL=1 = 2026-09-21: classify every priority (cat-1) cell from its
# tile -- the scene's baked mask by code, else a mask from the art --
# instead of cat1hole's per-cell class baked from gameplay (the attract's
# level-2 demo put rocks where the bake had none: black cells).
ifdef C1RTALL
SHCCFLAGS += -DC1_RT_ALL
endif
# C1RTN=<slots> = 2026-09-22: size of the runtime cat-1 mask cache (default
# 64). The eye's pupil notch was this cache thrashing on a harvested
# scene's ~500 priority codes; 1024 is collision-free for the eye.
ifdef C1RTN
SHCCFLAGS += -DC1_RT_N=$(C1RTN)
endif
# BGBOTTOM=1 = 2026-09-21: the BG bottom-band backstop (2026-08-25) blanks
# a BG cell in view rows 24-27 only where the FG tile over it is fully
# OPAQUE; level 2's foreground has holes there and the arcade shows the
# floor through them (black cells around the rocks).
# `make ... C1REFUSE_BG=1` = MIKE'S LEVEL-1 BLACK TILES (2026-09-27, the first fix
# attempt since the 09-25 diagnosis). The bottom-band backstop blanks a BG cell
# where the FG tile over it is fully OPAQUE -- but that asks about the ART, never
# about whether the FG cell GOT an MD slot, and the slot-pressure rule (MDA(4),
# vage<12) refuses a cat-1 cell whose hash set is full of hot ways. Opaque FG not
# drawn + BG blanked under it = both layers blank = black. Gameplay runs 1018/1024
# slots and 123/128 sets full where attract runs 499 and 0, which is why the
# attract-window census honestly read 0. A 140-byte per-cell bitmap records the
# refusal; the backstop consults it. BG composes BEFORE FG so it reads one frame
# late, which is in time for a defect that persists for seconds.
ifdef C1REFUSEBG
SHCCFLAGS += -DC1REFUSE_BG
endif
ifdef BGBOTTOM
SHCCFLAGS += -DBG_BOTTOM_HOLES
endif
# NTWIPEGEN=1 = 2026-09-21: bump the name-table walk generation whenever
# slot tags are wiped (install, set free, round change) so rows naming
# wiped slots re-walk at their next visit instead of after NT_MAXAGE
# (the eye scene's stale patches).
ifdef NTWIPEGEN
SHCCFLAGS += -DNT_WIPE_GEN
endif
# MDSREMARK=1 = 2026-09-21: a scene install re-ships (marks dirty) the
# slots of sets whose pen map changed instead of wiping their tags; cells
# naming a wiped slot showed the next claimant's art until re-walked (the
# eye scene's stale patches).
ifdef MDSREMARK
SHCCFLAGS += -DMDS_REMARK
endif
# OFFSCREENREMARK=1 = 2026-09-21: a set assigned while the round is off
# screen (every attract picture) has its slots RE-SHIPPED instead of its
# tags wiped (the eye scene's stale patches: ~540 wipes in 90 frames).
ifdef OFFSCREENREMARK
SHCCFLAGS += -DOFFSCREEN_REMARK
endif
# HOLDCOMPLETE=1 = 2026-09-21: the display hold releases only when every
# visible mirror cell names a claimed, shipped slot (cap HOLD_COMPLETE_MAX,
# 150); the eye picture used to show ~150 frames of churn garbage.
ifdef HOLDCOMPLETE
SHCCFLAGS += -DHOLD_COMPLETE
endif
# MDSCENES=1 = 2026-09-21: the MD tile tables are keyed by SCENE (rounds
# 0-4 plus the attract's splash/eye/picture/scores/transformation, baked
# from tools/attract_harvest.py dumps) instead of by round.
ifdef MDSCENES
SHCCFLAGS += -DMD_SCENES
endif
# HOLDFROMBLANK=1 = 2026-09-21: the display hold's settle rotations count
# from the game's blank, and a baked attract scene needs one rotation.
ifdef HOLDFROMBLANK
SHCCFLAGS += -DHOLD_FROM_BLANK
endif
# PALQUANT=1 = 2026-09-22: mdp_quant maps each 5-bit S16 channel to the
# NEAREST level of the MD's measured DAC ramp (0,52,87,116,144,172,206,
# 255) instead of +2>>2 in linear space; 16 of 32 inputs land one level
# darker, matching the arcade (LESSONS 2026-09-22, the eye's lid). A
# global colour change. Mike's rig A/B 2026-09-22 REJECTED it ("eye2.
# easily."): darker than the cabinet on the FPGA; the ramp it targets is
# ares's, unmeasured on hardware. Off. Re-pitch only with a rig ramp probe.
ifdef PALQUANT
SHCCFLAGS += -DPAL_QUANT_RAMP
# AND IT IS INCOMPATIBLE WITH THE BAKED TABLES UNTIL THE BAKER LEARNS THE RAMP
# (2026-09-27). tools/bake_tilecram.py:md() is the LINEAR +2>>2 quantiser, and
# MDROUND=1 makes its per-ROUND tables override the per-SCENE ones at runtime.
# So PALQUANT=1 + MDROUND=1 ships baked tables that disagree with the runtime
# on 16 of the 32 input levels -- measured by tools/quant_diff.py, which also
# confirms m_main.c:3103's own "sixteen move one level down". That is the
# pink-trees failure mode (LOOP29 196) re-armed: the SH-2's drift check
# compares a table colour against mdp_quant of the live word, so every
# disagreeing pen is freed and re-claimed for ever.
ifdef MDROUND
ifndef PALQUANT_OK
$(error PALQUANT=1 with MDROUND=1 ships baked tables that disagree with the \
runtime on 16 of 32 levels (tools/quant_diff.py). Teach bake_tilecram.py:md() \
the mdp_q5 ramp first, or pass PALQUANT_OK=1 if you are deliberately testing \
the mismatch)
endif
endif
endif
# CUTPREFETCH=1 = 2026-09-22: on the cut bit, before the game switches to
# pages 10/11, claim free slot ways for the cut's tiles from the staging
# tile RAM and ship their art under scene 9's table (bit 30 of md_tag),
# so the switch finds its art resident (LESSONS "cut+10 is already the
# switch": ~800 new tiles at the switch = the transition).
ifdef CUTPREFETCH
SHCCFLAGS += -DCUT_PREFETCH
endif
# EARLYREC=1 = 2026-09-23: the 68K pushes the sprite records + scroll regs
# over the DREQ FIFO at the game's IRQ4 exit; the master lands them and
# launches the compose from its idle loop, so the window is blit+publish.
ifdef EARLYREC
SHCCFLAGS += -DEARLY_REC
MDCCFLAGS += -DEARLY_REC
export EARLYREC
endif
# ERECIRQ4=1 (with EARLYREC=1) = the push at the game's IRQ4 exit (records
# final at ~line 90) instead of only the idle loop; costs 68K lines there.
ifdef ERECIRQ4
MDASFLAGS += -Wa,--defsym,EREC_IRQ4=1
endif
# RIGGAME=1 (with RIGBARCODE=1) = barcode byte 5 carries the game's scene
# timer low byte (0xFFF02A): game frames per vint on the rig from two captures.
ifdef RIGGAME
MDCCFLAGS += -DRIG_GAME
endif
# RIGWIN=1 = rig probe (2026-09-24): barcode byte 5 = the raw V counter at
# the game's IRQ4 exit (ring 0xFFA300, previous vint) = window end + the
# 1-11 line handler; byte 6 = raw V at the shim's vblank entry (0xFFA380).
# 68K only. Excludes RIGGAME/RIGBLIT on bytes 5-6.
ifdef RIGWIN
MDCCFLAGS += -DRIG_WIN
export RIGWIN
endif
# GATESLOW=1 (2026-09-24): the shared FM gate spin polls every ~17 us (24 nops)
# instead of ~4 us: less 68K bus pressure on the adapter while the master blits.
ifdef GATESLOW
export GATESLOW
endif
# UPLOADSKIP=1 (2026-09-24 A/B, patcher): the game's IRQ4 skips its uploads on
# odd vints (jump to the handler's tail); the frame flag still advances.
ifdef UPLOADSKIP
export UPLOADSKIP
endif
# SPRSKIP=1 (A/B, patcher): the game's IRQ4 skips only the sprite RAM copy on odd vints.
ifdef SPRSKIP
export SPRSKIP
endif
# RIGSPAN=1 (2026-09-24): the game's own frame span. The patcher's idle-loop thunk
# (RIGSPANHOOK) calls span_hook; barcode byte 5 = V at the pass end, byte 6 = vints spanned.
# RIGSPAN=2: byte 5 = the highest raw V at which a gate of THIS pass was released (the
#   patcher's RIGWINMAX stamp: max over the pass's FM waits; 0 = it never waited),
#   byte 6 = vints spanned. The (ack, span) pairing.
# RIGSPAN=3: the span histogram per attract step (reset when round/step 0xFFF142/0xFFF031 change):
#   byte 5 = frames of span >= 2 / 2, byte 6 = frames of span 1 / 2 (each saturates at 511).
# RIGSPAN=4: as 3 but byte 6 = frames of span 0 / 2 (one-vint frames).
# RIGSPAN=5: byte 5 = vints inside passes / 4, byte 6 = vints waiting / 4 (shim_vblank).
# RIGSPAN=6: the interrupted-PC ring (64 vints) classified: byte 5 = vints in OUR 68K code
#   (WRAM shim + thunks), byte 6 = vints in the game idle loop; the rest = game code.
# RIGSPAN=7: bytes 5-6 = the 68K PC the IRQ4 interrupted this vint (low 16 bits).
# RIGSPAN=8: byte 5 = wait-loop arrivals in the last fully idle vint / 2 (a 68K speed gauge:
#   cart fetch of the jsr/beq + the WRAM thunk), byte 6 = one-vint passes / 2. (never fires: the
#   game is released every vint it waits; kept for the record)
# RIGSPAN=9: every 64th vint the shim times 256 cart-window reads (byte 5) and a 256-
#   iteration WRAM loop (byte 6) in quarter-lines: the 68K speed gauge, ares vs FPGA.
# RIGSPAN=10: span-1 passes by end line in their 2nd vint: byte 5 = lines 0-130 / 2, byte 6 = 131-261 / 2.
# RIGSPAN=11: byte 5 = mean pass length in lines / 2, byte 6 = passes / 4 (per attract step).
# RIGSPAN=12 (with CHAINPRICE=1): byte 5 = chain entry -> shim end in lines, byte 6 = entry -> part B end.
# RIGSPAN=13: 256 FB word writes (byte 5) and reads (byte 6) in quarter-lines, every 64th vint.
# RIGSPAN=14: mode 9 run INSIDE the window (waits for raw V 0x40, +102): cart reads / WRAM loop.
# RIGSPAN=15 (with CARTBENCH=1): the same 512-iteration loop fetched from the cart (byte 5) and from WRAM (byte 6).
# RIGSPAN=16: chains entered with the game mid-pass / 2 (byte 5) vs with a fresh frame / 2 (byte 6), per step.
ifdef CARTBENCH
export CARTBENCH
endif
# PORTLOAD=1 (master) / =2 (slave), 2026-09-26: F0(c). A synthetic load of the
# translated game's shape -- 10,400 SH-2 instructions a vint against a 16 KB
# working set (4x the cache, so it exercises RESIDENCY, which FULL-PORT (b)
# says is the planning variable) -- run once a vint on the chosen CPU. Read the
# cost with RIGRATE=1 on the rig. sh_src/port_load.h.
ifdef PORTLOAD
SHCCFLAGS += -DPORT_LOAD=$(PORTLOAD)
endif
# PORTLOADN=n: iterations a vint (default 2080 = 10,400 instructions). The
# CONTROL for a null result -- if a 10x load does not move the rate either,
# the hook is not running and the null means nothing.
ifdef PORTLOADN
SHCCFLAGS += -DPL_ITERS=$(PORTLOADN)u
endif
# TESTCART=1 (2026-09-27): the 32X TEST CARTRIDGE, phase 1 --
# docs/design/TESTCART.md, md_src/testcart.c. Boots normally, then runs a
# machine-characterisation suite in BOOT_BANK3HALT's slot and HALTS, painting a
# results grid on name-table row 12: white pass, red fail, blue not run, with a
# white/red legend at the row start so a screenshot is self-describing. Barcode
# bytes 3 and 4 are reserved for it (5 and 6 are PORTTEST's). Never a ship.
# TCPADTEST=1: controller-only cold-boot diagnostic. Reuse the test cartridge's
# display/boot, but do not execute its hardware experiments or start the game.
# Phase traces are present only with the diagnostic or optional frontend.
ifdef TCPADTEST
MDCCFLAGS += -DTEST_CART -DTC_PADTEST
MDASFLAGS += -DTC_PADTEST
SHCCFLAGS += -DTC_PADTEST
endif
ifdef R60FRONTEND
ifndef TCPADTEST
MDCCFLAGS += -DR60_FRONTEND
MDASFLAGS += -DR60_FRONTEND
SHCCFLAGS += -DR60_FRONTEND
endif
endif
ifdef TESTCART
MDCCFLAGS += -DTEST_CART
# AND THE SH-2 SIDE TOO (2026-09-27). It was MD-only until the SH-2 hardware
# probe landed, so sh_probe.h compiled out silently and the 68K sat waiting on
# a handshake nobody was listening for -- COMM4 read back the 68K's own SHP_GO.
# The symptom is indistinguishable from a dead master; the cause is a flag on
# the wrong variable.
SHCCFLAGS += -DTEST_CART
# and tell BOTH linkers: md.ld applies the TESTCART boot-stack reserve, mars.ld
# steps .tilesmd and .porttest up a page so the grown .text does not push the
# .ramtext image into the blob. Shipping layouts are untouched.
MDLDFLAGS += -Wl,--defsym,__testcart=1
SHLDFLAGS += -Wl,--defsym,__testcart=1
endif
ifdef TCBISECT
MDCCFLAGS += -DTC_BISECT
endif
ifdef TCNOTESTS
MDCCFLAGS += -DTC_NOTESTS
endif
# TCPAGE=n: which CATEGORY page the suite opens on (0 CORE, 1 68K TIMING,
# 2 Z80 BUS, 3 32X VDP). The pad pages between them, but the MiSTer rig takes
# no pad input over the launch API -- only screenshots come back -- so a page
# past the first is unreachable there without this. One rom per page is the
# whole cost of capturing the FPGA's answer for every category.
ifdef TCPAGE
MDCCFLAGS += -DTC_PAGE0=$(TCPAGE) -DTC_NOSPLASH
endif
# ... and build it WITHOUT WRAMCODE: the suite never runs the game, so
# relocating the game's hot routines into work RAM buys nothing and its region
# collides with the suite's own .bss (the link fails outright, which is the
# right way for that to surface).
# RIGRATE=1 (2026-09-26): THE WHOLE-RUN CADENCE RATE on the barcode. byte 5 =
# game frames per vint x 255 accumulated since boot (255 = 60 Hz, 128 = every
# other vint), byte 6 = vints/256 so the reader can see it has settled. Every
# other rig speed reading is a per-step accumulator sampled once or twice a
# launch, which is why the ~15-line per-launch spread keeps swamping the thing
# being measured; this one CONVERGES, so four launches give four numbers.
# Needs RIGBARCODE=1. Overrides bytes 5/6, so it excludes the RIGSPAN modes.
ifdef RIGRATE
MDCCFLAGS += -DRIG_RATE
SHCCFLAGS += -DRIG_RATE
endif
# SH2BENCH=1 (2026-09-25): SH-2 CYCLES PER INSTRUCTION, on the rig. Two loops with
# exactly known instruction counts (register-ALU cache-resident, and cached SDRAM
# loads at a 128-byte stride) timed with the master's FRT, published as
# cycles/instr x 8 on the 9-bit value channel, variant in bits 8-7. Read with
#   tools/rig_value.py -n 16 --layout trip
# PORT-CENSUS.md's 7-19x port ratio rests on this number being 1.5-4.0 and it has
# never been measured -- ares charges instruction count only. Destroys the picture
# (it floods CRAM every vint); it is a probe, never a shipping flag.
ifdef SH2BENCH
SHCCFLAGS += -DSH2_BENCH
MDCCFLAGS += -DSH2_BENCH
endif
# RIGBCEVERY=n (2026-09-24): the barcode is built and DMA'd every n-th vint only (n a
# power of two); the per-vint 384-word DMA costs the 68K ~48 lines in active display.
ifdef RIGBCEVERY
MDCCFLAGS += -DBC_EVERY=$(RIGBCEVERY)
endif
# CHAINPRICE=1 (2026-09-24): two V stamps in the 68K IRQ4 chain -- 0xFFA3C0 after the shim's
# vblank, 0xFFA3C2 after part B -- with the line's entry (0xFFA380) and rte (0xFFA300) rings
# they split the chain into shim / game IRQ4 / part B. Pricing only; ~40 cycles a vint.
ifdef CHAINPRICE
MDASFLAGS += -Wa,--defsym,CHAIN_PRICE=1
endif
# BURSTGATE=1 (2026-09-25): the 0x3A9A text-copy loop is gated once per loop instead of
# once per byte (patch_game.py BURST GATE). ~10 68K lines a pass in the level-1 demo.
ifdef BURSTGATE
export BURSTGATE
endif
# WRAMCODE=1 (2026-09-25): the level-1 demo's hottest game routines re-laid into WRAM
# 0xFF5000 by tools/wram_reloc.py (patch_game.py runs it last); their ROM entries jump
# there. Removes the MiSTer core's per-fetch ROM wait for ~82% of the pass; neutral on
# hardware (WRAM fetch is 112 quarter-lines on both, RIGSPAN=15).
ifdef WRAMCODE
export WRAMCODE
MDCCFLAGS += -DWRAM_CODE
endif
# WRAMCODE2=1 (2026-09-24 night): the third block set in WRAM region 2 (0xFF4600-0xFF4BFF,
# WRAM_BLOCKS2). Exact on ares; 0 conversions on the rig (LESSONS 23:50). A knob, off the line.
ifdef WRAMCODE2
export WRAMCODE2
endif
# MDTWOCHOICE=1 (2026-09-25): the MD slot table lets a tile live in either of two hash sets
# (capacity like 16 ways). Off the line: a tile in its alternate set costs four cache lines a
# hit and the FPGA read ~20 lines a pass slower (wc7/wc8). The cset+plane hash alone is the line.
ifdef MDTWOCHOICE
SHCCFLAGS += -DMD_TWO_CHOICE
endif
# RIGSPAN=17 (with CARTBENCH=1, 2026-09-24): mode 15's fetch gauge inside the window at raw
# V = RIGSPANV (default 0x40 = +102 lines); the burst-tenure test on instruction fetch.
RIGSPANV ?= 0x40
ifdef RIGSPAN
MDCCFLAGS += -DRIG_SPAN=$(RIGSPAN) -DRIG_SPAN_V=$(RIGSPANV)
export RIGSPANHOOK
RIGSPANHOOK = 1
ifeq ($(RIGSPAN),2)
export RIGWINMAX
RIGWINMAX = 1
endif
endif
# MDQUIET=1|2 (2026-09-24 A/B): the 68K posts the window but does no DMA/fetch
# in its vint (slim_dma, slim_fetch, mdspr_consume return at once); =1 also skips
# the game's IRQ4 (boot path). Picture stale by design. Reads the master's ack
# line without the 68K's concurrent bus traffic.
# DISPFORCE=1 (probe): the master never blanks the 32X layer (with MDQUIET=1 the
# barcode stays readable).
ifdef DISPFORCE
SHCCFLAGS += -DDISP_FORCE
endif
ifdef VIDFORCE
MDCCFLAGS += -DVID_FORCE
endif
ifdef MDQUIET
MDCCFLAGS += -DMD_QUIET
ifeq ($(MDQUIET),1)
MDASFLAGS += -Wa,--defsym,MD_QUIET_IRQ4=1
endif
endif
# RIGWIN3=1 (with RIGWIN): byte 5 = V at the POST (FM raise) instead of the
# frame end; byte 6 stays the gate-spin exit. The pre-post 68K work is 5 - vblank.
ifdef RIGWIN3
MDCCFLAGS += -DRIG_WIN3
MDASFLAGS += -Wa,--defsym,RIG_WIN3=1
endif
# RIGWIN4=1 (with RIGWIN): byte 5 = V at the game's rte, byte 6 = vints the frame spanned
# (vblank count at the rte minus at the shim's entry, same ring slot).
ifdef RIGWIN4
MDCCFLAGS += -DRIG_WIN4
endif
# BODYPROF=1 = 2026-09-22 probe: FRT ticks per stage of the master's window
# body (post pickup -> ack), bprof[9] in .bss (rom/s16.lst _bprof).
ifdef BODYPROF
SHCCFLAGS += -DBODY_PROF
endif
# BLITNOAUDIT=1 = 2026-09-22 probe (never a ship by itself): the blit's
# per-row uncached readback audit of one skipped group is skipped, to
# price it against BODYPROF's master-half stage.
ifdef BLITNOAUDIT
SHCCFLAGS += -DBLIT_NOAUDIT
endif
# BLITSTOREALL=1 = 2026-09-23 rig probe (never a ship): the blit stores every
# group, skipping none, so the rig's blit lines read the store rate alone
# against the baseline's visit-and-skip (BLASTEM.md 13).
ifdef BLITSTOREALL
SHCCFLAGS += -DBLIT_STOREALL
endif
# CRAMISR=1 = 2026-09-22: paint the 32X CRAM at the flip in the V-ISR
# (vblank, PEN-safe) instead of inside the FM-held window: -10 lines of
# the window the game's gated writers wait on (BODYPROF).
ifdef CRAMISR
SHCCFLAGS += -DCRAM_ISR
endif
# BLITAUDITDIV=N = 2026-09-22: the blit audits one skipping row in N per
# window (rotating) instead of every row; the audit was ~13 lines of a
# compose frame's 66-line master half (BLITNOAUDIT probe).
ifdef BLITAUDITDIV
SHCCFLAGS += -DBLIT_AUDIT_DIV=$(BLITAUDITDIV)
endif
# PUBNORB=1 = 2026-09-22: the publish builds its replay copies (tp_lastA/B)
# from the SDRAM packet instead of reading the framebuffer copy back
# (2 x 368 FB reads, ~8 lines of the window; BODYPROF probe7).
ifdef PUBNORB
SHCCFLAGS += -DPUB_NORB
endif
# ROWGRP=1 = 2026-09-22: per-row 32-byte-group marks from the sprite runs;
# the slave clears only marked groups of a live row (was the full 336 B).
ifdef ROWGRP
SHCCFLAGS += -DROW_GRP
endif
# MTASKINWIN=1 = 2026-09-23: the master composes its owned rows (BANDSHIFT /
# RG2SHIFT) inside the window before its blit half; pair with
# BANDSHIFT=4 RG2SHIFT=0 (36+BS and 184+RS must be multiples of 8).
ifdef MTASKINWIN
SHCCFLAGS += -DMTASK_INWIN
endif
# MDSCENECUT=1 = 2026-09-21: select scene 9 (the round-0 transformation
# cut) from the state word's cut bit; off until its install is proven
ifdef MDSCENECUT
SHCCFLAGS += -DMD_SCENE_CUT
endif
# SLIMWORDCAP=1 = 2026-09-21: the slim emitter stops on packet WORDS, not
# at 40 records (baked records are 2 words). Pair with SLIMCAP=N and
# MDBATCHBLANK=N to load a baked scene faster while the display is blank.
ifdef SLIMWORDCAP
SHCCFLAGS += -DSLIM_WORDCAP
endif
# BLANKNOCOMPOSE=1 = 2026-09-21: while the 32X display is blanked the
# master skips the compose launches, so its window is short and the MD
# builder runs every vint (a scene load is bound by windows per vint).
ifdef BLANKNOCOMPOSE
SHCCFLAGS += -DBLANK_NO_COMPOSE
endif
# BLANKNOFLIP=1 = 2026-09-21: skip the body-fallback flip while blanked
ifdef BLANKNOFLIP
SHCCFLAGS += -DBLANK_NO_FLIP
endif
# BLANKBUDGET=N = page-capture budget per window while blanked (default 13)
ifdef BLANKBUDGET
SHCCFLAGS += -DMD_BLANK_BUDGET=$(BLANKBUDGET)
endif
# WINPROF=1 = per-m_stage FRT ticks of the master window while blanked (SDRAM 0x26028FC0)
ifdef WINPROF
SHCCFLAGS += -DWIN_PROF
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
# GLOWPROBE=1 = NOTES 67 / LOOP29 275. Counts, per run: vints in the
# normal scene vs any other, times the glow animator YIELDED to the 68K
# (the transform gate), vints a mask grant sat undelivered because COMM8
# was busy, the longest such run, and vints the animator actually ticked.
# Answers whether Mike's flat chevron is the animator yielding and the
# 68K never being told to stop masking those blocks.
# GLOWRATE=1 = NOTES 67 / LOOP29 276. Per 64 vints, the number of vints
# in which the chevron/glow ramp (PAL_SH 0x98-0x9F, set 19) CHANGED. The
# rig's only instrument is the value channel and its screenshot path
# samples at ~0.5 Hz, far too slow to see a per-vint rotation directly.
# Pair it with NOGLOW=1 to measure the DELTA PATH's ability to animate
# those words -- the state the SH-2 animator hands them to in the
# transform scene, which the attract never reaches.
# NEEDS BOOTFLIPRATE: the CRAM flood that makes the value channel
# readable lives under BOOT_VALUE, and BOOT_FLIPRATE is what selects
# 0xFFA18A as its source. GLOW_RATE's write runs AFTER the flip-rate
# write in the same vint, so it wins the word and the flood shows the
# ramp count. Built without it, the probe writes a word nothing paints
# and every capture reads "unreadable" -- which is what the first cut
# did (LOOP29 276).
# GLOWCUT=1 = LOOP29 278. The glow animator's transform gate asks
# `pscene_cur != 0` -- the PALETTE-DETECTED scene -- while the animator
# is itself overwriting the very PAL_SH words the detector compares, so
# the transform is never detected, the animator never yields, and it
# paints its graveyard wave over the cutscene (the "floating head" class
# the gate was added to stop). Gate on MD_STATE_CUT instead: the 68K's
# own transformation bit on COMM14, independent of anything we paint.
# Needs MDSTATE (the word) and PALGLOW (the animator).
# OBJLOG=1 = NOTES 72. Logs object 0's (x, y) once per GAME FRAME into
# a ring at 0xFF4000, to find the first frame where our demo diverges
# from the arcade's. Indexed by game frame -- logged at the GAMEGATE
# release, which happens exactly once per game frame -- so it lines up
# with a MAME log of the arcade modulo a constant boot offset.
# 2048 entries x 4 bytes; count at 0xFF4FFE. Probe only.
# MASKPROBE=1 = NOTES 74. Counts what card O's text-capture mask
# actually resolves to per call: full, nothing, partial, untagged.
# Tests the BACKSTOP -- the 68K forces the mask full every 8th vint, so
# full captures should be ~1 in 8 of all calls. Needs TEXTMASKPKT.
# FBBYTES=1 = NOTES 77 / LOOP29 289, the protocol workstream's first
# instrument. Counts FRAMEBUFFER bytes written per generation, measured
# where the write happens rather than derived from a per-row figure.
# FBB[0] blit bytes (the only FB write pass on this line), FBB[1]
# generations, FBB[2] sbuf bytes cleared (SDRAM, for contrast -- the
# clear is NOT a framebuffer pass because DIRECT_FB is not shipped).
ifdef FBBYTES
SHCCFLAGS += -DFB_BYTES
endif
ifdef MASKPROBE
ifndef BOOTFLIPRATE
$(error MASKPROBE needs BOOTFLIPRATE=1 - it supplies the value-channel flood)
endif
SHCCFLAGS += -DMASK_PROBE
MDCCFLAGS += -DGLOW_RATE
endif
ifdef OBJLOG
MDCCFLAGS += -DOBJ_LOG
endif
# GLOWPAGE=1 = LOOP29 284 / LOOP-DECOMPILE 124. The glow animator
# yields to the 68K whenever the CHEVRON PLANE is up -- any 4-bit
# quadrant of either plane's page select >= 10. Pages 10/11 carry the
# transformation and nothing else in a whole arcade run selects a page
# that high, so it is an exact scene marker that is not palette-derived
# (the animator cannot corrupt it), not a sentinel peek (the transform's
# palette sits inside the ambient envelope) and needs no new channel.
# Replaces the `pscene_cur != 0` gate, which never fired.
ifdef GLOWPAGE
SHCCFLAGS += -DGLOW_PAGE
endif
ifdef GLOWCUT
SHCCFLAGS += -DGLOW_CUT
endif
ifdef GLOWRATE
ifndef BOOTFLIPRATE
$(error GLOWRATE needs BOOTFLIPRATE=1 - it supplies the value-channel flood)
endif
SHCCFLAGS += -DGLOW_RATE
MDCCFLAGS += -DGLOW_RATE
endif
ifdef GLOWPROBE
SHCCFLAGS += -DGLOW_PROBE
endif
# `make ... BOOTFLIPRATE=1 TRIPCENSUS=1` = NOTES 78 / LOOP29 292: THE
# GENERATION TRIPLE. Under GAMEGATE the generation rate may be gating its
# own input, so wall figures that assume an independent rate are suspect.
# Per 64 vints on four tags: 0 generations LAUNCHED (master, over COMM6
# tagged 0xA000 -- NOT a bare bit 15, which the 68K's own 0xB101 announce
# would satisfy), 1 68K releases (0xFFA0F6 delta), 2 GAMEGATE fallbacks
# (0xFFA0F4 delta), 3 frames presented (FS bank changes). Token releases
# = tag1 - tag2. Reading: 0~1>3 = we compose frames nobody sees; 0~3<1 =
# the game runs ahead and generations are skipped; all equal = a
# self-gating loop, and the 2.47 v/gen is an equilibrium, not a cost.
# Tags 3 and 1 have KNOWN answers (BOOTFLIPRATE, BOOTGAMERATE) -- check
# them before the number sizes anything.
# `make ... BOOTFLIPRATE=1 BODYCENSUS=1` = NOTES 85 / LOOP29 295: WHERE THE
# GENERATION'S BODY GOES, ON HARDWARE. Every stamp either thread has placed
# lives in the V-ISR -- ~22 lines of a ~577-line generation, under 4% of it --
# and the rest was inferred by subtraction. DIAG's per-stage accumulators are
# ungated in the shipping rom; this carries four of them to the rig, which has
# no memory dump and only the value channel. Per 64 vints, master FRT ticks
# per generation >> 8 (a vint = 47, a 2.2-vint generation = 104):
#   tag 0 WINDOW span (DIAG[8]: entry -> launch, contains blit + palette push)
#   tag 1 SHIP (DIAG[5])   2 MAPS DRAIN (DIAG[11])   3 WHOLE GENERATION
# RESIDUAL = tag3 - (tag0+tag1+tag2) is what the card is hunting. ares says
# window 40% / ship 19% / drain 12% / unaccounted 19%, but ares charges
# instruction cycles only -- that ranking is what the rig is here to overturn.
# `make ... BODYCENSUS=1 BODYGAP=1` = NOTES 87: the census's own blind spot.
# NAT_WALL[0] is the generation WALL (launch->close); the rig PERIOD is
# longer and 0.38 v/gen sits in between with no stamp on it. Swaps the tag
# set to: 0 close->launch GAP, 1 launch->launch PERIOD, 2 WALL (anchor:
# must reproduce LOOP29 295's 86.5), 3 window span (anchor: must reproduce
# 38.0). Two anchors, so a drifted channel cannot pass as a measurement.
#
# `make ... BODYCENSUS=1 PURGESTRESS=1` = NOTES 87, THE FETCH DISCRIMINATOR.
# One extra cache_purge per body poll visit: ~1 purge a window becomes
# hundreds. An extra purge can never be INCORRECT (it only discards valid
# lines), so this renders identically to the line and differs only in how
# cold the cache is -- unlike removing purges, which would confound timing
# with rendering. Stages inflate = instruction fetch is the tax and the
# purge is a lever; stages hold = the tax is data bandwidth and the pivot
# is the only lever left. DIAGNOSTIC ONLY, never ships.
ifdef BODYGAP
SHCCFLAGS += -DBODY_GAP
endif
# `make ... BODYSHIFT=9` = NOTES 89: double the census ceiling. The default
# >>8 puts a vint at 47 and saturates at 127 = 2.7 vints, which the
# half-cache build (CACHEOFF=0x19) exceeded -- it read a flat 127 twice.
# >>9 halves the resolution and reaches 5.4 vints; a >>9 reading times two
# compares directly against a >>8 baseline.
ifdef BODYSHIFT
SHCCFLAGS += -DBODY_SHIFT=$(BODYSHIFT)
endif
ifdef PURGESTRESS
SHCCFLAGS += -DPURGE_STRESS
endif
# `make ... BODYCENSUS=1 CACHEOFF=1` = NOTES 87, THE FETCH DISCRIMINATOR.
# Disables the master's cache so every instruction fetch is an SDRAM round
# trip -- the largest possible move of the fetch term, in the opposite
# direction to the eventual fix, which is what makes it a MEASUREMENT of
# the term's size. A disabled cache cannot be incoherent, so it renders
# identically to the line. Stages inflate a lot = fetch is the tax and code
# placement is the lever; stages barely move = the tax is data bandwidth and
# the pivot is the only lever left. DIAGNOSTIC ONLY, slow, never ships.
# (PURGESTRESS was the first attempt and FAILED to move the variable: under
# NAT_ALL_SLAVE there are ~1 body poll visits a generation to hook.)
# `make ... BODYCENSUS=1 CACHEOFF=0x13|0x15` = NOTES 89, THE FETCH/DATA
# SPLIT, and it is the test LOOP29 297 said could not be built. The SH7604
# separates the two streams in hardware: CACHE.sv:499 fills the
# instruction bus only while CCR.ID is clear and the data bus only while
# CCR.OD is clear, with CE untouched, so lookups keep working and the
# other stream keeps caching. Against the 0x11 baseline:
#   CACHEOFF=0x13  (CP|CE|ID)  instruction fills OFF -> isolates FETCH
#   CACHEOFF=0x15  (CP|CE|OD)  data fills OFF        -> isolates DATA
# Stages inflate under 0x13 and not 0x15 = the uniform 2.9x is fetch, and
# code placement is the lever. The reverse = data, and the pivot is it.
# CACHEOFF=1 keeps the old CE-clear form, which WEDGES the master (297) --
# it is kept only so that result stays reproducible. MASTER ONLY: the
# guard in cache_purge() discriminates on the stack pointer, because
# slave cache-off is a known-black state (mars_start.s:547).
# DIAGNOSTIC ONLY, never ships.
ifdef CACHEOFF
SHCCFLAGS += -DCACHE_OFF -DCACHE_OFF_CCR=$(if $(filter 1,$(CACHEOFF)),0,$(CACHEOFF))
endif
ifdef BODYCENSUS
ifndef BOOTFLIPRATE
$(error BODYCENSUS needs BOOTFLIPRATE=1 - it supplies the value-channel flood)
endif
SHCCFLAGS += -DBODY_CENSUS
MDCCFLAGS += -DBODY_CENSUS
endif
ifdef TRIPCENSUS
ifndef BOOTFLIPRATE
$(error TRIPCENSUS needs BOOTFLIPRATE=1 - it supplies the value-channel flood)
endif
ifndef GAMEGATE
$(error TRIPCENSUS measures the GAMEGATE release rate - add GAMEGATE=1)
endif
SHCCFLAGS += -DTRIP_CENSUS
MDCCFLAGS += -DTRIP_CENSUS
endif
ifdef PALGLOW
ifndef PALSTATIC
$(error PALGLOW rides the PALSTATIC scene machinery - add PALSTATIC=1)
endif
MDCCFLAGS += -DGLOW_MASK
SHCCFLAGS += -DGLOW_ANIM
endif
# `make ... MDBATCH=N` = LOOP29 199. Tiles shipped to MD VRAM per vint
# while the display is ON. 12 is the R60 default: 40 was calibrated for
# 30Hz windows and overruns vblank at 60, which is the load-in tear and
# the purple band. But 12 is also why cells pop in black -- a scene change
# needs the whole 1120-tile working set, which is 93 vints at 12. The
# blanked path already ships 40 (m_main.c:13268). Mike's eye ranks this.
ifdef MDBATCH
SHCCFLAGS += -DMD_BATCH_N=$(MDBATCH)
endif
# `make ... MDBATCHOFF=N` = LOOP29 214. Tiles shipped per vint while the
# ROUND IS OFF SCREEN (a cutscene: face, eye, intro, ranking). Default 24,
# ranked on the rig 2026-09-12: 40 put the chevron plane up on the first
# frame of the field but tore level 2's top band on hardware (the documented
# vblank overrun; headless ares does not show it); 24 clears the band and
# the plane arrives 50 frames after the field. The play batch stays MDBATCH.
ifdef MDBATCHOFF
SHCCFLAGS += -DMD_BATCH_OFF=$(MDBATCHOFF)
endif
# `make ... EDGENOWIPE=1` = LOOP29 219 DIAGNOSTIC. Do not wipe the resident
# sets' tile tags when the round leaves the screen (218). Isolates the
# hardware-only horizon band of vi66: A/B against vi66a (218 with the old
# page-0 table). Expect the chevron plane to draw in alternate rows (216).
ifdef EDGENOWIPE
SHCCFLAGS += -DEDGE_NOWIPE
endif
# ASSIGNNOWIPE=1 = LOOP29 223 diagnostic: no stale-tag wipe at assign time.
ifdef ASSIGNNOWIPE
SHCCFLAGS += -DASSIGN_NOWIPE
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

# MDSPROFF must run AFTER every SHCCFLAGS assignment above -- filtering
# mid-file removed nothing because -DMD_SPR is appended later (measured:
# the flag had no effect at all, LOOP29 176).
ifdef MDSPROFF
SHCCFLAGS := $(filter-out -DMD_SPR -DMDSPR_TOP -DMDSPR_WHY,$(SHCCFLAGS))
endif
# `make line NOSLIM=1` = the line WITHOUT the slim pipeline (the FB tile
# route), for A/B against `make line`. Filtered here, after every
# assignment, because LINE_FLAGS sets TILESLIM=1 inside the recursive make.
comma := ,
ifdef NOSLIM
SHCCFLAGS := $(filter-out -DTILE_SLIM,$(SHCCFLAGS))
MDCCFLAGS := $(filter-out -DTILE_SLIM -DSLIM_CAP=40,$(MDCCFLAGS))
MDASFLAGS := $(filter-out -Wa$(comma)--defsym$(comma)PARTB_HOOK=1,$(MDASFLAGS))
endif

# FLAG STAMP — objects must depend on the FLAG SET, not just on sources.
# Without this, `make IDLETOKEN=1` right after a plain `make` reuses every
# object whose .c file did not change, so half the build silently keeps
# the old semantics. LOOP 11a lost a full measurement cycle to exactly
# that: the SH-2 published idle tokens while md_main.o, untouched, had no
# poll-and-skip in it at all — and the run scored a perfect 24.26 because
# it WAS the baseline. Any flag build measured before this existed should
# be re-measured before it is believed.
ARES ?= $(HOME)/src/ares-debug/build_macos/headless-ui/Release/ares-headless
FLAGSTAMP := .build_flags
# every tool a recipe runs sees the title (2026-10-29): a per-game input it cannot find is
# an error, never a silent fall back to altbeast's
export GAME
# THE DECOMPILE HALF IS DERIVED FOR THESE TITLES ONLY (2026-10-29 smoke test). Its CPORT_*
# tables (roots, stubs, clamps, widths, hot set), the audit tables in docs/audit/ and the
# corpus are Altered Beast's. A port build for any other title stops HERE -- before this
# guard, GAME=goldnaxe decompiled and shipped Altered Beast under another name. Add a title
# once its tables exist (docs/design/FRAMEWORK.md, step 4).
PORT_TITLES = altbeast
ifdef PORTFULL
ifeq ($(filter $(GAME),$(PORT_TITLES)),)
$(error PORTFULL: no decompile tables for GAME=$(GAME) (have: $(PORT_TITLES)) -- derive its corpus docs/proof/$(GAME).c, CPORT_* tables and docs/audit tables first; see docs/design/FRAMEWORK.md)
endif
endif
PORTSTAMP := .port_flags
# INFORMATION-ONLY GOALS DO NOT RESTAMP. This $(shell) runs at PARSE time,
# so `make lint` -- which only asks questions -- used to overwrite the
# stamp with a reduced -D set and force a full rebuild of the next real
# build. Any goal that compiles nothing belongs in this list.
# quantcheck joined the list 2026-09-27, having proved the comment right the
# hour it was added: `make quantcheck` compiles nothing, restamped the flags
# with the bare -D set, and the next build silently relinked WITHOUT
# TESTCART=1 -- rom/s16.32x became a plain game rom while the disassembly it
# was about to be measured from looked fine. A goal that compiles nothing and
# is not in this list is a probe-rom invalidator.
#
# AND `make -n` RESTAMPS TOO. The $(shell) is parse-time, so a DRY RUN with
# flags -- `make -n line PALQUANT=1`, checking whether a guard fires -- rewrites
# the stamp and forces the next real build to a different flag set. -n is not
# read-only here. Check flags with a goal from this list, or re-stamp by
# rebuilding the line afterwards.
INFO_GOALS := print-defs line-defs shipus-defs lint lint-generic lint-32x \
              quantcheck pengrid compare shprobe glyphcheck \
              port check-demo
# port and check-demo (2026-10-04) compile nothing in THIS tree: they recurse into
# build/port/, which keeps its own stamp. Without them here, `make port` restamped the
# real tree's flags and the next `make line` rebuilt the line for nothing.
ifeq ($(filter-out $(INFO_GOALS),$(MAKECMDGOALS)),)
ifneq ($(MAKECMDGOALS),)
FLAGSTAMP_SKIP := 1
endif
endif
# THE STAMP ITSELF IS WRITTEN AT THE END OF THIS FILE, NOT HERE.
#
# It is a parse-time $(shell), so it captures SHCCFLAGS as it stands AT THAT LINE --
# and every PORT* probe flag (PORTSEED, PORTZOOM, PORTENDURE, PORTBODY, PORTWATCH,
# PORTREGS, PORTMAIN) is appended ~550 lines further down. Measured 2026-10-02:
# `.build_flags` carried -DPORT_FULL and NOT -DL2M_SEEDAT, so switching PORTSEED=64
# to PORTSEED=9 changed nothing make could see and port_l2.o was REUSED. Every
# PORT*-flag-only comparison made before this moved is a comparison of two
# identical objects unless a source file happened to change in between.
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
# SHIPBANDSHIFT / SHIPRG2SHIFT: the slave/master compose row split of the
# ship line (2026-09-23; a bare BANDSHIFT=N on the command line lost to
# the literal here, like BLITSHIFT). 36+BS and 184+RS must be multiples of 8.
SHIPBANDSHIFT ?= 36
SHIPRG2SHIFT ?= 40
SHIP_COMMON = MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1 R60=1 \
              BANDSHIFT=$(SHIPBANDSHIFT) RG2SHIFT=$(SHIPRG2SHIFT) BLITSKIP=1 DIRTYROW=1 \
              BLITSHIFT=$(SHIPBLITSHIFT) SPRLATE=1 PRHOLD=6 ROWDEFER=1 PALDELTA=1 NATIVE=1 \
              LAUNCHEARLY=1 BLITCHASE=1 EDGE42=1 HSSHIP=1 \
              TILESLIM=1 SLIMCAP=80 SLIMWORDCAP=1 FBXECHO=1 BGBOTTOM=1 MDSREMARK=1 \
             MDSCENES=1 MDSCENECUT=1 HOLDFROMBLANK=1 ARTTAIL=1 C1MASKTAB=1 CUTPREFETCH=1 CRAMISR=1 BLITAUDITDIV=4 PUBNORB=1
# TILESLIM=1 SLIMCAP=40 joined the line 2026-09-20 (rom/night/slim21.32x,
# Mike: "background fixed, and frames feel consistent again"). The slim
# pipeline: docs/design/SLIM-PIPELINE.md 1b.
# NOGLOW=1 drops PALGLOW from the ship line: no 68K glow mask and no
# SH-2 animator, so sets 19-21 are driven by the PALETTE DELTA PATH
# alone. That is the state the animator hands them to in the transform
# scene (m_main.c's scene gate), so this build measures the delta path's
# ability to animate them WITHOUT needing to reach the cutscene -- which
# the attract never does (LOOP29 275: 5 vints of 3903 outside the normal
# scene, zero animator yields). Probe only.
SHIP_US = $(SHIP_COMMON) $(if $(NOSPRBAKE),,SPRBAKE=1) TILECLASS=1 TXTCLASS=1 MDSPR=1 PALSTATIC=1 $(if $(NOGLOW),,PALGLOW=1) PENMATCH=1 MDSTATIC=1
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
# ---- THE LINE (2026-09-15) ----
# THE FLAGS bldS WAS BUILT WITH, AS ONE TARGET. `make ship-us` alone is
# NOT the line and neither is bare `make`: both regenerate .build_flags
# with a REDUCED -D set, which regenerates md_src/pal_thunks.h and
# md_src/fmgate_tab.h differently and leaves md_main.c:5329 failing to
# compile ('md_hold_seen' undeclared). Recovering from that costs a
# session; `make line` cannot get it wrong.
#
# Verify after building: `cmp -l rom/s16.32x rom/night/bldS.32x` must
# report bytes ONLY in 0x25F0xx (git hash) and 0x3FFFxx (BUILD string).
# 2026-09-25: the TAGKEEP family (TAGKEEP, PENHOLD, PENREPAINT) LEFT the line with
# nt1 -- Mike's play pass ("looks great no regressions"), rig 3/3, and TAGKEEP was
# already falsified for its own purpose. WRAMCODE is still passed explicitly.
LINE_FLAGS = FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 \
             TEXTCAPMASTER=1 GAMEGATE=1 GAMEGATEWAIT=1 \
             NBUILD1=1 \
             MDSPRTOP=1 C1NOFB=1 C1PUNCH=1 C1STAMP=1 CAT1MD=1 GLOWPAGE=1 \
             MDBATCH=24 MDROUND=1 MDSTATE=1 MDSREFUSE=1 \
             SCANMEMO=1 SPRLIST=1 SPRRUN32=1 TEXTCAPMASK=1 TEXTMASKPKT=1 \
             TILESMD=1 ROWGEN=1
# HOST-SIDE GATE, no emulator and no rig: does the SH-2's mdp_quant still agree
# with the baker that generates its tables? Two implementations of one function
# in two languages, and when they drift the symptom is pink trees, not a crash.
# EVERY STRING THE CART PRINTS MUST BE RENDERABLE. tc_glyph knows 45 glyphs and
# returns a BLANK for anything else, silently -- the "?" no-reference marker had
# been invisible since the delta column was written, looking like a spacing
# choice rather than a bug. Two seconds, no emulator.
.PHONY: glyphcheck
glyphcheck:
	python3 tools/tc_glyphcheck.py

.PHONY: quantcheck
quantcheck:
	python3 tools/quant_diff.py

.PHONY: line
line:
	$(MAKE) ship-us $(LINE_FLAGS)

# THE PORT BUILDS OUT OF TREE. `make port` runs this same Makefile inside build/port/,
# a shadow of the source tree (tools/shadow_tree.sh: real directories, per-file
# symlinks to the tracked sources, directory symlinks to roms/ mame/ docs/proof/), so
# its objects, generated headers, .build_flags and rom land under build/port/ and the
# line's objects and rom/s16.32x are never touched -- no `touch` to force a rebuild,
# no second `make line` to restore. The rom is build/port/rom/s16.32x.
# ATTRACT FULLY STATIC (2026-10-10): two tables keyed on 0xFFF022, the demo half on four
# lines (the sprite line is measured free in the demos -- tools/mdspr_census.py, 0 claims
# over 86 frames) and the static half on three.
PORT_FLAGS = NOSPRBAKE=1 PORTFULL=1 PORTMAIN=1 PORTOS=1 NOGAMEHIGH=1 PORTVIDEO=1 \
	PORTPLAY=1 PORTBOOT=1 PORTCORO=1 PORTCLK1=1 PORTFFBM=1 \
	MDATTRACTSPLIT=1 MDLINES4=1 MDSPROFF=1 PORTTILEDIRTY=1 \
	$(R60_LINE_FLAGS)
# THE R60 RENDERER'S LINE (2026-10-16, sw121, Mike's office-PVM pass): a plain `make port`
# builds it. Every sw-rom since 2026-10-14 passed these through PORT_EXTRA by hand; a
# plain `make port` built the deleted renderer's path instead. Probe flags still go in
# PORT_EXTRA on top.
R60_LINE_FLAGS = R60NEW=1 GOVERNOR=1 R60SLAVEWIN=1 R60PLOT3=1 R60CRAMDELTA=1 R60NOBARS=1 \
	R60PLANES=1 PALFIRST=1 R60TEXT=1 R60RECTCLR=1 R60ZOOM=1 R60KEEPSTALE=1 R60TXTPOOL=1
PORT_DIR = build/port
# A PROBE BUILD IS NAMED, AND IT IS NEVER s16.32x. PORT_EXTRA roms used to land on
# build/port/rom/s16.32x -- the same path the gate measures and the same path `make port`
# writes -- so a probe silently became "the port". It cost a whole measurement on 2026-10-08:
# a PAL_SETGEN-seeded probe was compared against build/port/rom/s16.32x as its control, and
# that file WAS the seeded probe, so both sides read 0xFFF1 and agreed perfectly. The name
# is derived from PORT_EXTRA (PORTRIG=1 -> portrig), or set PORT_NAME to choose it.
PORT_SLUG := $(if $(PORT_EXTRA),$(if $(PORT_NAME),$(PORT_NAME),$(shell printf '%s' '$(PORT_EXTRA)' | tr 'A-Z' 'a-z' | tr -cd 'a-z0-9')),)

# THE RELEASE INPUT (2026-10-29): roms/$(GAME) is unpacked from the user's own MAME zip
# (mame/$(GAME).zip, or the parent's for a merged clone) by tools/romset.py, which also
# interleaves prog68k.bin and checks its SHA-1. prog68k.asm is read by ten tools; it is
# objdump at -m m68k:68000 -- plain -m m68k decodes FPU forms and gives a different file.
# No prerequisite on purpose: an existing roms/ tree is never re-extracted.
$(GAMEROMS)/prog68k.bin:
	python3 tools/romset.py $(GAME)
$(GAMEROMS)/prog68k.asm: $(GAMEROMS)/prog68k.bin
	$(MDBIN)/m68k-elf-objdump -D -b binary -m m68k:68000 $< > $@
romset: $(GAMEROMS)/prog68k.bin $(GAMEROMS)/prog68k.asm
# THE COMMITTED DECOMPILE INPUTS (2026-10-29): docs/proof is the working decompile dir and
# is untracked, so a release clone builds from these copies (tools/shadow_tree.sh links
# them in when docs/proof is absent). `candidate` refuses when they drift from docs/proof,
# so a release never builds from a different decompile than the one that was tested.
RELEASE_INPUTS = $(GAME).c datwidth.json datwidth_observed.json datwidth_observed_min.json \
                 indirect_roots.txt object_handlers_union.txt
release-sync:
	@for f in $(RELEASE_INPUTS); do cp docs/proof/$$f games/$(GAME)/decomp/$$f; done; git status --short games/$(GAME)/decomp
release-check:
	@[ ! -d docs/proof ] || for f in $(RELEASE_INPUTS); do cmp -s docs/proof/$$f games/$(GAME)/decomp/$$f || \
	  { echo "release-check: games/$(GAME)/decomp/$$f differs from docs/proof -- run make release-sync and commit"; exit 2; }; done
.PHONY: release-sync release-check
# THE PER-GAME C CONSTANTS (framework step 2, 2026-10-29): games/$(GAME)/game.py TABLES ->
# sh_src/game_cfg.h (GAME_WRAM_*), generated, never committed. FLAGSTAMP carries GAME.
sh_src/game_cfg.h: tools/emit_game_cfg.py games/$(GAME)/game.py kit/schema.py $(FLAGSTAMP)
	python3 tools/emit_game_cfg.py $(GAME) > $@
md_src/md_main.o md_src/testcart.o sh_src/port_l2.o sh_src/r60/r60_main.o sh_src/r60/r60_planes.o: sh_src/game_cfg.h
# the title's runtime glue, #included into port_l2.c where it was cut out (framework 3e)
sh_src/port_l2.o: games/$(GAME)/glue.c
sh_src/port_l2.o: SHCCFLAGS += -Igames/$(GAME)
ifdef L2NONE
sh_src/port_l2_data.o sh_src/port_l2_frames.o sh_src/port_l2_io.o: | $(L2NONE)
$(L2NONE):
	mkdir -p $(dir $@) && : > $(dir $@)seed.bin && : > $(dir $@)deltas.bin && : > $(dir $@)io.bin
endif
.PHONY: romset

port: romset
	GAME=$(GAME) tools/shadow_tree.sh $(PORT_DIR)
	$(MAKE) -C $(PORT_DIR) line $(PORT_FLAGS) $(PORT_EXTRA)
ifeq ($(PORT_SLUG),)
	@ls -l $(PORT_DIR)/rom/s16.32x
else
	@mv $(PORT_DIR)/rom/s16.32x $(PORT_DIR)/rom/$(PORT_SLUG).32x
	@# keep the symbol map and the flags under the rom's name too (tools/save_build.sh's rule):
	@# the next build rewrites s16.lst/s16.elf and tools/romsyms.py then refuses to read
	@# this rom (2026-10-22: artaudit refused hp1 because hp0 was built a minute later)
	@cp $(PORT_DIR)/rom/s16.lst $(PORT_DIR)/rom/$(PORT_SLUG).lst
	@cp $(PORT_DIR)/rom/s16.elf $(PORT_DIR)/rom/$(PORT_SLUG).elf
	@cp $(PORT_DIR)/.build_flags $(PORT_DIR)/rom/$(PORT_SLUG).flags
	@echo "PROBE: $(PORT_DIR)/rom/$(PORT_SLUG).32x  (PORT_EXTRA=$(PORT_EXTRA); s16.32x NOT written)"
	@ls -l $(PORT_DIR)/rom/$(PORT_SLUG).32x
endif

# THE PLAYABLE CANDIDATE, ONE COMMAND (2026-10-20). A bare `make port` carries none of the
# renderer's or the pads' flags: no live pads, no palette remap, no FG priority, no plot
# fixes. Mike played such a rom on 2026-10-20 and called it broken, and earlier the same
# rom read as "input not wired". Every rom handed to Mike comes from this target.
#   make candidate NAME=sw268            -> build/port/rom/sw268.32x
#   make candidate NAME=x EXTRA="FOO=1"  -> the candidate plus a probe flag
CANDIDATE_FLAGS = R60PLOT4=1 R60PLOT5=1 R60TXTFAST=1 R60ROWCLR=1 R60ROWFILL=1 R60CRAMMASK=1 \
	R60CMSTATS=1 R60KEEPSPR=1 R60EMPTYPKT=1 R60BLANKFILL=1 R60PRIO=1 R60IDPENS=1 R60RAWPENS=1 R60TXTTOUCH=1 \
	R60HOLDDIRECT=1 R60STAGEDIRECT=1 R60IDLECONSUME=1 R60LIVEPAD=1 R60FRONTEND=1 NOTILESMD=1 \
	R60SPEECH=1 R60SFX=1 R60CUTPF=1 R60PALMAP=1 R60ZOOMFAST=1 R60PUNCH2=1 R60ROWFAST=1 R60CLREXT=1 R60CLRPRE=1 PORTSTARTCOIN=1 R60PKTODDHI=1 PORTNOPALPROBE=1 R60SPLITFAST=1 R60TXTPLANFAST=1 R60SPEECHSLAVE=1 R60TXTLUTPAL=1 R60TXTHEAL=1 R60TXTHEALLATE=1 R60PLANFAST=1 R60PLANNEW=1 PORTHOT2=1 CARTMB=4
.PHONY: candidate
# The sound lanes' inputs are recorded from the user's MAME set (tools/sound_inputs.sh skips
# what is present), then baked by the sh_src/{speech,sfx}.bin rules below.
sound-inputs:
	GAME=$(GAME) tools/sound_inputs.sh
.PHONY: sound-inputs
# THE RELEASE ENTRY POINT: a fresh clone + mame/$(GAME).zip + mame on PATH -> one rom.
release-rom:
	$(MAKE) tables
	$(MAKE) candidate NAME=$(GAME)
.PHONY: release-rom
candidate: release-check sound-inputs sh_src/speech.bin sh_src/sfx.bin
	@test -n "$(NAME)" || { echo "make candidate NAME=<rom name>"; exit 2; }
	@# THE SOUND BANKS ARE SEGA-DERIVED AND GITIGNORED: a tree without them links EMPTY
	@# .sounddata/.sfxdata and the rom loops a sound re-init with no effects or speech
	@# (sw270, 2026-10-20: the builder's checkout held 0-byte placeholders). Refuse.
	@for f in sh_src/speech.bin sh_src/speech_tab.h sh_src/sfx.bin sh_src/sfx_tab.h; do \
	  test -s $$f || { echo "candidate: $$f is missing or empty -- the speech/effects lanes would ship silent."; \
	    echo "  copy the banks from the advisor worktree (.claude/worktrees/advisor-bisect/sh_src/{speech,sfx}.bin and *_tab.h)"; exit 2; }; done
	$(MAKE) port PORT_EXTRA="$(CANDIDATE_FLAGS) $(EXTRA)" PORT_NAME=$(NAME)

# THE GAMEPLAY PARITY HARNESS (2026-10-22, docs/handoff/PARITY-HARNESS.md). One command
# scores a rom against the arcade on the attract (tools/accept_ares.sh) and on a played
# level (tools/parity_play.py replays TAPE on ares-headless, tools/parity_compare.py
# scores it against the ARCADE corpus and writes REPORT.md).
#   make parity ROM=build/port/rom/hp1.32x [TAPE=tools/inputs/level1_full.csv] \
#       [ARCADE=tests/gameplay/arcade_level1_full]       -> build/parity/hp1/REPORT.md
#   make parity-oracle [TAPE=...] [ARCADE=...]           -> regenerate the arcade corpus
#                                                           (Sega-derived, gitignored);
#                                                           the DIPs default to 66/54, what
#                                                           the port boots with (PORT_CORO,
#                                                           port_l2.c); GD_DSW1/GD_DSW2 override
#   make parity-candidate NAME=x [EXTRA='FLAG=1']        -> candidate + PORTGODMODE=1, then parity
# GFKEY=1 KEYS THE PORT'S TAPE TO GAME FRAMES (2026-10-25): parity_play.py re-keys the tape to
# the arcade's blink-clock step count and ares --input-gameframe plays it against the port's
# u16 port_gf_count (sh_src/port_l2.c), so a port slower than 60 Hz takes the arcade's route at
# the same game frames. Keyed by wall frame the hero left the arcade's path 28 frames after
# the first enemy and the port never reached the boss.
# THE GODMODE ROM IS MEASUREMENT ONLY. ares-headless cannot write memory, so the port's
# lives are held at 3 in the rom (PORTGODMODE, sh_src/port_l2.c) to mirror the oracle's
# GD_GOD rule; a rom built by parity-candidate is never handed to Mike. The compare is
# given the same TAPE the port replayed; section 0 of the report refuses a corpus recorded
# on another tape or on other DIPs than the port boots with.
TAPE ?= tools/inputs/level1_full.csv
ARCADE ?= tests/gameplay/arcade_level1_full
PARITY_OUT = build/parity/$(notdir $(basename $(ROM)))
.PHONY: parity parity-oracle parity-candidate
parity:
	@test -n "$(ROM)" -a -f "$(ROM)" || { echo "make parity ROM=build/port/rom/<name>.32x [TAPE=] [ARCADE=]"; exit 2; }
	@test -f "$(ARCADE)/timeline.csv" || { echo "parity: no arcade corpus at $(ARCADE) -- make parity-oracle TAPE=$(TAPE) ARCADE=$(ARCADE)"; exit 2; }
	mkdir -p build/accept $(PARITY_OUT)
	ACCEPT_OUT=build/accept tools/accept_ares.sh $(ROM)
	python3 tools/parity_play.py $(ROM) --tape $(TAPE) --out $(PARITY_OUT) $(if $(TAPEPAD),--rom-tape,$(if $(GFKEY),--key-arcade $(ARCADE)))
	python3 tools/parity_compare.py --arcade $(ARCADE) --port $(PARITY_OUT) --rom $(ROM) \
		--tape $(TAPE) --accept build/accept --report $(PARITY_OUT)/REPORT.md
	@echo "PARITY: $(PARITY_OUT)/REPORT.md"

parity-oracle:
	tests/gameplay/arcade_oracle.sh $(TAPE) $(ARCADE)

parity-candidate:
	@test -n "$(NAME)" || { echo "make parity-candidate NAME=<rom name> [EXTRA='FLAG=1']"; exit 2; }
	$(MAKE) candidate NAME=$(NAME) EXTRA="PORTGODMODE=1 $(if $(TAPEPAD),PORTTAPEPAD=$(abspath $(TAPE)) PORTTAPEARCADE=$(abspath $(ARCADE))) $(EXTRA)"
	$(MAKE) parity ROM=$(PORT_DIR)/rom/$(NAME).32x TAPE=$(TAPE) ARCADE=$(ARCADE)

# THE DEMO-B PARITY CHECK: build the port, run ares-headless, diff every live object
# record against the arcade's work RAM at ticks 0xA2 0xD4 0x1FE 0x228 0x246
# (tests/demo-b/check_demo.py; the arcade dumps are tests/demo-b/arcade/aw_*.bin).
# Exits 1 on any differing byte in a live record.
# SWEEP=1 adds the full sweep after the gate: every arcade tick of both demos, all 64
# records live or dead plus the globals (tests/demo-b/sweep_demo.py). ~25 minutes.
CHECK_DEMO_ROM := $(PORT_DIR)/rom/$(if $(PORT_SLUG),$(PORT_SLUG),s16).32x
ifdef PORT_EXTRA
export DEMO_PROBE = 1
endif
check-demo: port
# THE DECLARED-EXTENT CHECK RUNS FIRST (2026-10-09). It has flagged mdp_line_c overrunning
# mdp_pen_rc since 2026-09-10 and exited 1 the whole time, and nothing ran it -- which is how
# a four-line pen table shipped eleven pens of refcount bytes. Two seconds, no emulator.
#
# IT CANNOT GATE YET AND THE `|| true` IS NOT LAZINESS. With the mdp_* overlap fixed --quiet
# STILL exits 1 on SEVEN overlaps that predate this card: grp_key 0x28360 (160B), HSC_RING
# 0x28D40 (192B and 384B, two claimants), DRQR 0x28F80 (108B), BQ_INCOMPLETE 0x28FC4 (20B),
# PHH 0x39900 (64B) and MDSPR_PAL 0x3A500 (384B), plus a batch of same-base declarations the
# tool reports as "by 0 bytes". Some of those are the probe blocks its own docstring says may
# never be live together (SLC / SPRLATE / FBP); none has been triaged. Gating on it today
# would make every check-demo red, so it PRINTS and the next card either triages the seven or
# teaches the tool a named baseline. Do not silence it -- read it.
	-python3 tools/sdram_map.py --quiet || true
# A PORT_EXTRA BUILD IS GATED AS A NAMED PROBE (2026-10-13). It writes <slug>.32x, never
# s16.32x, so the gate is pointed at the slug and DEMO_PROBE=1 is the explicit opt-in that
# check_demo.py requires for a named rom. Without PORT_EXTRA nothing changes: the same
# command, the same rom, the same output. Three candidates in a row went ungated on logic
# before this existed, because every candidate worth measuring is a PORT_EXTRA build.
	python3 tests/demo-b/check_demo.py $(CHECK_DEMO_ROM)
	if [ "$(SWEEP)" = 1 ]; then python3 tests/demo-b/sweep_demo.py $(CHECK_DEMO_ROM); fi

ship-us:
	$(MAKE) GAME=$(GAME) $(SHIP_US)   # was GAME=altbeast: a GAME=goldnaxe candidate built AB (2026-10-29)
ship-jp:
	$(MAKE) GAME=altbeastj $(SHIP_JP)

$(MDOBJS) $(SHOBJS): $(FLAGSTAMP)
# the 68K reads the tile blob's shape from the generated header
md_src/md_main.o: sh_src/tiles_md.bin
# pal_thunks.h is generated by the game_body rule; without this edge a
# flag flip can compile md_main.o against the stale header (the #error
# guard in md_main.c is the backstop that caught it)
md_src/md_main.o: md_src/pal_thunks.h md_src/fmgate_tab.h md_src/game_irq.h
# MDHSCR generates one more header in the same patch_game run; without this
# edge md_main.o compiles before it exists (LOOP-DECOMPILE 25).
# The sound blob lives in sh_src/ (rsynced into the out-of-tree shadow like tiles.bin; the
# sndtest headers it is cut from are untracked generated files and exist only in the real
# tree, so the shadow keeps the copied blob). Sega-derived: gitignored, never committed.
SND_SRC := $(wildcard sndtest/md/render_music.h sndtest/md/sndmap_data.h)
sh_src/sound.bin md_src/sound_tab.h: tools/sound_blob.py $(SND_SRC)
	python3 tools/sound_blob.py sndtest/md/render_music.h sndtest/md/sndmap_data.h sh_src/sound.bin md_src/sound_tab.h
# Each blob is a dependency ONLY under its own flag: they are Sega-derived and gitignored, so a
# tree without them must still build the bare line (tools/flagcheck.sh checks exactly that).
ifdef R60SOUND
sh_src/sound_data.o: sh_src/sound.bin
md_src/md_sound.o: md_src/sound_tab.h md_src/md_z80_player.h
endif
ifdef R60SPEECH
sh_src/sound_data.o: sh_src/speech.bin
endif
sndtest/sh/speech_bank8.c: tools/speech_bake.py $(wildcard sndtest/speech/manifest.json)
	python3 tools/speech_bake.py sndtest/speech $@ --rate 8000
SPEECH_SRC := $(wildcard sndtest/sh/speech_bank8.c sndtest/speech/manifest.json)
sh_src/speech.bin sh_src/speech_tab.h: tools/speech_blob.py $(SPEECH_SRC)
	python3 tools/speech_blob.py sndtest/sh/speech_bank8.c sndtest/speech/manifest.json sh_src/speech.bin sh_src/speech_tab.h
ifdef R60SPEECH
sh_src/r60/r60_main.o: sh_src/speech_tab.h sh_src/r60/r60_speech.inc sh_src/r60/r60_lockprobe.h
endif
# THE SFX BANK IS BAKED FROM MAME CAPTURES IN build/sfx, WHICH ARE SEGA-DERIVED AND
# GITIGNORED, so a tree without them cannot make sh_src/sfx.bin. Only r60_speech.inc
# includes sfx_tab.h and it is compiled only under R60SFX, so these dependencies belong
# INSIDE the flag -- unconditional, they broke every build on a tree without the
# captures (builder, 2026-10-19; `max() iterable argument is empty` out of sfx_bake.py).
ifdef R60SFX
sh_src/r60/r60_main.o: sh_src/sfx_tab.h
sh_src/sound_data.o: sh_src/sfx.bin
endif
SFX_SRC := $(wildcard build/sfx/arc_*.wav)
sh_src/sfx.bin sh_src/sfx_tab.h: tools/sfx_bake.py tools/speech_bake.py $(SFX_SRC)
	python3 tools/sfx_bake.py build/sfx sh_src/sfx.bin sh_src/sfx_tab.h --rate 8000
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
	@end=$$(grep -E ' __ramtext2_end$$' $@ | head -1 | cut -c1-8); \
	if [ -n "$$end" ] && [ $$((16#$$end)) -gt $$((16#06019000)) ]; then \
	  echo "FATAL: .ramtext2 end 0x$$end crosses SDRAM region base 0x06019000"; \
	  rm -f $@; exit 1; \
	fi
ifdef PORTHOT2
	@python3 tools/hot_check.py $< $(HOT_SET) sh_src/portfull/objdispatch.c tools/hot_dispatch_keys.txt || { rm -f $@; exit 1; }
endif

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
	@# PAD TO 8 MB, matching sh_src/mars.ld's rom region and the image guard below.
	@# It was 0x400000, and once the port sections moved above 4 MB the pad became a
	@# no-op: the file ended at the last section, dd conv=sync filled the block with
	@# NULs, and build_id's `tail pad not free` assert fired on a tail that was 0x00
	@# where it expects 0xFF (builder, 2026-10-20).
	@$(SHOBJC) -O binary --gap-fill=0xff --pad-to=$(CART_PAD) $< temp.32x
	@# DROPPED-SECTION GUARD (2026-09-17). objcopy -O binary emits only
	@# ALLOC sections. A section declared `.section .foo` without the "a"
	@# flag gets CONTENTS,READONLY and no ALLOC: ld still places it
	@# exactly where the linker script says, every placement ASSERT
	@# passes, _end is right -- and the bytes never reach the cart.
	@# .tilesmd lost 425KB to this and the feature was silently inert
	@# for an evening, passing every gate because it WAS the baseline
	@# (LESSONS.md "An .incbin blob needs .section name, \"a\"").
	@bad=$$($(SHOBJD) -h $< | awk '/^ *[0-9]+ \./ { n=$$2; sz=$$3; getline f; \
	    if (sz != "00000000" && f ~ /CONTENTS/ && f !~ /ALLOC/) print n }' \
	    | grep -v '^\.\(debug\|comment\|note\|stab\)'); \
	if [ -n "$$bad" ]; then \
	  echo 'FATAL: section(s) carry CONTENTS but not ALLOC.'; \
	  echo '       objcopy -O binary DROPS these -- the data will NOT be in the cart,'; \
	  echo '       and a feature that reads it will read 0xFF gap fill and do nothing:'; \
	  for x in $$bad; do echo "         $$x"; done; \
	  echo '       Fix: give it the allocatable flag ->  .section NAME, "a"'; \
	  rm -f temp.32x $@; exit 1; \
	fi
	@hi=$$($(SHOBJD) -h $< | awk '/^ *[0-9]+ \./ { n=$$2; s=$$3; l=$$5; \
	    getline f; if (f ~ /LOAD/ && n != ".gamehigh") print l, s }' \
	    | while read l s; do echo $$((16#$$l + 16#$$s)); done \
	    | sort -n | tail -1); \
	if [ -n "$$hi" ] && [ $$hi -gt $$(($(CART_HI))) ]; then \
	  printf 'FATAL: cart image ends 0x%08x, past the 8MB cart at 0x00800000\n' $$hi; \
	  echo "(the SH-2 decodes 0x02000000-0x03FFFFFF and the MiSTer loader takes 16 MB,"; \
	  echo " so raise sh_src/mars.ld's rom LENGTH and this bound together -- but anything"; \
	  echo " the 68K reads must stay under 0x400000, which is all it can address)"; \
	  rm -f temp.32x $@; exit 1; \
	fi
	@dd if=temp.32x of=$@ bs=8192 conv=sync 2>/dev/null
	@rm -f temp.32x
	@# BUILD STAMP at file offset 0x3C0 (unused header pad): git hash +
	@# epoch + PRESSURE flag. Every savestate self-identifies its build
	@# (tools/build_id.py) — no more provenance arguments.
	@python3 tools/build_id.py stamp $@ $(if $(R60),R60,$(if $(K2FREE),K2FREE,$(if $(VISRFLIP),VISRFLIP,$(if $(FMGATE),FMGATE,$(if $(PKTSLIM),PKTSLIM,$(if $(PAL32),PAL32,$(if $(FLICKFUSE),FLICKFUSE,$(if $(FBSPR),FBSPR,$(if $(BANDSHIFT),SHIFT$(BANDSHIFT),$(if $(SPRBAKE),SPRBAKE,$(if $(DIRTYROW),DIRTYROW,$(if $(BLITNOLOAD),BLITNOLOAD,$(if $(BLITSOLO),BLITSOLO,$(if $(PRESSURE),PRESSURE,$(if $(SPROBE),SPROBE,$(if $(TAILPROBE),TAILPROBE,$(if $(WINSPLIT),WINSPLIT,$(if $(DRQPROBE),DRQPROBE,$(if $(MDPAYOFF),MDPAYOFF,$(if $(BLITSKIP),BLITSKIP,$(if $(SPRTRUNC),$(if $(CUT30),TRUNC_CUT30,SPRTRUNC),$(if $(CUT30),CUT30,normal))))))))))))))))))))))
	@python3 tools/build_id.py show $@
	@# COMPILED-BYTES GUARD (2026-10-04): the 68K art base as linked, not as
	@# written. -DPORT_FULL reached only the SH-2 compiler for a day; the source
	@# check (tilesmd-addr) cannot see that. tools/check_tilesmd_bytes.py.
	@python3 tools/check_tilesmd_bytes.py $@ $(if $(PORTFULL),--portfull,)

$(TARGET).elf: $(SHOBJS) $(LDSCRIPT) | $(ROMDIR)
	$(SHCC) $(SHLDFLAGS) $(SHOBJS) -o $@ $(SHLIBS)

$(ROMDIR):
	@mkdir -p $(ROMDIR)

# Patched arcade game body + boot RAM copy, .incbin'd by mars_start.s
md_src/md_start.o: md_src/game_irq.h    # GAME_IRQ4 comes from the patcher
md_src/game_body.bin md_src/boot_copy.bin md_src/game_high.bin md_src/pal_thunks.h md_src/fmgate_tab.h md_src/game_irq.h md_src/wram_code.h &: $(GAMEROMS)/prog68k.bin tools/patch_game.py games/$(GAME)/game.py $(FLAGSTAMP)
	@GAME=$(GAME) MISSKEEP=$(MISSKEEP) SCENESEL=$(SCENESEL) MDHSCR=$(MDHSCR) MDSPRPROBE=$(MDSPRPROBE) FBSPR=$(FBSPR) FBTEXT=$(if $(FOLD5),,$(FBTEXT)) PAL32=$(PAL32) FMGATE=$(FMGATE) K2FREE=$(K2FREE) R60=$(R60) TXTWRAM=$(TXTWRAM) FBXPEND=$(FBXPEND) GAMEGATE=$(GAMEGATE) TXTMASK=$(TEXTCAPMASK)$(TEXTMASKPKT) PAL_APOST=$(PALAPOST) MISSKEEP=$(MISSKEEP) RELBANK=$(RELBANK) PASSCOUNT=$(PASSCOUNT) FRAMEDONE=$(FRAMEDONE) TAILCENSUS=$(TAILCENSUS) python3 tools/patch_game.py
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
# TILESMD_SKIP RIDES THE FLAG, not my shell. Under MDATTRACTSPLIT the attract is picked by
# its own key, so tables 5-9 -- the harvested attract scenes -- are unreachable: MD_ROUND_GET
# masks the round with 7 so 8 and 9 never were, and rounds >= 5 clamp to 0. Their art is 64
# blocks of 2 KB and skipping it is what makes table 10's 42 blocks fit (SCENE-KEYED-COST 4).
# They stay in the header because round_split.py derives table 10's pen maps FROM them.
ifdef MDATTRACTSPLIT
TILESMD_SKIP ?= 5,6,7,8,9
endif
sh_src/tiles_md.bin: tools/bake_tiles_md.py sh_src/tiles.bin sh_src/pal_rounds_md.h
	@TILESMD_SKIP=$(TILESMD_SKIP) python3 tools/bake_tiles_md.py --emit
sh_src/tiles_md_data.o: sh_src/tiles_md.bin

# Fold 1's hole map (LOOP29 244): two bits per tilemap cell, from the rom
# tilemap and the tile art, .incbin'd by cat1hole_data.s under C1PUNCH.
sh_src/cat1hole.bin: tools/bake_cat1hole.py sh_src/tiles.bin $(GAMEROMS)/prog68k.bin
	@GAME=$(GAME) python3 tools/bake_cat1hole.py
sh_src/cat1hole_data.o: sh_src/cat1hole.bin
sh_src/cat1mask.h: tools/bake_cat1mask.py sh_src/cat1hole.bin sh_src/tiles.bin
	@GAME=$(GAME) python3 tools/bake_cat1mask.py

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
                $(wildcard games/$(GAME)/discover/*.csv)
	@python3 tools/bake_sprites.py
sh_src/sprbake.h: sh_src/sprbake.bin
	@:
sh_src/sprbake_data.o: sh_src/sprbake.bin

# P3: MD sprite art blob + both indices regenerate from the census
sh_src/md_sprart.bin sh_src/md_sprart.h md_src/md_sprart_info.h: \
		tools/bake_mdspr.py games/$(GAME)/discover/play.csv
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
tables: romset sh_src/tiles.bin sh_src/sprites.bin sh_src/sprbake.bin \
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

# ---- LINT SUPPORT (2026-09-17) ----
# Print the EXACT -D sets a build compiles with, WITHOUT building.
# `make -n` is not usable for this (LESSONS "make -n REWRITES
# .build_flags"), and neither is this: the FLAGSTAMP `$(shell ...)` above
# runs at PARSE time on every invocation, so any make rewrites the stamp.
# tools/lint.sh saves and restores it around these targets. The recursion
# mirrors `line:` -> `ship-us:` exactly so the two cannot drift.
.PHONY: print-defs line-defs shipus-defs lint lint-generic lint-32x
print-defs:
	@echo "MD $(MDCCFLAGS)"
	@echo "SH $(SHCCFLAGS)"
shipus-defs:
	@$(MAKE) --no-print-directory GAME=$(GAME) $(SHIP_US) print-defs
line-defs:
	@$(MAKE) --no-print-directory $(LINE_FLAGS) shipus-defs
# THE SHIPPING SET since the port (2026-10-29): `make candidate` = line + PORT_FLAGS +
# CANDIDATE_FLAGS. line-defs alone is the September 68K-shim line, which nothing ships.
candidate-defs:
	@$(MAKE) --no-print-directory $(LINE_FLAGS) $(PORT_FLAGS) $(CANDIDATE_FLAGS) shipus-defs

# `make lint` = the whole health gate. See tools/lint.sh.
lint:
	@tools/lint.sh
lint-generic:
	@tools/lint.sh --generic
lint-32x:
	@tools/lint.sh --32x

# The blob's cart address lives in TWO files: the AT() in sh_src/mars.ld
# and the 68K's recompute in md_src/md_main.c. A silent mismatch makes
# the 68K fetch gap fill and render garbage a long way from its cause,
# which is exactly how .tilesmd's missing "a" flag cost an evening.
#
# AT THE END OF THE FILE ON PURPOSE. Defined up among the flag blocks it
# became the FIRST target in the Makefile and therefore the DEFAULT
# GOAL, so a plain `make` ran the check and built no rom at all -- which
# is how it broke `make line` the moment it was added.
.PHONY: tilesmd-addr
tilesmd-addr:
	@python3 tools/check_tilesmd_addr.py

# ---- PORTFULL generated objects (kept at the END of this file on purpose:
# an explicit rule placed earlier becomes make's DEFAULT GOAL, and this one
# silently did -- `make line PORTFULL=1` built port.o, stopped, and exited 0) ----
# port.o is produced by the feedback loop in cport_build.sh, not by a pattern rule:
# the corpus needs several prep/compile passes to close Ghidra's undeclared list.
# ONE DEFINITION OF THE CHAIN, because running these four steps by hand drifted
# twice: the dispatch table was generated from a different root set than the unit,
# so a state routine was in neither and every state call missed. CPORT_HANDLERS
# must be the SAME file for cport_build.sh and cport_dispatch.py.
CPORT_ROOTFILE = docs/proof/indirect_roots.txt

# THE ROOT SET. Four roots are the frame body as the L1 window captured it. The
# eleven that used to follow them were the 2026-10-01 flat-list experiment and are
# GONE: the arcade's frame loop has branches, so a flat unconditional sequence runs
# calls the arcade skips, and the loop measured every length as monotonically worse
# (FULL-PORT.md 32, docs/design/PORT-ARCH.md 1).
COMMA := ,
CPORT_ROOTS_BASE = FUN_00003aae,FUN_0000144a,object_dispatcher,irq4_handler
CPORT_STUB_WAIT = wait_frame_flag

# `make ... PORTMAIN=1` = ROOT THE ROTATED GAMEPLAY FRAME (`frame_rotated`, 0x0AE2).
# The decompiled code then does its own branching instead of the driver
# reimplementing the sequence.
#
# 0x0AE2 AND NOT 0x005BE. The per-frame loop is ROTATED around its vsync --
# `0xAE2 -> tail -> 0xB7C -> 0x97C -> body -> 0xADC (jsr 0x397E)` -- and the L2
# captures are taken while the 68K is suspended in that wait, so one arcade frame
# BEGINS at 0xAE2. Entering at 0x005BE runs the state-initialisation pass instead:
# measured 2026-10-02, stack top 0x0094 against the arcade's 0x0AE2, 1163 wrong,
# net +1111, of which 868 cells are RAM that pass legitimately cleared. Entering at
# the loop head 0x97C would omit the tail, including `bsr 0x144A` at 0xB04.
# tools/ghidra/split_frame_body.py made it a function; FULL-PORT.md 37-38.
#
# HOW ONE FRAME IS EXTRACTED FROM A LOOP THAT NEVER EXITS: wait_frame_flag (0x397E)
# is REDIRECTED to port_frame_wait in sh_src/port_l2.c, which __builtin_longjmps
# out at the FIRST yield. There is no prologue to work around at this entry, so the
# driver seeds work RAM before the call exactly as it always did. Control is
# INVERTED: the game loop drives and the driver is its vsync.
ifdef PORTMAIN
CPORT_ROOTS_SET = $(CPORT_ROOTS_BASE),frame_rotated$(if $(PORTBOOT),$(COMMA)FUN_0000040e)
CPORT_STUB_WAIT = wait_frame_flag->port_frame_wait,FUN_0000e16a->port_despawn_index,walk_player_slots->port_walk_player_slots,FUN_00003774->port_bcd_score_add,FUN_00003716->port_score_tick,FUN_0000d80e->port_spawn_walk
SHCCFLAGS += -DPORT_MAINLOOP
else
CPORT_ROOTS_SET = $(CPORT_ROOTS_BASE)
endif

# `make ... PORTVIDEO=1` = THE OUTPUT PATH. The renderer stops capturing the 68K's
# staging into the two shadows the port now writes itself.
#
# The renderer has never read System 16 memory: it reads four SDRAM shadows the
# patched 68K fills through tools/patch_game.py remap(). tools/cport_layout.py places
# the port's own S16 video writes at exactly those four addresses (parsed out of
# sh_src/m_main.c, each size the distance to the next define, so a renderer-side move
# carries the port with it). So the output path is a LAYOUT DECISION, not a transport
# -- there is nothing to packetise, nothing to DREQ, and no per-frame cost at all.
#
# STEP 1 COVERS TILEMAP AND SPRITES, the two that make a picture and the two with
# exactly one capture site each (cap_page, and text_capture's FB_SPR loop). Text and
# palette stay on the 68K path: their delivery is fused into the R60 packet lift's
# change detection (PAL_SETGEN, cram_paint_spr), so replacing the producer there is a
# delta scan rather than a gate. A PORTVIDEO=1 build therefore draws the PORT's
# background and actors with the old path's colours and text -- a mixed picture on
# purpose, because it is one that can be looked at and compared.
ifdef PORTVIDEO
SHCCFLAGS += -DPORT_VIDEO
endif

# `make ... PORTPLAY=N` = THE PORT DRIVES THE DISPLAYED FRAME. Not a gate: no
# scoring, no capture diff. Seed work RAM from the capture ONCE at frame N, then run
# one `frame_rotated` per displayed frame forever, feeding the capture's own recorded
# controller stream. The renderer (PORTVIDEO=1) draws from the shadows the port wrote.
#
# WHY THIS EXISTS. Every PORTFULL build before 2026-10-03 was a MEASUREMENT build:
# the SH-2 really ran the decompiled game, but it was scored against work-RAM
# captures and drew nothing, while the 68K kept the picture alive beside it. Mike,
# 2026-10-03: "am I seeing actual SH-2 ported code running altered beast on the risc
# processors? I'm inclined to think you are still building a pipeline". The score was
# the only output that existed, so that read was fair. This flag makes the port the
# producer of a PICTURE.
#
# WHY THE CAPTURE'S INPUT AND NOT THE PAD. The port's I/O cells (DAT_00c41003 and
# friends) are filled by l2m_io_apply from the capture; a live pad needs an MD->SH-2
# relay that does not exist yet (the 68K writes MD RAM 0xFFB013, which the SH-2
# cannot read). Seed + recorded input needs NO new transport and rides the free-run
# path that already measures net-correct for 250 frames, so it is the shortest route
# to a frame on screen. Live pads are the next step, not this one.
#
# Requires PORTMAIN=1 (the rooted frame) and wants PORTVIDEO=1 and NOGAMEHIGH=1.
ifdef PORTPLAY
SHCCFLAGS += -DPORT_PLAY=$(PORTPLAY)
endif

# `make ... PORTCLK1=1` = ONE GAME FRAME PER MASTER VINT, no drain.
#
# pp_due is a COUNT of vints owed (cap 4) drained by a while loop, so a long master
# body (a scene cut) makes the game run 2-4 times in one master frame. Every
# irq4-driven counter advances once per game frame -- the blink timer 0xFFF020/21, the
# seconds counter, the colour-cycle index -- so a burst advances them faster than any
# arcade vblank can, and the phase never comes back (measured 2026-10-05: +19 steps by
# demo A, constant inside the scene, acquired at the boot and the two attract cuts).
# Under this flag the cap is 1 and the backlog is DROPPED rather than caught up: the
# game runs slower in wall time than the arcade, which it already does, but never
# faster. pp_clk_dbg[0] counts master frames that had more than one game frame waiting.
ifdef PORTCLK1
SHCCFLAGS += -DPORT_CLK1=$(PORTCLK1)
endif

# `make ... PORTHIST=1` = THE OWED-VINT HISTOGRAM, pp_hist[8] indexed by the vints one
# master frame body spanned (7 = 7 or more). 1 means the port kept arcade time that frame;
# 3 means the body was three vints long and two game frames were dropped. Read it with a
# dump of pp_hist at two frames and difference them -- that is the per-scene histogram.
# Costs one add and one store per body. Not on by default; it is an instrument.
ifdef PORTHIST
SHCCFLAGS += -DPORT_HIST=$(PORTHIST)
endif

# `make ... PORTTILEDIRTY=1` = THE PORT NAMES THE TILEMAP PAGE IT WROTE, at the store site.
# CORRECTED 2026-10-11: this comment used to say the marks come from w68.h's A68VID/A68VIDA.
# THEY DO NOT, and the block at the ifdef below plus STATE.md:150-157 have said so since the
# measurement: marking from the address macros caught 6 of 90 page changes, 93% missed,
# because the port keeps already-translated SH-2 pointers in work RAM and never re-forms a
# 68K address. The marks are in the STORE LOOPS, as hand files: tilemap_unpack_hi/lo,
# tilemap_block_04, tilemap_block_a5, FUN_0000258a and (2026-10-11) FUN_00002580.
# AND THE FLAG DOES NOT YET BUY SPEED: under PORT_TILEDIRTY the 13-page hash STILL RUNS
# (m_main.c ~13934) and the flag only adds the census beside it -- pp_hash_dirty,
# pp_mark_miss, pp_miss_pages, pp_mark_dirty. Deleting the hash is a separate change and
# must not be made until pp_mark_miss is 0, because a missed mark drops a tilemap update.
# Marks on reads too: a spare page upload costs a slot, a missed one costs a wrong picture.
# `make ... PORTFFBM=1` = THE FB RESTORE COPIES THE PAGES THAT MOVED. ff_bm was
# cycle_dirt | pg_watch and pg_watch is never cleared in a PORT_VIDEO build (cap_page
# clears it only in the copy path that flag returns before), so flip_finish_rest restored
# all 13 pages on every flip -- measured 13.00 of 13, 378 FRT a page. The content hash
# already knows which pages moved; this restores that set, unioned over two flips so both
# banks get every dirty page.
# `make ... PORTTICKFREE=1` = THE GAME TICK RUNS WHENEVER A FRAME IS OWED, posted window or
# not; only the renderer body stays gated on COMM0. 76% of the port's lost vints were passes
# that did nothing but re-read c0 (2026-10-06), and on ares it took the port clock x1.026 ->
# x1.005 with every gate green.
#
# IT IS OFF, AND HARDWARE IS WHY (2026-10-06, Mike at the FPGA rig). Three roms, one rig,
# same tree: both flags on (pc9def) showed SPRITES ONLY, NO BACKGROUND, about one frame
# every 20 seconds; both off (pc9ctl) one frame every 3 seconds; this flag off with
# PORTFFBM on (pc9ff) FULL-SCREEN GRAPHICS and more drawn than the control. The pair that
# differs only in this flag is pc9def against pc9ff, so it is this flag that takes the
# background away and the frame rate with it. ares cannot see it: it charges instruction
# cycles only (no SDRAM or cart-fetch wait), so running MORE game frames out of cart-
# resident .portcode is free there and ruinous on silicon.
# `make ... PORTRIG=1` = TICKS PER SECOND AND THE 1-VINT SHARE, PAINTED INTO THE PICTURE.
# The rig has no memory dump, a CRAM flood destroys the BG palette for the rest of the run
# (LESSONS 830 rule 2) and the name-table barcode writes CRAM 61-63 itself, so the only
# channel left while the background is under investigation is the 32X framebuffer. Counters
# in probe scrap at 0x26038000 (no .bss: that is why every earlier rig instrument failed to
# link), two bars in sbuf rows 224..231 = screen rows 216..223, read with tools/rig_bars.py
# off the rig's own screenshot. SH-2 only; the 68K side is untouched. NEVER SHIP -- it paints
# over eight rows of the picture, and the picture check masks exactly those rows.
ifdef PORTRIG
SHCCFLAGS += -DPORT_RIG=1
endif

# `make ... PORTGENSEED=1` = SEED PAL_SETGEN NEAR ITS 16-BIT CEILING, to reproduce the
# rig's 57-minute black screen in seconds. See m_main.c's note at the seed. NEVER SHIP.
ifdef PORTGENSEED
SHCCFLAGS += -DPORT_GENSEED=1
endif

ifdef PORTTICKFREE
SHCCFLAGS += -DPORT_TICKFREE=$(PORTTICKFREE)
endif

ifdef PORTFFBM
SHCCFLAGS += -DPORT_FFBM=$(PORTFFBM)
endif
# `make ... PORTIRQ4OWED=1` = THE GAME'S VBL HANDLER RUNS ONCE PER OWED VINT, not once per
# game frame, so its own latch test (0xFFF01C) decides whether to publish -- the arcade's
# behaviour, where IRQ4 fires regardless and the handler skips its work while the game is
# busy. Fixes the clock freeze the arcade shows across a scene teardown and the port does
# not. Measurement first: it changes every irq4-driven counter.
ifdef PORTIRQ4OWED
SHCCFLAGS += -DPORT_IRQ4OWED=$(PORTIRQ4OWED)
endif

# `make ... PORTTILEHASH=1` = PUT THE 13-PAGE CONTENT HASH BACK, as the ORACLE. The line
# takes its dirty set from port_tile_dirty (the store-site marks) since 2026-10-12; this
# restores the hash beside it so the two can be A/B'd. The hash costs 25.7% of the master
# body (PORT_HIST, demo A) and reads 53 KB of SDRAM per game frame to learn what the marks
# already know -- but a missed mark is a WRONG PICTURE, not slow pixels, so it stays
# reachable. With PORTTILEDIRTY it also runs the miss census (pp_mark_miss / pp_miss_adj /
# pp_miss_never), which must read 0/0/0 before the marks are trusted.
# `make ... PORTROTATE=1` = ADAPTIVE ROTATION. MEASURED WORSE 2026-10-12 and OFF by default:
# at the matched demo-A tick it took MOTION 8 -> 6, updates 29 -> 26 and longest hold 3 -> 5.
# CAUSE: plane B was skipped 45 times in 100 vints and walked ZERO times -- the backstop is
# 16 ROTATIONS, and a rotation still advances while B is skipped, so 16 rotations is ~144
# vints of frozen BG and the backstop never fired in any measured window. The next attempt
# must bound the skip per ROTATION (at most N of the 4 B phases), not per 16 rotations.
# The idea it tests is still live: a
# plane-B chunk whose pages carry no pending mark and whose coarse scroll has not moved is
# spent on a plane-A band instead, because the BG is hardware-scrolled and ships almost
# nothing: 30 cells against plane A's 507 over nine frames of demo A, and zero in a window
# where hscroll did not move. The FG is revisited once per 9 vints otherwise, which is
# 60/9 = 6.67 Hz and is exactly the "about 7 frames a second" Mike reports. Plane B is still
# walked at least once every 16 rotations whatever the signals say.
ifdef PORTROTATE
SHCCFLAGS += -DPORT_ROTATE=$(PORTROTATE)
endif
ifdef PORTTILEHASH
SHCCFLAGS += -DPORT_TILEHASH
endif
ifdef PORTTILEDIRTY
SHCCFLAGS += -DPORT_TILEDIRTY
# AND THE PORT UNIT, which tools/cport_build.sh compiles with its OWN flag line and not
# SHCCFLAGS: the first build of this defined the macro everywhere except the only
# translation unit that calls it, and measured zero marks against 90 hashed changes.
# The flag is the RENDERER's half and is inert until the port marks the page at its store
# sites; see port_tile_dirty in sh_src/m_main.c for why the address macros cannot.
PORT_TRACE_CF += -DPORT_TILEDIRTY
endif

# `make ... PORTBOOT=1` = ROOT THE 68K RESET VECTOR, so the port can COLD BOOT.
#
# The reset vector is 0x400 and holds `bra.w 0x40E` (prog68k.bin, verified
# 2026-10-03), so FUN_0000040e is the game's real power-on entry: it clears work RAM
# 0xFFC000 for 0xF00 longwords, calls clear_textram / clear_tileram /
# clear_spriteram_and_pool / set_level_palettes, and falls into main_entry.
#
# WHY IT WAS MISSING. Every port build so far rooted at `frame_rotated` -- one
# gameplay frame -- because the gate seeds work RAM from an arcade capture and only
# ever needed the frame that follows. That is also why a PORTPLAY build drew nothing
# but sprites: a mid-level capture restores WORK RAM, and the tilemap, text layer and
# palette were built by the init long before the capture starts, so they were never
# in the seed. Measured 2026-10-03: the port wrote real sprite records into SPR_SNAP
# and left TILEMAP_U / TEXT_U / PAL_SH at the 0xFF poison fill.
#
# This root pulls the whole init closure in, so expect it to cost cart space.
ifdef PORTBOOT
SHCCFLAGS += -DPORT_BOOT
endif

# `make ... PORTRESEED=N` = cold boot, then take the arcade's captured state at tick
# N. Needs PORTBOOT and a capture (L2DIR). See sh_src/port_l2.c for why neither half
# works alone: the boot draws but hangs, the seed advances but renders black.
ifdef PORTRESEED
SHCCFLAGS += -DPORT_RESEED=$(PORTRESEED)
endif

# `make ... PORTCORO=1` = frame_rotated YIELDS at each wait_frame_flag and RESUMES
# where it stood, on its own 8 KB stack (.portstack, sh_src/mars.ld), instead of
# being re-entered from its root every frame.
#
# Free-run already runs by re-entry at 0x0AE2 -- there is no prologue there to erase
# state, and the game's state lives in work RAM. What re-entry throws away is the
# SH-2 CALL STACK, every frame. The 68K's own stack is faithful (emulated in work RAM
# through A7, reloaded per frame because the arcade's loop reloads it at 0x62A), but
# anything the decompiler kept in an SH-2 local -- at -Os, most of it -- is gone at
# the boundary.
#
# THE HYPOTHESIS: free-run diverges (+31.7 B/frame vs the control over 176 frames)
# while per-frame is solidly correct (-25.6), and a per-frame error that only
# compounds is the signature of state lost at the boundary. One run of
# tools/portl2_endure.py settles it. Requires PORTMAIN=1.
ifdef PORTCORO
SHCCFLAGS += -DPORT_CORO
SHASFLAGS += --defsym PORT_CORO=1
endif

# `make ... PORTBLANKLATCH=1` makes the load-blank hold burn its vints WITHOUT
# clearing the game's frame latch 0xFFF01C, which is what the arcade does: its main
# loop is suspended inside the load and never reaches wait_frame_flag, so every IRQ4
# takes the `latch set -> do nothing' branch and 0xFFF020/21 PARKS. The hold used
# port_frame_wait, which IS wait_frame_flag and clears the latch, so each held vint
# published and advanced -- the hold bought the arcade's 21 frames of wall time and
# handed the game 21 frames of clock the arcade never gave it.
#
# MEASURED 2026-10-20, port game frame minus arcade game frame at the same wall
# frame over the whole attract: +8 at boot, flat through X/T/E; +19 at the E->A
# install -> +27 and FLAT for 600 frames of demo A; +3 at the H install -> +30;
# +19 at the H->B install -> +49 and FLAT for 600 frames of demo B; +3 at the B end.
# The rate is right inside every scene. The two +19s are sites 3/4, this flag.
ifdef PORTBLANKLATCH
SHCCFLAGS += -DPORT_BLANKLATCH
endif

# `make candidate NAME=x EXTRA='PORTGODMODE=1'` holds P1 lives (0xFFE018) at 3 while in
# play (0xFFF028 bit0). MEASUREMENT ONLY, never on a candidate for Mike: it mirrors the
# arcade oracle's GD_GOD rule (tools/gameplay_dump.lua, tools/arcade_level_run.lua:33).
ifdef PORTNOPALPROBE
SHCCFLAGS += -DPORT_NOPALPROBE   # the palette-collapse detector (port_pal_probe, 2026-10-04) off the tick: two 128-set uncached scans + a 256-byte WRAM scan a tick, ~5.7 master lines a vint (ares profile, fight 600-700); it only writes port_watch_out[180..188]
endif
ifdef PORTGODMODE
SHCCFLAGS += -DPORT_GODMODE
endif
# PORTTAPEPAD=<tape.csv> PORTTAPEARCADE=<arcade corpus dir> = MEASUREMENT ONLY: the input
# tape compiled into the rom and applied by game frame at each tick (sh_src/port_l2.c
# l2m_io_tape), because ares injects input per wall frame and lands it 0-1 game frames late.
# Needs R60LIVEPAD (the port's pad cells) and the tick-end counter; parity_play --rom-tape.
ifdef PORTTAPEPAD
SHCCFLAGS += -DPORT_TAPEPAD
# THE HEADER IS KEYED ON THE TAPE'S IDENTITY, not only its mtime: a second tape OLDER than the
# header left by the first was never regenerated, and a level-5 fight replay ran the escape tape's
# inputs (no punch or kick) through a whole parity run (2026-10-28). sh_src/tape_pad.id holds the
# tape path, corpus path and tape hash, rewritten only when one of them changes.
TAPEPAD_ID := $(PORTTAPEPAD) $(PORTTAPEARCADE) $(shell shasum $(PORTTAPEPAD) 2>/dev/null | cut -c1-16)
$(shell [ "$$(cat sh_src/tape_pad.id 2>/dev/null)" = "$(TAPEPAD_ID)" ] || printf '%s' "$(TAPEPAD_ID)" > sh_src/tape_pad.id)
sh_src/port_l2.o: sh_src/tape_pad.h
sh_src/tape_pad.h: tools/tape_pad.py sh_src/tape_pad.id $(PORTTAPEPAD) $(PORTTAPEARCADE)/timeline.csv
	python3 tools/tape_pad.py $(PORTTAPEPAD) $(PORTTAPEARCADE) $@
endif
# `make candidate NAME=x EXTRA='PORTJUMP=0x1A406 PORTJUMPAT=1500'` = PROBE ONLY: at game tick
# PORTJUMPAT the game thread is discarded and a fresh one starts at the 68K routine PORTJUMP
# (only 0x1A406, the crystal-ball scene, is wired in coro_entry). Reaches a scene the tapes
# cannot (the tick lottery), for renderer work. Never on a candidate for Mike.
ifdef PORTJUMP
SHCCFLAGS += -DPORT_JUMP=$(PORTJUMP) -DPORT_JUMP_AT=$(if $(PORTJUMPAT),$(PORTJUMPAT),1500)
endif

# `make ... PORTFORM=N` = PROBE ONLY: raise P1's target form byte 0xFFC06B to N while in play
# (sh_src/port_l2.c); the game's own form-change dispatch walks the beast up. Never on a candidate.
ifdef PORTFORM
SHCCFLAGS += -DPORT_FORM=$(PORTFORM)
endif
# `make ... PORTROUND=N` = PROBE ONLY: credited play starts at round N (0xFFF14E/0xFFF142 = N,
# 2 = level 3) through the game's own round start (sh_src/port_l2.c PORT_ROUND); the arcade
# side is tools/gameplay_dump.lua GD_ROUND=N. Unlocks levels 2-5 for tapes. Never on a candidate.
ifdef PORTROUND
SHCCFLAGS += -DPORT_ROUND=$(PORTROUND)
endif
# `make ... PORTENDCLEAR=1` = PROBE ONLY: at a scroll lock (0xFFF152 set) clear the enemy
# band so the lock releases and the end-of-level boss transforms on a tape that cannot aim
# (sh_src/port_l2.c PORT_ENDCLEAR; twin: gameplay_dump.lua GD_ENDCLEAR=1). Never on a candidate.
ifdef PORTENDCLEAR
SHCCFLAGS += -DPORT_ENDCLEAR
endif
# Scope FORM/ENDCLEAR diagnostics to one zero-based round, allowing a real
# transition into later rounds with both fixtures disabled. Never a play build.
ifdef PORTPROBEROUND
SHCCFLAGS += -DPORT_PROBE_ROUND=$(PORTPROBEROUND)
endif
# `make ... PORTHOLDLIVES=1` = PROBE ONLY: hold the lives counter 0xFFE018 at 3 every
# tick, so an unattended scripted run reaches level 2 instead of dying in level 1.
# tools/arcade_level_run.lua does the same from lua on the MAME side; ares-headless
# cannot write memory mid-run, so the port side has to be built with it. Never ship it
# and never measure a rate under it.
ifdef PORTHOLDLIVES
SHCCFLAGS += -DPORT_HOLDLIVES
endif

# `make ... PORTKINDCENSUS=1` = PROBE ONLY: accumulate the distinct handlers that ever
# run in the ENEMY BAND, slots 16-39, keyed by the round in 0xFFF142. Combat diverges
# between the port and the arcade within seconds, so the set of kinds that spawn is the
# comparable thing and a record diff is not; and one end-of-run dump cannot see it,
# because the band is empty at most instants. Read port_kind_seen / port_kind_n.
ifdef PORTKINDCENSUS
SHCCFLAGS += -DPORT_KIND_CENSUS_UNUSED
SHCCFLAGS += -DPORT_KINDCENSUS
endif

# `make ... PORTPFSLOT=1` points the per-slot tracker at the PER-FRAME curve instead
# of free-run. Free-run per-slot cannot distinguish "slot 16 owns a defect that
# compounds" from "slot 16 inherits the player's error"; the per-frame curve re-seeds
# from the arcade every frame, so a large reading there means its own defect and a
# near-zero reading means purely downstream. Those need opposite work.
ifdef PORTPFSLOT
SHCCFLAGS += -DL2M_SLOT_PERFRAME
endif

# `make ... NOGAMEHIGH=1` omits the 256 KB REBASED 68K image (.gamehigh). Under the
# SH-2 port the 68000 does not execute the game -- it keeps the i8751 shim, the VDP
# and the framebuffer handoff, and the logic runs on the SH-2 from .portrom +
# .portcode -- so that image is dead weight, and it is EXACTLY the 256 KB a playable
# port build is short. See sh_src/game_high_data.s for the measured budget.
ifdef NOGAMEHIGH
SHASFLAGS += --defsym NO_GAMEHIGH=1
SHCCFLAGS += -DNO_GAMEHIGH
# AND THE 68K SIDE, which is where it actually decides something. md_main's boot
# ends by jumping into the game at 0x00900400 -- an address that holds nothing when
# .gamehigh is omitted -- so without this the 68K left for garbage with interrupts
# masked and shim_vblank, the handler that posts the renderer's per-frame announce,
# never ran again. The flag was SH-2-only, so every md_main.c guard written for it
# was silently compiled out.
MDCCFLAGS += -DNO_GAMEHIGH
endif

# `make ... CPORT_COUNT=FUN_x,FUN_y` = insert an entry counter at the top of each
# named function in the generated unit, published at port_l2m_out[248..255].
#
# The port-side COUNTSITES. It exists because PORTWATCH names the function whose span
# CHANGED a cell, so a function whose writes are IDEMPOTENT on the current state
# reads as zero spans -- indistinguishable from never having run. The camera update
# FUN_000039C8 is exactly that with the camera frozen, and watching any of its
# writes is circular. Max 8 names.

# `make ... PORTOS=1` = compile the decompiled unit at -Os. REQUIRED with PORTMAIN:
# the main loop's closure is 716 functions and does not fit .portcode at -O2.
ifdef PORTOS
CPORT_OPT_SET = -Os
SHCCFLAGS += -DPORT_OS
else
CPORT_OPT_SET = -O2
endif

# CPORT_CLAMP: declarations narrowed to the width the ARCADE was observed to use,
# one address at a time so the gate arbitrates each. FFFFF142 was the first (the mode
# byte); FFFFF132 is the second -- declared uint, observed word, and FUN_0000E052
# assigned it into a ushort, so the port took the WRONG HALF and walked its table 172
# entries too far (FULL-PORT.md 41).
# FFFFF132 IS NOT IN THIS LIST AND MUST NOT BE. It is a 32-BIT fixed-point camera
# position -- FUN_000039C8 does `_DAT_fffff120 = -((short)((uint)_DAT_fffff132 >> 0x10)
# + -0x1000)`, the integer part of a long whose fraction lives at 0xFFF134. Narrowing
# it to a word makes that shift always zero and the camera dies. The arcade's
# "observed width 2" is an UNDER-SAMPLE: the window saw the `move.w` reads of the top
# half and not the long arithmetic. FULL-PORT.md 43.
# NOT IN CPORT_ENV UNTIL 2026-10-03, AND SO NEVER APPLIED. CPORT_A6 was defined here
# and hashed into PORTSTAMP (so a change rebuilt the unit) but was never passed to
# cport_prep.py, which reads os.environ. The pass therefore ran with an EMPTY caller
# set and restored nothing from the day it was written. Found by chasing why the
# player's respawn wrote into whatever slot was dispatched last: FUN_000047CC does
# `lea 0xffffc000,%fp` and the table has that exact triple
# (FUN_000047cc -> FUN_00004870, A6 = 0xFFC000).
#
# The lesson is the one this Makefile already carries about `.build_flags`: a variable
# that is DEFINED, DOCUMENTED and HASHED still does nothing unless it is handed to the
# process that reads it, and a pass that silently applies to nothing looks exactly
# like a pass that applies correctly.
#
# CPORT_A6: callers whose A6 load Ghidra dropped, restored from
# docs/audit/a6_callsites.json. OPT-IN PER CALLER -- applying all twenty at once drove
# the game's own frame counter to -1217 of 8 on a free-run (FULL-PORT.md 52).
CPORT_A6 ?= all

# CPORT_CLAMP: declarations narrowed to the width the program is OBSERVED to use, one
# address at a time so the gate arbitrates each. The evidence is now
# docs/audit/width_census.json -- read out of the 68K's instruction encodings by
# tools/cport_widths.py, which sees every path, where the older
# docs/proof/datwidth_observed.json is one MAME scene's runtime accesses and cannot
# report a width that scene never ran.
#   FFFFF142  the mode byte (the first)
#   FFFFF028  player state
#   FFFFF400  declared uint, and the program only ever does `moveb` there (2 sites).
#             No score change in the gate's scene -- adopted because it is right, and
#             said plainly rather than implied to have moved a number.
# 0xFFD400 was tried and the clamp REFUSED it, correctly: it is slot 40's base, inside
# the object table, and narrowing an object-record FIELD would change the layout for
# all 64 slots. _clamp_allowed exists for exactly that.
CPORT_CLAMP ?= FFFFF142,FFFFF028,FFFFF400
# CPORT_HI: 32-bit cells whose accesses are really 16-BIT -- the HIGH half, because a
# 68000 .w access at a long's own address is its high half on a big-endian machine.
# `ADDR@FUN+FUN` exempts the functions that genuinely use the full 32 bits.
#
# NOT A HEURISTIC. The width census comes from the program's own bytes, via the 68K
# objdump in the tree: at 0xFFF132 there are 49 movew + 1 addw reads and 5 movew
# writes, against exactly ONE addl and ONE movel -- and both of those are the
# fixed-point advance inside FUN_000039C8 (addl 0xfffff132,%d0 ; movel %d0,0xfffff132 ;
# swap %d0). See tools/cport_prep.py.
# FFFFC014: the eye's X VELOCITY. The 68K writes it as a WORD into the HIGH half of
# the long at 0xFFC014 -- proven 2026-10-04 from every code access in prog68k.bin:
#     223E  31FC F400 C014   move.w #-0xC00,0xC014      (the hold-loop exit)
#     2272  D178 C014        add.w  d0,0xC014
#     24B4  4A78 C014        tst.w  0xC014
#     553C  42B8 C014        clr.l  0xC014              (FUN_00005530: the one long)
# Ghidra typed the cell uint, so `_DAT_ffffc014 = -0xc00` stored 0xFFFFF400 and
# integrate_position (0x3F24: movew @+0x14; extl; lsll #8; addl @+0x0C) read a
# velocity of -1 instead of -0xC00: the eye scrolled one unit every 256 frames
# instead of 12 a frame, and `while (0x1000 < _DAT_ffffc00c)` -- the attract's
# "eye has panned off" test -- never came true. Measured: X 0x1180 -> 0x117A in
# 1,600 ticks. FUN_00005530 is exempt because its access really is 32-bit.
CPORT_HI ?= FFFFF132@FUN_000039c8,FFFFC014@FUN_00005530

# CPORT_AREG: which 68K ADDRESS REGISTERS a function publishes into the live register
# file before an indirect call, by index (A0=8 .. A5=13). The dispatcher establishes A6
# and only A6, so every other address register a handler computes sits in a decompiler
# LOCAL that the dispatch thunk never sees. Ghidra's own variable STORAGE says which
# local is which register (tools/ghidra/areg_locals.py -> docs/audit/areg_locals.json);
# a textual rule cannot, because A6+0x3C alone is loaded into A0 25 times, A1 3 times
# and A5 8 times across the program.
#
# ONE REGISTER AT A TIME, arbitrated by the gate, like CPORT_CLAMP and CPORT_HI.
# Publishing every unambiguous A-register at once measured WORSE (the reproducer
# 68 -> 147 wrong), so "all" is not the answer.
#   13 = A5. Adopted 2026-10-03: it is what the player's respawn hangs off
#        (FULL-PORT.md 62-63), free-run -91.9 -> -94.5, per-frame -30.9 -> -31.1,
#        the one-frame reproducer 67 -> 47 wrong.
#    7 = D7. Adopted 2026-10-03: the player's handler reaches its SECOND state
#        dispatch with D7 = 0x40, and D7 decides whether the state routine NEGATES
#        the new velocity -- so the port negated one the arcade does not. The 250-frame
#        endurance went net -36 -> -102 on this one register. FULL-PORT.md 69.
#
# An AMBIGUOUS register (Ghidra split it into several locals) is resolved PER CALL
# SITE by the nearest preceding assignment, which is what the SSA naming means. D7 in
# FUN_000049BA has three locals and the live one at that call is uVar8.
CPORT_AREG ?= 13,7

# CPORT_DREG: callers whose CONSTANT register loads before an indirect call Ghidra
# dropped, restored from docs/audit/dreg_callsites.json (tools/dreg_callsites.py reads
# them out of the 68K's own bytes). The decompiler's liveness ends at `jsr (An)`, and
# this dispatcher deliberately THREADS D3/D6 handler to handler, so the callee runs on
# the previous handler's leftovers instead of the zero the caller wrote.
#
# FUN_00004870 adopted 2026-10-03: `moveq #0,D6 ; moveq #0,D7 ; jsr (A0)` at 0x49B6 is
# the player's respawn dispatch, and with a stale D6 the state routine set state 9 /
# script 0x7888 where the arcade keeps state 4 / 0x76B0. One-frame reproducer
# (PORTSEED=108) 47 -> 19 wrong, net -25 -> -53, slot 0 from 27 bytes to 5.
# `all` applies every row. FULL-PORT.md 64.
CPORT_DREG ?= FUN_00004870
# CPORT_WATCHFN: measurement only -- functions that get a port_watch_hook(0xADDR) call
# at entry (port_l2.c, PORT_FFWATCH). Empty on the line.
CPORT_WATCHFN ?=
# CPORT_F154LOG: measurement only, on with R60LOCKPROBE -- every write to the enemy count 0xFFF154
# goes through port_f154_note() (sh_src/port_l2.c), shown on the lock footer.
ifdef R60LOCKPROBE
CPORT_F154LOG = 1
endif
# CPORT_PROBE: measurement only -- a function whose state-label entries stamp the tick
# into port_probe_N globals (cport_prep.py); never on a candidate build.
CPORT_PROBE ?=
CPORT_PROBE_AT ?=
CPORT_PROBE_VARS ?=

# CPORT_ANYNORM: functions whose pointer-ish parameter can also carry a 68K PROGRAM
# address, so it is normalised with A68ANY (three domains) instead of A68NORM (two).
# FUN_0000d868 adopted 2026-10-03: the object allocator fills a new record from the
# SPAWN-TABLE ENTRY its caller passes, which lives in program ROM at 0x1D4D6 --
# A68NORM is the identity there, so the allocator dereferenced a 68K ROM address as an
# SH-2 one and 31 bytes of the record stayed zero. FULL-PORT.md 66.
CPORT_ANYNORM ?= FUN_0000d868

# CPORT_WIDTH: declared widths taken from the DISASSEMBLY, as ADDR=N. For cells the
# program reaches through a resolved pointer walk, no census of absolute operands can
# see the width -- but the instruction can be read. The spawn-init template at 0xDD86
# is five fields read as long/word/word/long/long, and Ghidra declared all five as 4
# bytes, so the two WORD reads became big-endian narrowings and took the LOW half:
# a spawned enemy with the wrong sprite bank and palette index. FULL-PORT.md 68.
# 0x4120/0x4124/0xFFFFF400: the palette-queue ring header, PROVEN 2026-10-03 from
# the init's own instructions -- two MOVE.L, so all three cells are 32 bits and
# Ghidra had typed them `byte`:
#     3b08: 41f8 f400    lea   0xfffff400,%a0
#     3b0c: 43fa 0612    lea   %pc@(0x4120),%a1
#     3b10: 20d9         movel %a1@+,%a0@+
#     3b12: 2091         movel %a1@,%a0@
# 0xFFF402 is a 32-bit pointer that STRADDLES 0xFFF400 and 0xFFF404, so a one-byte
# copy left it holding 0x00000010 where the arcade has 0xFFFFF600. With the widths
# forced the port's 0xFFF400..407 reads `00 ff ff ff f6 00 00 00`, byte-identical to
# the ROM constants, and the ring base is 0xFFFFF600 exactly.
# FFFFF400=4 REMOVED 2026-10-04: the 68K reads and increments 0xFFF400 as a BYTE
# (0x3B44 moveb, 0x3B4E addqb) -- the palette-slot counter -- and only the 8-byte
# INIT at 0x3B10 writes it as part of a long. With the cell forced to 4 bytes the
# compare `DAT_fffff400 < 0x3f` read the counter | 0xFFF401 | the queue pointer's high
# word and never allocated a sprite-palette slot: PAL_SH sets 64-79 absent in every
# port build (rd9-rd12). The init copy is rewritten by cport_prep.py instead.
# FFFFD400=2 (2026-10-04): the slot-40 object's flag is a WORD (`movew #0x8000,(fp)` at
# 0x16B6E with fp = 0xFFD400); as a uint the store put 00 00 80 00 and the slot never
# activated (arcade f947: slot 40 active with handler 0x16BF0; port: flag 0).
# FFFFD580=2 (2026-10-25): slot 43's flag is a WORD, both writers say so: `movew #0x8000,%a0@(0)`
# at 0x56C6 (a0 = 0xFFD580, the intro's typewriter object, handler 0x56E8) and `movew #0x8000,
# %fp@(0)` at 0x18A00 (fp = 0xFFD580, the fourth record 0x1892A spawns). Ghidra typed it uint, so
# the store put 00 00 80 00 and the handler's long at +2 then overwrote the 0x8000: the slot never
# went live, and "I COMMAND YOU TO RISE FROM YOUR GRAVE" was never typed (sw318, text rows 9/11
# zero through the whole intro; the arcade's 0xFFD580 reads 8000 56E8 from frame 1060).
# GHIDRA'S uRam/cRam/bRam NAMES (an access it resolved through a register-held base, so
# no absolute operand exists for the census) default to `uint` in dat_syms.h. The real
# width is the instruction's: measured 2026-10-04, `uRamffffdfa1 = 2` (0xF27A move.b
# #2,0x21(a5)) stored a 32-bit 2 over slot 63's +0x21..+0x24, so the record started at
# anim index 0 with state 2. Every WORK-RAM uRam cell below was read from its instruction
# (tools/cport_widths.py cannot see them): 0xFFC460-6/0xFFC4E0-6 = 0xF494../0xD618.. move.w
# (player 2's box words); 0xFFC48B 0x5430 move.b; 0xFFC4A0 0xD644 move.b; 0xFFD48A-0xFFD64E
# 0x17FE2.. move.b (palette copies, +0xb, +0x4e); 0xFFD822 0x5ED8 move.w; 0xFFD900 0x5EE6
# clr.w; 0xFFDE80/0xFFDEC0/0xFFDEC4 0x58E4/0x58F8/0x58FE move.w; 0xFFDE89/8B/A1/A2/AF
# 0x58F2..0x5914 move.b/clr.b; 0xFFDF21/0xFFDFA1 0xF256/0xF27A clr.b/move.b; 0xFFFC82/83
# 0x42B4/0x42BA move.b.
# 0x19846/0x19850 (2026-10-28): level-four boss velocity/acceleration
# constants are MOVE.W via A0 at 0x1925A/0x19264/0x1935E. Byte declarations
# read 0x01/0x00 instead of 0x0100/0x0004, so the vertical orbit never reverses.
CPORT_WIDTH ?= 00840000=2,0000DD8A=2,0000DD8C=2,0000DCEE=2,0000DCF0=2,0000DD3C=2,0000DD3E=2,0000DDD8=2,0000DDDA=2,00019846=2,00019850=2,0001984E=2,00004120=4,00004124=4,FFFFD400=2,FFFFD580=2,FFFFC460=2,FFFFC462=2,FFFFC464=2,FFFFC466=2,FFFFC48B=1,FFFFC4A0=1,FFFFC4E0=2,FFFFC4E2=2,FFFFC4E4=2,FFFFC4E6=2,FFFFD48A=1,FFFFD4EC=1,FFFFD4ED=1,FFFFD50A=1,FFFFD56A=1,FFFFD56B=1,FFFFD60B=1,FFFFD64E=1,FFFFD822=2,FFFFD900=2,FFFFDE80=2,FFFFDE89=1,FFFFDE8B=1,FFFFDEA1=1,FFFFDEA2=1,FFFFDEAF=1,FFFFDEC0=2,FFFFDEC4=2,FFFFDF21=1,FFFFDFA1=1,FFFFFC82=1,FFFFFC83=1

# CPORT_PARAMNORM: functions whose pointer parameters are normalised with A68PTR at
# entry -- the arcade's video writers that take a destination or source in an
# address register (tools/video_writers.lua census, 2026-10-04; see cport_prep.py).
# + FUN_0000f28a (2026-10-04): the demo's enemy installer takes slot 62/63 as a bare 68K
#   constant in A5 (0xF1C0); without A68PTR its stores went to SH-2 0xFFFFDF00.
# + FUN_0000135e (2026-10-04): the per-slot clear takes A0 = 0xFFC000 as a bare int.
# + FUN_0000469c, FUN_00004612 (2026-10-04): the high-score table's text writers (arcade
#   wp census frames 2104-2116: 156 + 6 writes) take the text pointer in A0/A2.
# + 2026-10-04 (rd102 literal census, cport_prep.py): callees that receive a bare 68K
#   address in an address register and dereference it -- FUN_00001192 (A0 = 0xFFF028),
#   switchD_00002ef0__caseD_f0 (A1 = 0xFFF004/6, the input edge records), FUN_000037d0
#   (A0 = 0xFFF010, the hi-score digits), FUN_00004276/FUN_00004330/FUN_00004196 (the
#   name-entry record, 0xFFFC74/0xFFFC82/0xFFFC3A -- SH-2 0xFFFFFC3A is on-chip RAM),
#   FUN_000058e4/FUN_00005e04 (A5 = 0xFFDE00/0xFFD900 object slots), FUN_0000f8c0/
#   FUN_00014b5e (A1 = ROM object templates 0x10112/0x15164..), FUN_000180c2 (A5 = ROM
#   0x186FA).
CPORT_PARAMNORM ?= FUN_00002622,FUN_00003972,textram_stride_write,FUN_00001732,tilemap_unpack_hi,tilemap_unpack_lo,FUN_0000258a,FUN_00003aa4,FUN_0000f28a,FUN_0000135e,FUN_0000469c,FUN_00004612,FUN_00010394,FUN_00001192,switchD_00002ef0__caseD_f0,FUN_000037d0,FUN_00004276,FUN_00004330,FUN_00004196,FUN_000058e4,FUN_00005e04,FUN_0000f8c0,FUN_00014b5e,FUN_000180c2
# CPORT_AFTER: hooks called right after a marked call site, MARKER=hook (the marker is the
# game's breadcrumb _DAT_fffffefc = jsr + 6; tools/cport_prep.py). 574 = the boot's
# set_level_palettes (FUN_00000500), where altbeastj adds its warning page: the hook
# returns at once unless the region byte is 1 (games/altbeast/glue.c port_region_boot).
CPORT_AFTER ?= 574=port_region_boot
CPORT_ENV = CPORT_CORPUS=docs/proof/$(GAME).c CPORT_HAND_DIR=games/$(GAME)/hand CPORT_CLAMP=$(CPORT_CLAMP) \
	    CPORT_STUBS='$(CPORT_STUB_WAIT),object_dispatcher->port_object_dispatch,zoom_scale_lookup->port_zoom_scale' \
	    CPORT_HANDLERS=$(CPORT_ROOTFILE) \
	    CPORT_OPT=$(CPORT_OPT_SET) \
	    CPORT_A6=$(CPORT_A6) \
	    CPORT_HI=$(CPORT_HI) \
	    CPORT_AREG=$(CPORT_AREG) \
	    CPORT_DREG=$(CPORT_DREG) \
	    CPORT_WATCHFN=$(CPORT_WATCHFN) \
	    CPORT_F154LOG=$(CPORT_F154LOG) \
	    CPORT_PROBE=$(CPORT_PROBE) CPORT_PROBE_AT='$(CPORT_PROBE_AT)' CPORT_PROBE_VARS='$(CPORT_PROBE_VARS)' \
	    CPORT_ANYNORM=$(CPORT_ANYNORM) \
	    CPORT_WIDTH=$(CPORT_WIDTH) \
	    CPORT_PARAMNORM=$(CPORT_PARAMNORM) \
	    CPORT_ARGLOG=$(CPORT_ARGLOG) \
	    CPORT_COUNT=$(CPORT_COUNT) \
	    CPORT_AFTER=$(CPORT_AFTER) \
	    CPORT_ROOTS=$(CPORT_ROOTS_SET)

# $(PORTSTAMP) IS A REAL PREREQUISITE HERE. The generated unit's CONTENT is
# determined by CPORT_ROOTS, CPORT_STUBS and CPORT_OPT -- none of which is a -D, so
# none of which reaches .build_flags. Without this stamp the rule saw its two file
# prereqs unchanged and REUSED a unit built from a different root set: that is the
# `.build_flags` trap (Makefile 3627) one level up, in a generated source rather
# than an object, and it also makes CPORT_ROOTS_BASE overridable on the command line
# (which is how the 2026-10-01 sixteen-root baseline is reproduced).
# THE STATIC WIDTH CENSUS the clamp reads. Derived from the program's own instruction
# encodings, so it only changes when the ROM or Ghidra's function boundaries do -- and
# it is a PREREQUISITE of the port unit, because a stale census would clamp to a width
# the program no longer uses and say nothing about it.
docs/audit/width_census.json: $(GAMEROMS)/prog68k.bin docs/audit/func_ranges.txt tools/cport_widths.py
	python3 tools/cport_widths.py --json $@

# WHICH REGISTERS EACH 68K FUNCTION WRITES (2026-10-25): cport_prep's extraout feed reads
# it to tell a register the producer passes through (fed from the value before the call)
# from one it computes and C never returns (left to the attributed call). Same inputs and
# the same standing as the width census above.
docs/audit/regwrites.json: $(GAMEROMS)/prog68k.bin docs/audit/func_ranges.txt tools/m68k_regwrites.py
	python3 tools/m68k_regwrites.py --json $@

# THE GENERATOR AND THE HAND FILES ARE INPUTS, 2026-10-11. They were not listed, so editing
# cport_prep.py or a hand/*.c did NOT rebuild port.o: the unit was regenerated only when
# something else in this list moved. That is how eleven hand-file signatures sat broken
# against protos.h since d696b228 without a single build failing -- cport_prep checks them,
# but only when it actually runs. Deleting build/port was the only way to find out.
CPORT_HAND := $(wildcard games/$(GAME)/hand/*.c games/$(GAME)/hand/*.h)
sh_src/portfull/port.o: $(CPORT_ROOTFILE) docs/proof/$(GAME).c $(FLAGSTAMP) $(PORTSTAMP) docs/audit/width_census.json docs/audit/regwrites.json tools/cport_prep.py tools/cport_build.sh $(CPORT_HAND)
	$(CPORT_ENV) CPORT_EXTRA_CFLAGS='$(PORT_TRACE_CF)' CPORT_DEFS='$(filter -D%,$(SHCCFLAGS))' tools/cport_build.sh sh_src/portfull
	python3 tools/cport_layout.py sh_src/portfull --rom $(GAMEROMS)/prog68k.bin \
	  --wram-base 0x0602CC00 --mmio-base 0x06019000 --rom-base $(PORTROM_BASE) \
	  --io-base 0x0602C700
	$(CPORT_ENV) python3 tools/cport_dispatch.py sh_src/portfull

# -finstrument-functions on the PORT UNIT ONLY (PORTTRACE=1): gcc calls
# __cyg_profile_func_enter on entry to every function, and sh_src/port_l2.c logs
# `this_fn` into a ring. That is the port's own call trace, to diff against the
# arcade's progress markers (0xFFFEF4/F6) which Ghidra dropped as dead stores.
# Costs a call per call, so it is a probe flag and never on by default.
ifdef PORTTRACE
# += NOT =, 2026-10-11. This line sits AFTER the PORTTILEDIRTY block's
# `PORT_TRACE_CF += -DPORT_TILEDIRTY`, so a plain `=` dropped that define from this
# variable whenever PORTTRACE was also set. It was harmless only by accident: the port
# unit gets -DPORT_TILEDIRTY again through CPORT_DEFS (the -D filter over SHCCFLAGS, the
# port.o rule below), so nothing ever broke and nobody noticed. Any future flag that adds
# to PORT_TRACE_CF and is NOT in SHCCFLAGS would have been silently dropped.
PORT_TRACE_CF += -finstrument-functions
endif

# `make ... PORTZOOM=N` = the L2 per-frame curve at FULL resolution for frames
# N..N+31 instead of downsampled over the whole run, and the cell collector fires
# on the first frame in that window that STEPS (>= 8 bytes worse than its
# predecessor) rather than on the last frame. The per-frame curve re-seeds from the
# arcade every frame, so a step is a defect the port owns with no inherited error.
ifdef PORTZOOM
SHCCFLAGS += -DL2M_ZOOM=$(PORTZOOM)u
endif

# `make ... PORTENDURE=N` = free-run N frames of game logic from the seed and LEAVE
# work RAM at the endpoint, instead of running the two curves. Costs no cart space
# (the host compares the dump against the arcade's capture at frame 9000+N), so N is
# not bounded by the delta chain -- 900 frames is 15 s, the repo's own probe unit.
# Read with tools/portl2_endure.py.
ifdef PORTENDURE
SHCCFLAGS += -DL2M_ENDURE=$(PORTENDURE)u
endif

# `make ... PORTSEED=N [PORTSKIP=S]` = seed work RAM to the arcade's frame N, run
# ONE frame body, leave it for a host-side diff against the capture at N+1. This is
# the per-frame gate with a COMPLETE wrong-cell list. PORTSKIP=S dispatches every
# active slot except S, so running the same seed with and without a slot and diffing
# the two cell lists gives that slot's exact contribution.
# Read with tools/portl2_seed.py.
ifdef PORTSEED
SHCCFLAGS += -DL2M_SEEDAT=$(PORTSEED)u
endif
ifdef PORTSKIP
SHCCFLAGS += -DL2M_SKIP=$(PORTSKIP)
endif
# `make ... PORTSEED=N PORTWATCH=0xFFFA80 PORTTRACE=1` = watch ONE 68K cell through
# that seeded frame and publish every function whose span changed it, innermost
# first. Read with tools/port_watch_read.py.
ifdef PORTWATCH
SHCCFLAGS += -DL2M_WATCH=$(PORTWATCH)u
endif
# `make ... PORTBODY=n` = run only the FIRST n calls of the arcade's measured frame
# body (FULL-PORT.md 32). The full fifteen regressed the seed frame 9 -> 97 wrong, so
# the sequence is adopted a call at a time against the gate.
ifdef PORTBODY
SHCCFLAGS += -DL2M_BODY_N=$(PORTBODY)
endif
# `make ... PORTREGS=1` = give each object handler the ARCADE's register file at the
# dispatcher's call site (tools/cport_dispregs.py), instead of A6 alone. A
# DIAGNOSTIC: it proves whether the missing registers are what stops objects being
# hidden, and bounds the payoff before deriving the register chain. Never a ship.
ifdef PORTREGS
SHCCFLAGS += -DPORT_DISPREGS
endif

sh_src/portfull/objdispatch.o: sh_src/portfull/port.o
	$(SHCC) $(SHCCFLAGS) -w -I sh_src/portfull -c sh_src/portfull/objdispatch.c -o $@

# defs.c DEFINES the pseudo-registers (_r_unaff_A6 and friends) that pseudoregs.h
# only declares. Leaving it out of the link is an undefined-reference storm, not a
# quiet miscompile -- but it is easy to forget, because nothing else references it.
sh_src/portfull/defs.o: sh_src/portfull/port.o
	$(SHCC) $(SHCCFLAGS) -w -I sh_src/portfull -c sh_src/portfull/defs.c -o $@

# the ARCADE's register file per slot, for the PORTREGS diagnostic
sh_src/portfull/dispregs.o: sh_src/portfull/port.o
	@# l1_vectors.jsonl is an arcade trace (never committed); without it the tracked
	@# dispregs.c stands, which is what the generator wrote from it.
	if [ -f docs/proof/l1_vectors.jsonl ]; then python3 tools/cport_dispregs.py sh_src/portfull; fi
	$(SHCC) $(SHCCFLAGS) -w -I sh_src/portfull -c sh_src/portfull/dispregs.c -o $@

sh_src/portfull/l2seed.o: sh_src/portfull/port.o
	python3 tools/cport_l2seed.py sh_src/portfull
	$(SHCC) $(SHCCFLAGS) -w -I sh_src/portfull -c sh_src/portfull/l2seed.c -o $@

# ---- THE FLAG STAMP, LAST IN THE FILE SO IT SEES EVERY -D ----------------------
# Must stay the final parse-time statement: anything that appends to MDCCFLAGS or
# SHCCFLAGS below this point is invisible to the stamp and therefore invisible to
# make. See the note at the FLAGSTAMP definition for what that cost.
ifndef FLAGSTAMP_SKIP
$(shell f='$(MDCCFLAGS) $(SHCCFLAGS)'; \
        [ "$$(cat $(FLAGSTAMP) 2>/dev/null)" = "$$f" ] || printf '%s' "$$f" > $(FLAGSTAMP))
$(shell f='$(CPORT_ROOTS_SET) | $(CPORT_STUB_WAIT) | $(CPORT_OPT_SET) | $(CPORT_CLAMP) | $(CPORT_A6) | $(CPORT_HOT) | $(CPORT_AFTER)'; \
        [ "$$(cat $(PORTSTAMP) 2>/dev/null)" = "$$f" ] || printf '%s' "$$f" > $(PORTSTAMP))
endif

# the 4 MB and 6 MB linker scripts (CARTMB, see SHLDFLAGS); kept at the end so they are never the default goal
sh_src/mars6.ld: sh_src/mars.ld
	sed -e 's/LENGTH = 0x00800000/LENGTH = 0x00600000/' \
	    -e 's/ASSERT(0x0244D000 + SIZEOF(\.portcode) <= 0x02800000/ASSERT(0x0244D000 + SIZEOF(.portcode) <= 0x02600000/' $< > $@
sh_src/mars4.ld: sh_src/mars.ld
	sed -e 's/LENGTH = 0x00800000/LENGTH = 0x00400000/' \
	    -e 's/\.sfxdata 0x022FE400 : AT(0x002FE400)/.sfxdata 0x02292000 : AT(0x00292000)/' \
	    -e 's/SIZEOF(\.sfxdata) <= 0x02400000, ".sfxdata overflows past the low 4 MB (the 68K-visible region)"/SIZEOF(.sfxdata) <= 0x022F8000, ".sfxdata overflows into .palscenes at 0x2F8000"/' \
	    -e 's/ASSERT(ADDR(\.sounddata) + SIZEOF(\.sounddata) <= 0x022F8000/ASSERT(ADDR(.sounddata) + SIZEOF(.sounddata) <= 0x02292000/' \
	    -e 's/\.portrom 0x02400000 : AT(0x00400000)/.portrom 0x02300000 : AT(0x00300000)/' \
	    -e 's/\.portdata 0x02440000 : AT(0x00440000)/.portdata 0x02340000 : AT(0x00340000)/' \
	    -e 's/\.portcode 0x0244D000 : AT(0x0044D000)/.portcode 0x0234D000 : AT(0x0034D000)/' \
	    -e 's/ASSERT(0x0244D000 + SIZEOF(\.portcode) <= 0x02800000/ASSERT(0x0234D000 + SIZEOF(.portcode) <= 0x02400000/' $< > $@
