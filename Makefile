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
# `make SPANPROBE=1` = LOOP 9: pickup-V histogram ([34..41]) + blit-span
# histogram ([42..49]) + late restores split by pickup half ([50]/[51]).
# The mean span already fits vblank; this asks what the TAIL looks like
# and whether a late pickup is what makes it. NEVER SHIP.
ifdef SPANPROBE
SHCCFLAGS += -DSPAN_PROBE
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
SHCCFLAGS += -DPAL_DELTA
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
# `make ... FBXPORT=1` = THE FB TRANSPORT (LOOP27 67). The r60 packet
# crosses through the 32X framebuffer instead of the DREQ FIFO: the 68K
# writes it at FM=0 before the post (1 scanline per 20 words on hardware,
# against the FIFO's 48), publishes a sequence word LAST, and the master
# copies it into SPR_LAND in its window body — same bytes, same layout,
# the whole harvest downstream unchanged. Not a probe: this is the
# candidate ship route.
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
# `make line PALFIRST=1` = BG palette DMA at the consume's top (2026-09-20)
ifdef PALFIRST
MDCCFLAGS += -DPAL_FIRST
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
ifdef FBXPORT
MDCCFLAGS += -DFB_XPORT
SHCCFLAGS += -DFB_XPORT
endif
# `make ... BOOTFLIPRATE=1 TAILCENSUS=1` = NOTES 49 / LOOP29 260: the 68K's
# IRQ4 tail split in lines (entry->A, batch A, batch B, pump, blast,
# blast->post, idle before IRQ4) as means per 64 vints; tag 7 = vints with
# no idle. The GAMEGATE thunk stamps the frame-done V (patch_game.py).
ifdef TAILCENSUS
export TAILCENSUS
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
# `CARTSSF=1` = the SSF2 cart mapper (2026-10-31, docs/design/SILICON.md 4e): the CARTMB=4
# layout plus a paged region above 4 MB (.pages, sh_src/pages_data.s) reached through a
# 512 KB slot the 68K pages on the SH-2's request (md_src/md_bank.c, COMM10 0x5Bxx). The
# header says "SEGA SSF" (sh_src/mars_start.s). For the two-channel music stream.
ifeq ($(CARTSSF),1)
LDSCRIPT   = sh_src/mars4s.ld
CART_HI    = 16\#1000000
PORTROM_BASE = 0x022C0000          # tools/gen_ld_ssf.py: the low 4 MB repacked, slot 7 (0x380000) empty.
                                   # THE PORT READS ITS 68K CONSTANTS HERE (layout.ld --rom-base): with the
                                   # CARTMB=4 value the master read .portcode bytes as data and took an
                                   # exception at the level start (ares watch-sdram, 2026-10-31)
MDSPR_ARGS = --winoff $(shell python3 tools/gen_ld_ssf.py --winoff)   # .mdsprart's bank-2 window offset on that layout: ONE source
SHCCFLAGS += -DCART_SSF
MDCCFLAGS += -DCART_SSF
SHASFLAGS += --defsym CART_SSF=1
MDASFLAGS += -Wa,--defsym,CART_SSF=1
ifdef SSFPLAINHDR
SHASFLAGS += --defsym CART_SSF_PLAINHDR=1   # probe: the SSF layout under the plain 32X header (standard board)
endif
endif

MDEXTRA =
SHEXTRA =

ifdef R60WORKSETCODE
ifndef R60NEW
$(error R60WORKSETCODE requires R60NEW=1)
endif
endif

MDOBJS  = $(patsubst %.s,%.o,$(wildcard md_src/*.s))
MDOBJS += $(patsubst %.c,%.o,$(wildcard md_src/*.c))
SHOBJS  = $(patsubst %.s,%.o,$(wildcard sh_src/*.s))
SHOBJS += $(patsubst %.c,%.o,$(wildcard sh_src/*.c))

# The baked sprite blob is ~660KB of cart ROM and is dead weight until
# the compose fast path reads it, so it is NOT in the default link —
# the wildcard above would otherwise pull it into every build,
# including the shipping rom.
SHOBJS := $(filter-out sh_src/sprbake_data.o sh_src/md_sprart_data.o sh_src/artbake_data.o,$(SHOBJS))
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
# `R60SLAVEWIN=1` = the slave owns the frame: draws a snapshot, then does the window
# (packet, CRAM, flip request, FM down) itself; the master only ticks and snapshots.
ifdef R60SLAVEWIN
SHCCFLAGS += -DR60_SLAVEWIN
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
ifdef R60SPLITFAST
SHCCFLAGS += -DR60_SPLITFAST   # r60_split_row's histogram as a prefix sum (same split; ~4.8 master lines a vint less)
endif
ifdef R60ROWFAST
SHCCFLAGS += -DR60_ROWFAST_OPT   # the hoisted per-sprite row loop (2026-10-23); see R60_ROWFAST in r60_main.c
endif
ifdef R60RECTCLR
SHCCFLAGS += -DR60_RECTCLR
endif
ifdef R60ZOOM
SHCCFLAGS += -DR60_ZOOM
endif
# `R60KEEPSTALE=1` = a cell whose new art did not ship this window keeps LAST window's
# cell instead of tile 0: the title logo recolour rolls white->red with no black holes,
# scene rewrites show the old scene's tiles until the new art lands. 2026-10-16.
# `R60TXTPOOL=1` = the text colours live in the sprite sets' unused pens 0 and 15 (31 CRAM
# entries, allocated per window to the (group, pen) pairs the visible text uses) instead
# of sprite sets 12-14, which the arcade's sprites DO use (MAME census: all 16 sets, 12-14
# from frame 1733; the white wolf drew in text colours). 2026-10-16.
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
OPM_LANE_RATE ?= 16002.3
OPM_LANE_ROWSCALE ?= 1.00028
# (its rules sit with the other sound-bank rules after `all`: a rule up here becomes the
# default goal and the inner build stops after one object -- it did, 2026-10-31)
# `R60CUTPF=1` = preload the demo's transformation-cut art (pages 10/11) before the switch
# (sh_src/r60/r60_planes.c r60_pf_step). 2026-10-19.
ifdef R60CUTPF
SHCCFLAGS += -DR60_CUTPF $(if $(R60PFFORM),-DR60_PF_FORM=$(R60PFFORM)u)   # R60PFFORM=N: in play, preload the cut pages from N orbs (default 2; a probe sets 1)
endif
# `R60PALMAP=1` = sprite palettes 0-63 mapped onto the 16 32X colour banks per pass (sw262):
# without it palette n drew with the colours of (n & 15) (hit flashes, power-up forms).
ifdef R60PALMAP
SHCCFLAGS += -DR60_PALMAP
endif
# `R60PTONE=1` (2026-11-01) = the sprite plotter's pen table keeps ONE flip (8 KB, not 16): the
# flipped entry was the byte-swapped twin, so the flipped read swaps (swap.b) instead.
# Gate: attract screenshots byte-identical to the same build without the flag (ares). The
# 8 KB is for the synth lane's hot tables (sh_src/r60/r60_main.c R60_PTONE).
ifdef R60PTONE
SHCCFLAGS += -DR60_PTONE
endif
# `R60BAKEART=1` (2026-11-04, needs CARTSSF=1) = the baked art: tools/bake_art.py packs the
# sprite frames the plotter spends the most ROM words on (ranked over the whole-game census
# dumps in build/gate/neff/art, Sega-derived, gitignored) into the five SH-2-readable windows
# of the SSF layout (tools/gen_ld_ssf.py pins them; window 0 is the tail of the music page).
# Four windows, ~255 KB: the gap above .text is the .data/.ramtext load image, not free.
# The index at the head of window 0 maps (bank, addr, pitch, flip) -> SH-2 address. The slave's
# copy path reads it under R60_BAKEART; this flag alone only LINKS the blob (step 3 of the
# bake order, STATE.md). `R60BAKEARTCHECK=1` (with it) replays every copied row through the
# plotter into RAM and counts differences (r60_bk_bad rows, r60_bk_badx row ends, r60_bk_badn
# pixel counts; the copy then runs from cart): zero over the Neff tape is the identity gate.
ifdef R60BAKEARTCHECK
R60BAKEART = 1
endif
# `R60BKBENCH=1` (with R60BAKEART) = PROBE: the copy loop's memory cost on the running machine,
# r60_bk_bench[0..3] = FRT ticks for 512 groups SDRAM->SDRAM, cart->SDRAM, SDRAM->FB, cart->FB,
# [5] cart->SDRAM again. With `R60NOBARS=` the values and the live copy/plotter costs are bars in
# rows 196..223 for the rig (r60_main.c r60_bkb_bars; read with tools/rig_bkbench.py).
# `R60BKBENCH=2` = the PUNCH's memory (2026-11-05): [0] SDRAM->SDRAM, [1] FB uncached -> SDRAM (the
# read), [2] SDRAM->FB, [3] FB uncached read-modify-write in place, [5] the same over uncached SDRAM;
# read with `tools/rig_bkbench.py --punch`.
ifdef R60BKBENCH
SHCCFLAGS += -DR60_BKBENCH=$(R60BKBENCH)
endif
ifdef R60BAKEART
ifndef CARTSSF
$(error R60BAKEART needs CARTSSF=1: the windows are the SSF layout's)
endif
SHCCFLAGS += -DR60_BAKEART $(if $(R60BAKEARTCHECK),-DR60_BAKEARTCHECK)
SHASFLAGS += --defsym R60_BAKEART=1
SHOBJS += sh_src/artbake_data.o
# the corpus lives in the TOP tree's build/; `make port` hands the shadow tree its absolute path
ARTBAKE_CORPUS ?= $(abspath build/gate/neff/art)
ARTBAKE_RUNS = r0=$(ARTBAKE_CORPUS)/round0 r1=$(ARTBAKE_CORPUS)/round1 r2=$(ARTBAKE_CORPUS)/round2 \
	r3=$(ARTBAKE_CORPUS)/round3 r4=$(ARTBAKE_CORPUS)/round4 tape=$(ARTBAKE_CORPUS)/tape
# window 0 = page 8 (SH-2 0x02380000) from the 16-aligned end of sound.bin to 64 bytes short of
# the page's end: the rom's last 48 bytes are the build stamp (tools/build_id.py, 0xFF pad)
ARTBAKE_WIN0 = $(shell python3 -c "import os;s=(os.path.getsize('sh_src/sound.bin')+15)&~15;print('0x%08X:0x%X'%(0x02380000+s,0x80000-s-64))")
# window 1 (the slot-6 tail after .portcode) is empty on a PORTTAPEPAD build: the in-port input
# tape lives in .portcode and takes it past 0x375000 (measurement roms only; coverage -44 KB)
# `ARTBAKENOWIN1=1` = the same empty window 1 on a normal build (2026-11-05: p15bpt4's zombies,
# Aggar and the heads drew broken on the FPGA while the tape probe with window 1 empty drew them
# right; ares draws both right. The A/B rom p15bpt5.)
ARTBAKE_WIN1 = $(if $(or $(PORTTAPEPAD),$(ARTBAKENOWIN1)),0x02375000:0,0x02375000:0xB000)
ifneq ($(or $(PORTTAPEPAD),$(ARTBAKENOWIN1)),)
SHASFLAGS += --defsym ARTBAKE_NOWIN1=1   # no .artbake1 input section: an empty pinned section inside .portcode wiped the image
endif
ARTBAKE_WIN2 = 0x022B1000:0xF000
ARTBAKE_CHUNKS = $(ARTBAKE_WIN0),$(ARTBAKE_WIN1),$(ARTBAKE_WIN2),0x02298000:0x6100
# `LAUNCHER=1 LAUNCHER_PAYLOAD=<file>` = THE LAUNCHER'S PAGE (2026-11-13): one 512 KB page
# linked after the last game page (sh_src/pages_data.s), holding the payload wrapped by
# tools/launcher_page.py. Both SH-2s sum it through slot 7 at boot and the game's page comes
# back before the first tick (sh_src/r60/r60_launcher.c). The payload is Sega-derived
# (the intro's art), so it is an untracked input named on the command line.
ifdef LAUNCHER
$(foreach f,CARTSSF R60SOUND,$(if $($(f)),,$(error LAUNCHER requires $(f)=1)))
$(if $(LAUNCHER_PAYLOAD),,$(error LAUNCHER=1 needs LAUNCHER_PAYLOAD=<file>))
# where the launcher starts in its page, after the music data every slot-7 page begins with;
# introlab/sh/port.ld links the program for this address (tools/launcher_page.py checks it)
# 0x3A620 (2026-11-14, was 0x3B000): sound.bin is 239,027 B (0x3A5B3), so this leaves 109 B for it
# to grow; the launcher needed the other 2.5 KB. pages_data.s stops the build if the music reaches it.
LAUNCHER_BASE ?= 0x3A620
SHCCFLAGS += -DLAUNCHER -DLAUNCHER_BASE=$(LAUNCHER_BASE)
MDCCFLAGS += -DLAUNCHER -DLAUNCHER_BASE=$(LAUNCHER_BASE)
SHASFLAGS += --defsym LAUNCHER=1 --defsym LAUNCHER_BASE=$(LAUNCHER_BASE)
endif
# `R60BAKEPAGES=1` (with R60BAKEART) = PER-LEVEL PAGES (2026-11-04, "bake per level"): pages 8..12
# each carry the music blob unchanged plus one level's own baked tail (page 8 + level); windows
# 1-3 hold the frames two or more levels share. A 6.5 MB rom (PAGEPROBE=13 proved 7 MB on the
# FPGA). The SWITCH (r60_main.c R60_BAKEPAGES): the master asks the 68K for page 8 + level over
# COMM10 between slave passes and withholds snapshots until the ack; the slave purges its cache
# at the next pass start. `R60BAKEPAGES=1 R60BAKEPAGESNOSW=1` = the pages without the switch
# (page 8 everywhere, the 2026-11-04 19:25 gate build p15bp).
ifdef R60BAKEPAGES
SHCCFLAGS += $(if $(R60BAKEPAGESNOSW),,-DR60_BAKEPAGES)
ARTBAKE_PAGESPEC = r0+tape,r1,r2,r3,r4
ARTBAKE_PAGEBINS = sh_src/artbake_p9.bin sh_src/artbake_p10.bin sh_src/artbake_p11.bin sh_src/artbake_p12.bin
SHASFLAGS += --defsym ARTBAKE_PAGES=5
endif
# the recipes sit below `all:` with the sprbake rules -- a rule up here becomes the DEFAULT GOAL
endif
# `R60SPANCAP=n` (2026-11-03) = the slave's row-span table, entries per bank (default 768, 4 B each,
# .bss). The level-1 fight holds ~830 spans (seven fence pillars x 64 rows + three zombies), and an
# overflow clears the WHOLE canvas: 146 lines a pass against the row clear's ~35, on 390 of 870
# first-wave passes of the line (build/gate/psg6grs2). Needs the .bss room: R60PTONE=1 gives 8 KB.
ifdef R60SPANCAP
SHCCFLAGS += -DR60_SSPAN_CAP=$(R60SPANCAP)u
endif
# `R60RIGSPLIT=1` (with `R60NOBARS=`) = PROBE: both cores' phase split as bars in rows 196..223,
# one row a value, 1 px a scanline (2026-10-24, the rig phase split; map in r60_main.c R60_RIGSPLIT).
# `R60RIGSPLIT=2` = the PLOT and PUNCH rows at 2 lines a px (the 1 px form caps at 319 lines, which
# the level-1 boss reaches on the FPGA, 2026-11-04); read with `tools/rig_split.py --plot2`.
# `R60RIGSPLIT=4`: mode3 scales, peak plot at row219 instead of IRQ time;
# rows188..195 encode V-ints, native game clock, tick calls and observed applied flips.
# Reserves pen253; counters are visible only when the renderer presents an image.
ifdef R60RIGSPLIT
SHCCFLAGS += -DR60_RIGSPLIT=$(R60RIGSPLIT)
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
# `R60PKTODDHI=1` (with R60STAGEDIRECT) = the odd-parity direct hold packet moves from FB 0x11800
# (68K 0x851800), which overlapped canvas rows 222-223 and left its words there as pixels, to
# 0x11E00 (68K 0x851E00), past the canvas and its spill (2026-10-24). Both CPUs.
ifdef R60PKTODDHI
SHCCFLAGS += -DR60_PKTODDHI
MDCCFLAGS += -DR60_PKTODDHI
endif
# `R60PUNCHTHRU=1` (with R60PUNCH2) = THE NO-READ PUNCH (2026-11-05): the FG punch writes a reserved
# through pen (CRAM bit 15, entry 0's colour; 255 on a NOBARS build, 254 under bars) through the
# overwrite image instead of zeroing by read-modify-write over the uncached alias. On the FPGA every
# uncached access is ~16-18 cycles (R60BKBENCH=2), and a solid cell-row took five of them.
ifdef R60PUNCHTHRU
SHCCFLAGS += -DR60_PUNCHTHRU
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
# Prepared palette-index sprite packets, in the 1,024-word mask arena freed by
# using 512 priority-cache entries. R60SPRPREP2 divides that same arena into two
# residents. R60SPRPREP2S is the small-slot capacity in words (default 256);
# R60SPRPREP2H is its maximum routed pose height (default 24). The large slot
# receives every remaining word. A game must census and measure its own policy.
ifneq ($(strip $(R60SPRRAW)$(R60SPRPREP)),)
ifndef R60BAKEART
$(error R60SPRRAW/R60SPRPREP require R60BAKEART)
endif
endif
ifdef R60SPRPREP
SHCCFLAGS += -DR60_SPRPREP -DR60_PRIOCACHE512
endif
ifneq ($(strip $(R60SPRRAW)$(R60SPRPREP)),)
SHCCFLAGS += -DR60_SPRRAW
SHASFLAGS += --defsym R60_SPRRAW=1
endif
# R60RAWTRAIL=1: the ready-byte row loop keeps its trailing-carry predicate
# in the now-dead palette-add register instead of saving r8 on the stack.
# Default-off until the exact row oracle and controlled route both pass.
ifdef R60RAWTRAIL
ifneq ($(strip $(R60RAWTRAIL)),1)
$(error R60RAWTRAIL must be 1)
endif
ifneq ($(strip $(R60SPRRAW)$(R60SPRPREP)),)
SHASFLAGS += --defsym R60_RAW_TRAIL_R7=1
else
$(error R60RAWTRAIL requires R60SPRRAW or R60SPRPREP)
endif
endif
ifneq ($(strip $(R60SPRPREP2H)$(R60SPRPREP2S)),)
ifndef R60SPRPREP2
$(error R60SPRPREP2H/R60SPRPREP2S require R60SPRPREP2=1)
endif
endif
ifdef R60SPRPREP2
ifneq ($(strip $(R60SPRPREP2)),1)
$(error R60SPRPREP2 must be 1)
endif
ifndef R60SPRPREP
$(error R60SPRPREP2 requires R60SPRPREP=1)
endif
SHCCFLAGS += -DR60_SPRPREP2=1
ifdef R60SPRPREP2H
SHCCFLAGS += -DR60_SP_SMALL_HEIGHT=$(R60SPRPREP2H)u
endif
ifdef R60SPRPREP2S
SHCCFLAGS += -DR60_SP_SMALL_WORDS=$(R60SPRPREP2S)u
endif
endif
ifdef R60PRIOCACHE512
SHCCFLAGS += -DR60_PRIOCACHE512
endif
# `R60ZOOMFAST=1` = zoomed sprite rows by source word through the PLOT5 pen table (the
# per-pixel zoom loop cost ~120 instructions a word; Neff's entrance smoke, 19 zoomed
# sprites, ran six vints a picture, 2026-10-23). `R60ZOOMCHECK=1` replays every zoomed
# row both ways into RAM and counts differing bytes (r60_zoom_bad / r60_zoom_rows).
ifdef R60ZOOMFAST
SHCCFLAGS += -DR60_ZOOMFAST
endif
# `R60ZOOMKEEP=1` = optional immutable horizontal keep patterns. Copy16 bytes
# once per scaled sprite into the existing caller buffer; retain old row
# kernels and reconstruct the two scalar/check masks.512 ROM bytes, no BSS.
# Independent of R60ZOOMWORDS; defaults off. Setup savings require measurement.
ifdef R60ZOOMKEEP
ifndef R60ZOOMFAST
$(error R60ZOOMKEEP requires R60ZOOMFAST=1)
endif
SHCCFLAGS += -DR60_ZOOMKEEP
endif
# R60BOOTBANKS=1: initialize complete framebuffer banks while MODE remains off.
# R60-only cold path; default off preserves the accepted initialization.
ifdef R60BOOTBANKS
ifneq ($(strip $(R60BOOTBANKS)),1)
$(error R60BOOTBANKS must be 1)
endif
SHCCFLAGS += -DR60_BOOTBANKS=1
endif
# A separate immutable sprite working-set page. The normal global windows and
# level pages keep their original budgets; inputs are explicit baked data.
ifdef R60WORKSET
ifneq ($(strip $(R60WORKSET)),1)
$(error R60WORKSET must be 1)
endif
$(foreach f,CARTSSF R60BAKEART R60BAKEPAGES R60SPRPREP R60ZOOMFAST R60ROWFAST R60PLOT5 R60PALMAP R60SLAVEWIN R60LIVEPAD PORTBOOT,$(if $($(f)),,$(error R60WORKSET requires $(f)=1)))
ifneq ($(strip $(R60BAKEPAGESNOSW)$(R60BAKEARTCHECK)$(R60MBAND)$(R60PLOT5CHECK)$(R60ZOOMCHECK)$(R60PUNCH2CHECK)$(R60MASKBLOCK)$(R60ARTCACHE)$(R60PRIOMASK)$(R60PRIOROW)),)
$(error R60WORKSET conflicts with alternate atlas/worker/page/check experiments)
endif
ifeq ($(strip $(R60WORKSETATLAS)),)
$(error R60WORKSET requires R60WORKSETATLAS=<raw ZB1 input>)
endif
ifeq ($(strip $(R60WORKSETPOSES)),)
$(error R60WORKSET requires R60WORKSETPOSES=<unscaled pose manifest>)
endif
R60_WORKSET_POSES_EFFECTIVE ?= $(R60WORKSETPOSES)
# Title-specific native emitter; another game supplies its own guard binding.
ifeq ($(GAME),altbeast)
CPORT_ENTRY_GUARDS ?= FUN_00002e50:R60_WORKSET:port_sound_mcu_ready
endif
SHCCFLAGS += -DR60_WORKSET -Ish_src/r60 -Igames/$(GAME)
SHASFLAGS += --defsym R60_WORKSET=1 --defsym R60_RAW_ZOOM_EXTENTS=1
endif
# R60ZOOMRUN=1: one prevalidated longest opaque interval per scaled packet row.
# The row copier branches only at segment boundaries; the accepted ZB1 packet
# and normal line remain byte-for-byte unchanged when this experiment is off.
ifdef R60ZOOMRUN
ifneq ($(strip $(R60ZOOMRUN)),1)
$(error R60ZOOMRUN must be 1)
endif
ifndef R60WORKSET
$(error R60ZOOMRUN requires R60WORKSET=1)
endif
ifdef R60WORKSETCACHE
$(error R60ZOOMRUN indexed sidecar does not yet support R60WORKSETCACHE)
endif
SHCCFLAGS += -DR60_ZOOM_RUN=1
SHASFLAGS += --defsym R60_ZOOM_RUN=1
R60_WORKSET_RUN_ARG = --opaque-runs 4
endif
# Admission-only owner-macro experiment.  The immutable MC1 file is produced
# by tools/pack_sprite_macros.py and appended after TP1; no live draw is enabled
# by this flag.  Keeping the file explicit prevents a normal build from
# depending on an emulator capture directory.
ifdef R60MACROPROBE
ifneq ($(strip $(R60MACROPROBE)),1)
$(error R60MACROPROBE must be 1)
endif
ifndef R60WORKSET
$(error R60MACROPROBE requires R60WORKSET=1)
endif
ifndef R60ZOOMRUN
$(error R60MACROPROBE requires R60ZOOMRUN=1)
endif
# R60MACROPACKETS is optional: without it the page carries no macro packets and every
# sprite takes the ordinary draw (what a source build without the capture gets).
ifdef R60MACROPACKETS
R60_MACRO_DEPS = $(R60MACROPACKETS)
R60_MACRO_BAKE_ARG = --macro-packets $(R60MACROPACKETS)
endif
SHCCFLAGS += -DR60_SPRITE_MACRO_PROBE=1
endif
# Fixture-only live owner-macro experiment.  Keep the admission probe enabled
# beside it until framebuffer/bookkeeping differentials have passed; this is
# intentionally not part of the accepted renderer line.
ifdef R60MACROLIVE
ifneq ($(strip $(R60MACROLIVE)),1)
$(error R60MACROLIVE must be 1)
endif
ifndef R60MACROPROBE
$(error R60MACROLIVE currently requires R60MACROPROBE=1)
endif
ifdef R60MACROLIVEDRAW
ifneq ($(strip $(R60MACROLIVEDRAW)),1)
$(error R60MACROLIVEDRAW must be 1)
endif
endif
# Both the scalar control and live diagnostic compile and call identical code.
# Only this volatile data initializer differs, keeping code/BSS layout stable
# enough for a meaningful frame-for-frame differential.
SHCCFLAGS += -DR60_SPRITE_MACRO_LIVE=1 \
	-DR60_SPRITE_MACRO_LIVE_INITIAL=$(if $(R60MACROLIVEDRAW),1,2)
endif
# R60BKHOIST=1: keep the baked-row palette transform constants in high
# registers for the complete sprite instead of rebuilding and saving them for
# every row.  This is a default-off scalar-kernel experiment; pixels, spans,
# painter order and framebuffer stores are unchanged.
ifdef R60BKHOIST
ifneq ($(strip $(R60BKHOIST)),1)
$(error R60BKHOIST must be 1)
endif
ifdef R60ZOOMRUN
$(error R60BKHOIST and R60ZOOMRUN are separate row-kernel experiments)
endif
SHASFLAGS += --defsym R60_BK_HOIST=1
endif
# Optional non-LTO compositor closure, with no render-loop caller yet.
# "fixed" uses the bounded resident ROM-load gap;8192 tests a page-7 lease.
# Absence keeps both the object list and full-page assembly path unchanged.
ifdef R60WORKSETCODE
ifndef R60WORKSET
$(error R60WORKSETCODE requires R60WORKSET=1)
endif
ifeq ($(filter $(R60WORKSETCODE),4096 8192 fixed),)
$(error R60WORKSETCODE must be 4096, 8192 or fixed)
endif
ifneq ($(words $(R60WORKSETCODE)),1)
$(error R60WORKSETCODE must be 8192 or fixed)
endif
ifneq ($(filter $(R60WORKSETCODE),4096 8192),)
SHCCFLAGS += -DR60_WORKSET_CODE=$(R60WORKSETCODE)
SHASFLAGS += --defsym R60_WORKSET_CODE=$(R60WORKSETCODE)
R60_CODE_BAKE_ARGS = --code-bytes $(R60WORKSETCODE)
else
SHCCFLAGS += -DR60_WORKSET_CODE_FIXED=1
endif
SHOBJS += sh_src/r60/r60_sprite_blocks_code.o
R60_CODE_LD_ARGS = --workset-code=$(R60WORKSETCODE)
endif
# R60WORKSETCLIP=1: exact right-edge scaled packet copy, independently gated.
ifdef R60WORKSETCLIP
ifndef R60WORKSET
$(error R60WORKSETCLIP requires R60WORKSET)
endif
ifneq ($(strip $(R60WORKSETCLIP)),1)
$(error R60WORKSETCLIP must be 1)
endif
SHCCFLAGS += -DR60_WORKSET_CLIP=1
endif
# R60SNAPLEASE=1: the slave claims the LATEST published snapshot (GO lease) instead of
# a stale one, and the master may republish while the slave is mid-draw of that same
# sequence. Default off; part of wslease01, the 2026-11-08 line (Mike's play pass).
ifdef R60SNAPLEASE
ifneq ($(strip $(R60SNAPLEASE)),1)
$(error R60SNAPLEASE must be 1)
endif
ifndef R60WORKSET
$(error R60SNAPLEASE requires R60WORKSET=1)
endif
SHCCFLAGS += -DR60_SNAPSHOT_LEASE=1
endif
# `R60RAMTEXTPEEK=1` = PROBE, NEVER RUN: links with the .ramtext cap raised into the art
# lists' slot so the .lst can be read for sizes when the slot overflows.
ifdef R60RAMTEXTPEEK
SHLDFLAGS += -Wl,--defsym,__ramtext_cap_override=0x0603A000
endif
# `R60EMPTYPKT=1` = the master flags a packet with nothing in it (bit 12); the 68K takes it
# without the walk (an empty A consume cost 8 68K lines of the span). 2026-10-17.
ifdef R60EMPTYPKT
SHCCFLAGS += -DR60_EMPTYPKT
MDCCFLAGS += -DR60_EMPTYPKT
endif
# `R60ROWDIFF=1`: build row deltas without copying the old 64-cell shadow row.
# The walk writes every scratch cell in its emitted span; default off.
ifdef R60ROWDIFF
SHCCFLAGS += -DR60_ROWDIFF
endif
# `R60PLANROWS=1`: resolve each contiguous physical source-row segment once;
# preserve live per-word reads, page boundaries and the existing residency planner.
ifdef R60PLANROWS
SHCCFLAGS += -DR60_PLANROWS
endif
# `R60TILEBATCH=1`: complete FG/BG inline-art backend, grouped pattern uploads,
# persistent blank pattern, and menu/reset invalidation. Requires R60SPANPROGRESS.
ifdef R60TILEBATCH
ifndef R60SPANPROGRESS
$(error R60TILEBATCH requires R60SPANPROGRESS)
endif
SHCCFLAGS += -DR60_TILEBATCH
MDCCFLAGS += -DR60_TILEBATCH
endif
# Experimental: completed packet work renews the slave's existing stall budget.
# COMM4 is reserved for this producer after the R60 startup handshake.
ifdef R60SPANPROGRESS
ifndef R60SLAVEWIN
$(error R60SPANPROGRESS requires R60SLAVEWIN)
endif
SHCCFLAGS += -DR60_SPANPROGRESS
MDCCFLAGS += -DR60_SPANPROGRESS
endif
ifdef R60TXTPOOL
SHCCFLAGS += -DR60_TXTPOOL
endif
# `R60TXTPOOL15=1` (2026-11-04, the baked-art renderer's bank fold) = the text pool is the 16
# pen-15 entries only; pen 0 of EVERY sprite bank is the through pen (CRAM bit 15), so a baked
# row copied with bank<<4 in every byte shows the MD planes where its art is transparent.
# Text colours past 16 borrow the nearest seated colour: r60_txt_borrow[0] sums the borrowed
# (pal, pen) pairs over windows, [1] is the worst window. Implies R60TXTPOOL.
ifdef R60TXTPOOL15
SHCCFLAGS += -DR60_TXTPOOL -DR60_TXTPOOL15
endif
# `R60TXTPOOLHYB=1` (2026-11-04) = the hybrid text pool: pen 15 of every bank always, plus pen 0
# of the banks no sprite drew in for three passes (the slave's R60_BANKBUSY); a bank whose pen 0
# seats text is handed a new palette last (R60_POOL0MASK). Pen 0 of every other bank is the
# through pen. Measured on p15i: seats every pair pool-15 borrowed (0 windows short, attract +
# Neff tape). Supersedes R60TXTPOOL15.
ifdef R60TXTPOOLHYB
SHCCFLAGS += -DR60_TXTPOOL -DR60_TXTPOOL15 -DR60_TXTPOOLHYB
endif
ifdef R60KEEPSTALE
SHCCFLAGS += -DR60_KEEPSTALE
endif
ifdef R60CRAMDELTA
SHCCFLAGS += -DR60_CRAMDELTA
endif
# `R60PLANES=1` = the MD background plane: the slot allocator carried from native1
# (m_main.c:19180-19280) with a new walk, two packets a frame (type 0 art, type 1 name
# table), seven plane rows each so a 28-row screen refreshes in four frames.
ifdef R60PLANES
SHCCFLAGS += -DR60_PLANES
endif
# `GOVERNOR=n` = the REBUILD pacing divider (section 2): 2 for bring-up (clean 30 Hz),
# 1 is the ship state. A divider on the same identical frame, not a phase machine.
ifdef GOVERNOR
SHCCFLAGS += -DR60_GOVERNOR=$(GOVERNOR)
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
ifdef PORTFULL
SHCCFLAGS += -DPORT_FULL
SHOBJS += sh_src/portfull/port.o sh_src/portfull/objdispatch.o \
          sh_src/portfull/defs.o sh_src/portfull/l2seed.o sh_src/portfull/dispregs.o \
          sh_src/region_data.o
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
SHOBJS := $(filter-out sh_src/port_l2.o sh_src/port_rom_data.o,$(SHOBJS))
endif

# Sound-blob generation also runs from the outer make, before the R60NEW port
# submake exists, so this policy intentionally lives outside that conditional.
# The separate SH-2 SFX lane owns effects in the game build.  md_sound.c's
# single Z80 lane only counts SND_YMSFX commands; starting those raw streams
# would cut the music.  Omit their unreachable bodies from each repeated
# mapper-page music blob while retaining all music streams and table indices.
ifdef R60SOUNDCOMPACT
ifneq ($(strip $(R60SOUNDCOMPACT)),1)
$(error R60SOUNDCOMPACT must be 1)
endif
ifndef R60SOUND
$(error R60SOUNDCOMPACT requires R60SOUND=1)
endif
SND_BLOB_ARG := --omit-unplayed-sfx
SND_BLOB_STAMP := compact
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
PCSAMPN ?= 55
ifdef PCSAMP
MDASFLAGS += -Wa,--defsym,PC_SAMP=1 -Wa,--defsym,PC_SAMP_N=$(PCSAMPN)
MDCCFLAGS += -DPC_SAMP -DPC_SAMP_N=$(PCSAMPN)
endif
ifdef PRHOLD
SHCCFLAGS += -DPR_HOLD_TICKS=$(PRHOLD)
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
# `make ... MISSKEEP=1` = LOOP-DECOMPILE 68. PROBE: nops the clr.w at
# 0x930 so the game stops wiping its own missed-frame counter at
# 0xFFF144. A zero reading on the shipping build is not evidence of zero
# misses; this makes the counter mean what it says.
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
# `make ... DIRTYROW=1` = LOOP 18 job 2: let the blit skip a row WITHOUT
# READING IT. Four probes said the blit is throughput-bound and partial
# removal returns sub-linearly (57% of stores gone kept 86% of the cost),
# so only whole rows pay. Under MD_BG sbuf is explicitly zeroed every row
# every cycle, which is why a "was it written" flag is useless and the
# fact tracked here is "is this row all zeros" — asserted only by the
# clear, revoked by every draw.
ifdef DIRTYROW
SHCCFLAGS += -DDIRTY_ROW
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
# `make ... MDALLOCWHY=1` = LOOP29 153: the MD residency allocator's own
# counters, 16 longs at 0x3A680 (see MDALLOC in m_main.c). Entry 152
# could not tell "the cell never claimed a slot" from "it claimed and
# something wiped it" because DIAG[39]/[50]/[53] are each written by
# three subsystems including the DREQ landing path. Read with
# tools/md_alloc_why.py. PROBE ONLY (collides with SPRLINE).
ifdef MDALLOCWHY
SHCCFLAGS += -DMD_ALLOC_WHY
endif
# `make ... NBUILD1=1` = one MD packet build per gap instead of two.
# Brake for NTSKIP's transport flood (LOOP29 170).
ifdef NBUILD1
SHCCFLAGS += -DNBUILD1
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
# Results in port_test_out[]; the symbol address comes from rom/s16.elf.
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
# The generated file is not kept in git (4,000 lines, all inside #ifdef PORT_TEST).
ifeq ($(wildcard sh_src/port_selftest.c),)
$(error PORTTEST needs sh_src/port_selftest.c: generate it with tools/gen_selftest.py)
endif
SHCCFLAGS += -DPORT_TEST -fno-strict-aliasing
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
# ROWGEN=1 = write-tracked row skip (2026-08-26): rows provably
# unchanged (no sprite span, no scroll/text/page/cut delta) skip their
# clear AND their blit. ROWSTALE measured 96% cycle-stable rows; this
# takes the write-knowable subset at zero read cost. Block at 0x39750
# (collides with DIRTYROWVERIFY's probe capture — probe-only overlap).
ifdef ROWGEN
SHCCFLAGS += -DROW_GEN
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
# BGBOTTOM=1 = 2026-09-21: the BG bottom-band backstop (2026-08-25) blanks
# a BG cell in view rows 24-27 only where the FG tile over it is fully
# OPAQUE; level 2's foreground has holes there and the arcade shows the
# floor through them (black cells around the rocks).
ifdef BGBOTTOM
SHCCFLAGS += -DBG_BOTTOM_HOLES
endif
# MDSREMARK=1 = 2026-09-21: a scene install re-ships (marks dirty) the
# slots of sets whose pen map changed instead of wiping their tags; cells
# naming a wiped slot showed the next claimant's art until re-walked (the
# eye scene's stale patches).
ifdef MDSREMARK
SHCCFLAGS += -DMDS_REMARK
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
export EARLYREC
endif
# ERECIRQ4=1 (with EARLYREC=1) = the push at the game's IRQ4 exit (records
# final at ~line 90) instead of only the idle loop; costs 68K lines there.
ifdef ERECIRQ4
MDASFLAGS += -Wa,--defsym,EREC_IRQ4=1
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
ifdef CARTBENCH
export CARTBENCH
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
endif
# WRAMCODE2=1 (2026-09-24 night): the third block set in WRAM region 2 (0xFF4600-0xFF4BFF,
# WRAM_BLOCKS2). Exact on ares; 0 conversions on the rig (LESSONS 23:50). A knob, off the line.
ifdef WRAMCODE2
export WRAMCODE2
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
# GAMEGATE=1 = LOOP29 141, THE PIVOT. The game's frame release (IRQ4 at
# 0x2AB8, LOOP-DECOMPILE 22) is patched to consult a shim token at WRAM
# 0xFFA0F5: the main loop advances one frame per token, the token is set
# on every flip echo (F102) or after GAMEGATE_MAXWAIT vints without one,
# and a held vint takes the game's own short path uncounted. The game
# runs to our presentation clock instead of racing the beam.
ifdef GAMEGATE
MDCCFLAGS += -DGAME_GATE -DGAMEGATE_MAXWAIT=$(if $(GAMEGATEWAIT),$(GAMEGATEWAIT),4)
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
endif
# COHERENT30: DEAD BY MIKE'S ORDER 2026-08-26 ("why on earth are you
# regressing to 30hz") — 30Hz stepping has no place in this
# architecture. Flag deleted; the 60Hz path is the MD-VDP sprite
# offload arc (P3). Do not rebuild this.
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
# THE ONE-ROM REGIONS' DATA (2026-10-30): the program bytes another regional set has and the
# reference lacks (games/$(GAME)/game.py REGION_DATA), taken from that set's MAME zip at build
# time. A missing set is an error, never an empty region.
sh_src/region_data.s: games/$(GAME)/game.py tools/region_data.py
	python3 tools/region_data.py $(GAME) sh_src
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
.PHONY: romset

port: romset
	GAME=$(GAME) tools/shadow_tree.sh $(PORT_DIR)
	$(MAKE) -C $(PORT_DIR) line $(PORT_FLAGS) $(PORT_EXTRA) ARTBAKE_CORPUS=$(abspath build/gate/neff/art)
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
	@# Freeze the matching 68K map too: the next SH probe can rebuild its MD stub.
	@cp $(PORT_DIR)/rom/md_start.lst $(PORT_DIR)/rom/$(PORT_SLUG).md.lst
	@cp $(PORT_DIR)/rom/md_start.elf $(PORT_DIR)/rom/$(PORT_SLUG).md.elf
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
# THE SHIPPING LINE IN ONE PLACE (2026-11-16). The source was stripped down to this option
# set, so it is the only configuration that builds: `make candidate`, `make check-demo`
# and `make release-rom` all use it, and tools/build_line.sh is a wrapper around candidate.
# Three inputs are not in git and are looked for under LINE_INPUTS:
#   sprite_macros.mc1     the sprite macro packets, or sprite_watch.csv.gz to pack them from
#   launcher_payload.bin  the launcher's page (the introlab build); NOLAUNCHER=1 leaves the
#                         launcher out, which is what the demo gate needs -- the launcher
#                         waits in its menu and the attract never starts
# The hot-function list for this line is tools/hot_set_macro.txt.
LINE_INPUTS ?= build/line-inputs
LINE_MACROS ?= $(LINE_INPUTS)/sprite_macros.mc1
# the capture sprite_macros.mc1 is packed from; when it is present the build packs the blob
# itself (the R60MACROCAPTURE rule). Game data, so it is not in git either.
LINE_MACRO_CAPTURE ?= $(LINE_INPUTS)/sprite_watch.csv.gz
# With neither file the rom is built without macro packets: same picture, every sprite
# drawn the ordinary way (the packets measured +8 presented frames in 7,201).
LINE_HAS_MACROS = $(if $(NOMACROS),,$(wildcard $(LINE_MACROS) $(LINE_MACRO_CAPTURE)))
LINE_LAUNCHER ?= $(LINE_INPUTS)/launcher_payload.bin
LINE_HOT_SET ?= tools/hot_set_macro.txt
LINE_ATLAS = build/graphics/$(GAME)/neff45.zb1
LINE_NOLAUNCHER = $(or $(NOLAUNCHER),$(findstring NOLAUNCHER=1,$(EXTRA)))
CANDIDATE_LINE = CARTSSF=1 R60SOUND=1 R60PSGMUSIC=1 R60PTONE=1 R60SOUNDCOMPACT=1 \
	R60SPANCAP=1536 R60TXTPOOLHYB=1 R60BAKEART=1 R60BAKEPAGES=1 R60PUNCHTHRU=1 R60SPRPREP=1 \
	R60SPRPREP2=1 R60SPRPREP2S=256 R60SPRPREP2H=24 \
	R60SPANPROGRESS=1 R60TILEBATCH=1 R60PLANROWS=1 R60ROWDIFF=1 R60ZOOMKEEP=1 R60BOOTBANKS=1 R60WORKSET=1 \
	R60WORKSETATLAS=$(abspath $(LINE_ATLAS)) R60WORKSETPOSES=$(abspath games/$(GAME)/graphics/neff-unscaled-22.json) \
	R60WORKSETCLIP=1 R60SNAPLEASE=1 R60ZOOMRUN=1 R60WORKSETCODE=4096 \
	R60MACROPROBE=1 R60MACROLIVE=1 R60MACROLIVEDRAW=1 \
	$(if $(LINE_HAS_MACROS),R60MACROPACKETS=$(abspath $(LINE_MACROS))) \
	$(if $(and $(LINE_HAS_MACROS),$(wildcard $(LINE_MACRO_CAPTURE))),R60MACROCAPTURE=$(abspath $(LINE_MACRO_CAPTURE)) R60MACROKEY=0x031401d0) \
	HOT_SET=$(abspath $(LINE_HOT_SET)) \
	$(if $(LINE_NOLAUNCHER),,LAUNCHER=1 LAUNCHER_PAYLOAD=$(abspath $(LINE_LAUNCHER)))
CANDIDATE_ALL = $(CANDIDATE_FLAGS) $(CANDIDATE_LINE) $(EXTRA)
# the scaled-pose atlas the working-set page is baked from; rebuilt every time, it is cheap
.PHONY: line-atlas
line-atlas:
	@[ -f sh_src/sprites.bin ] || { python3 tools/romset.py $(GAME); GAME=$(GAME) python3 tools/gen_sprites.py; }
	@mkdir -p $(dir $(LINE_ATLAS))
	python3 tools/extend_zoom_atlas.py --empty-base --manifest games/$(GAME)/graphics/neff-scaled-45.json \
	  --sprites sh_src/sprites.bin --budget 62528 --out $(LINE_ATLAS) --report $(basename $(LINE_ATLAS)).json >/dev/null
.PHONY: candidate
# The sound lanes' inputs are recorded from the user's MAME set (tools/sound_inputs.sh skips
# what is present), then baked by the sh_src/{speech,sfx}.bin rules below.
sound-inputs:
	GAME=$(GAME) tools/sound_inputs.sh
.PHONY: sound-inputs
# The music is transcoded from the eight YM2151 logs of the vgmrips.net pack
# "Altered Beast (Sega System 16B)" (v1.03), unpacked under $(SNDAUDIO). This bakes the two
# PSG channels of each song and writes sndtest/md/render_music_psg.h; it skips what is
# present. Proven 2026-11-16: from the rips alone it reproduces the accepted header and
# sh_src/sound.bin byte for byte.
music-inputs:
	@[ -s sndtest/md/render_music_psg.h ] || [ -s sh_src/sound.bin ] || { \
	  test -d "$(SNDAUDIO)" || { echo "music-inputs: no $(SNDAUDIO) -- unpack the vgmrips.net pack 'Altered Beast (Sega System 16B)' there (eight .vgz files)"; exit 2; }; \
	  for p in $(SNDMAP); do c=$${p%%=*}; f=$${p#*=}; \
	    [ -s $(PSG_BAKE)/$${c}_psg.json ] || python3 tools/psg_bake.py --vgz "$(SNDAUDIO)/$$f" --cmd $$c --out $(PSG_BAKE) || exit 2; done; \
	  $(MAKE) sndtest-music-psg; }
.PHONY: music-inputs
# The art census corpus is recorded the same way (tools/art_corpus.sh skips what is present).
art-corpus:
	GAME=$(GAME) tools/art_corpus.sh
.PHONY: art-corpus
# THE RELEASE ENTRY POINT: a fresh clone + mame/$(GAME).zip + mame on PATH -> one rom.
release-rom:
	$(MAKE) tables
	@test -s $(LINE_LAUNCHER) || echo "release-rom: no $(LINE_LAUNCHER) -- building without the launcher (a source build has none, README)"
	$(MAKE) candidate NAME=$(GAME) $(if $(wildcard $(LINE_LAUNCHER)),,NOLAUNCHER=1)
.PHONY: release-rom
candidate: release-check sound-inputs music-inputs art-corpus sh_src/speech.bin sh_src/sfx.bin line-atlas
	@test -n "$(NAME)" || { echo "make candidate NAME=<rom name>"; exit 2; }
	@test -n "$(LINE_HAS_MACROS)" || echo "candidate: no $(LINE_MACROS) and no capture -- building without the sprite macro packets"
	@for f in $(LINE_HOT_SET) $(if $(LINE_NOLAUNCHER),,$(LINE_LAUNCHER)); do \
	  test -s $$f || { echo "candidate: $$f is missing -- the line needs it (see THE SHIPPING LINE in the Makefile)."; exit 2; }; done
	@# THE SOUND BANKS ARE SEGA-DERIVED AND GITIGNORED: a tree without them links EMPTY
	@# .sounddata/.sfxdata and the rom loops a sound re-init with no effects or speech
	@# (sw270, 2026-10-20: the builder's checkout held 0-byte placeholders). Refuse.
	@for f in sh_src/speech.bin sh_src/speech_tab.h sh_src/sfx.bin sh_src/sfx_tab.h; do \
	  test -s $$f || { echo "candidate: $$f is missing or empty -- the speech/effects lanes would ship silent."; \
	    echo "  copy the banks from the advisor worktree (.claude/worktrees/advisor-bisect/sh_src/{speech,sfx}.bin and *_tab.h)"; exit 2; }; done
	@[ -s sh_src/sound.bin ] || $(MAKE) sh_src/sound.bin R60SOUND=1 R60PSGMUSIC=1 R60SOUNDCOMPACT=1
	@# the music blob must have been cut from the header the flags ask for (R60PSGMUSIC: the PSG squares)
	@if echo "$(CANDIDATE_ALL)" | grep -q 'R60SOUND=1'; then \
	  want=sndtest/md/render_music_lane.h; echo "$(CANDIDATE_ALL)" | grep -q 'R60PSGMUSIC=1' && want=sndtest/md/render_music_psg.h; \
	  echo "$(CANDIDATE_ALL)" | grep -q 'R60SOUNDCOMPACT=1' && want="$$want compact"; \
	  have=$$(cat sh_src/sound.bin.src 2>/dev/null); \
	  [ "$$have" = "$$want" ] || { echo "candidate: sh_src/sound.bin was cut from '$$have', the flags want $$want:"; \
	    echo "  run: make -B sh_src/sound.bin R60SOUND=1 $$(echo "$(CANDIDATE_ALL)" | grep -o 'R60PSGMUSIC=1') $$(echo "$(CANDIDATE_ALL)" | grep -o 'R60SOUNDCOMPACT=1')"; \
	    echo "  then make candidate again"; exit 2; }; fi
	$(MAKE) port PORT_EXTRA="$(CANDIDATE_ALL)" PORT_NAME=$(NAME)

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
# THE GATE BUILDS THE LINE (2026-11-16): `make candidate NAME=checkdemo NOLAUNCHER=1`, the only
# configuration the source builds. EXTRA='FLAG=1' adds a probe flag to the gated rom.
CHECK_DEMO_ROM := $(PORT_DIR)/rom/checkdemo.32x
ifdef PORT_EXTRA
export DEMO_PROBE = 1
endif
check-demo:
	$(MAKE) candidate GAME=$(GAME) NAME=checkdemo NOLAUNCHER=1 EXTRA="$(EXTRA)"
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
	DEMO_PROBE=1 python3 tests/demo-b/check_demo.py $(CHECK_DEMO_ROM)
	if [ "$(SWEEP)" = 1 ]; then python3 tests/demo-b/sweep_demo.py $(CHECK_DEMO_ROM); fi

# THE GOLDEN AXE ORACLE (tests/goldnaxe/oracle.py): the arcade side of a check-demo for the
# next title. goldnaxe-oracle regenerates tests/goldnaxe/arcade from one MAME run;
# goldnaxe-oracle-check builds a second set from a fresh boot and diffs it against the
# committed one (exit 1 on any byte). No port side exists yet.
.PHONY: goldnaxe-oracle goldnaxe-oracle-check
goldnaxe-oracle:
	python3 tests/goldnaxe/oracle.py build
goldnaxe-oracle-check:
	rm -rf build/goldnaxe-oracle
	python3 tests/goldnaxe/oracle.py build build/goldnaxe-oracle
	python3 tests/goldnaxe/oracle.py check tests/goldnaxe/arcade build/goldnaxe-oracle

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
# The game's blob is cut from render_music_lane.h (`make sndtest-music-lane`): the six YM2612
# channels of every song, channels 6/7 left to the SH-2 synth lane. The Oct-18 blob from
# render_music.h carried the PSG squares too and lane5 shipped them doubled (2026-10-31).
# `R60PSGMUSIC=1` (2026-11-01, Mike: "LETS USE IT", sndtest-psg1 accepted "PERFECT"): the blob is
# cut from render_music_psg.h (`make sndtest-music-psg`) instead: the six YM2612 channels AND
# channels 6/7 as the baked PSG squares (tools/psg_bake.py), no SH-2 synth lane. The blob's
# source is stamped so switching the flag re-cuts it (a stale sound.bin is silent wrongness).
ifdef R60PSGMUSIC
SND_MUSIC_H := sndtest/md/render_music_psg.h
else
SND_MUSIC_H := sndtest/md/render_music_lane.h
endif
SND_SRC := $(wildcard $(SND_MUSIC_H) sndtest/md/sndmap_data.h)
SND_MUSIC_SOURCE_STAMP ?= $(SND_MUSIC_H)
# The blob records its source in sh_src/sound.bin.src; `make candidate` refuses a blob cut from
# the other header (a parse-time stamp was tried first and re-cut the blob inside the shadow,
# where the headers do not exist). Re-cut with: make -B sh_src/sound.bin R60SOUND=1 [R60PSGMUSIC=1] [R60SOUNDCOMPACT=1]
sh_src/sound.bin md_src/sound_tab.h: tools/sound_blob.py $(SND_SRC)
	python3 tools/sound_blob.py $(SND_MUSIC_H) sndtest/md/sndmap_data.h sh_src/sound.bin md_src/sound_tab.h $(SND_BLOB_ARG)
	@echo "$(SND_MUSIC_SOURCE_STAMP)$(if $(SND_BLOB_STAMP), $(SND_BLOB_STAMP))" > sh_src/sound.bin.src
# Each blob is a dependency ONLY under its own flag: they are Sega-derived and gitignored, so a
# tree without them must still build the bare line (tools/flagcheck.sh checks exactly that).
ifdef R60SOUND
sh_src/sound_data.o: sh_src/sound.bin
sh_src/pages_data.o: sh_src/sound.bin
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
sh_src/r60/r60_main.o: sh_src/speech_tab.h sh_src/r60/r60_speech.inc
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

# R60BAKEART (2026-11-04): the baked-art windows, flags and addresses up at the flag block.
# NOT above `all:` -- the first rule in the file is the default goal of the inner
# `make GAME=... $(SHIP_US)`, and an earlier copy of these rules there baked and then built
# nothing else (p15b, 2026-11-04).
ifdef R60BAKEART
# $(FLAGSTAMP): the windows (PORTTAPEPAD drops window 1) and the page spec (R60BAKEPAGES) are
# flags, so a flag change must re-bake (the tape-build check once ran on a stale bake)
sh_src/artbake_0.bin: tools/bake_art.py tools/s16art.py sh_src/sprites.bin sh_src/sound.bin $(FLAGSTAMP)
	@test -s $(ARTBAKE_CORPUS)/round0/spr.bin -a -s $(ARTBAKE_CORPUS)/tape/spr.bin || { \
	  echo "bake_art: no census corpus at $(ARTBAKE_CORPUS) -- run: make art-corpus"; exit 2; }
	python3 tools/bake_art.py --rom sh_src/sprites.bin --chunks $(ARTBAKE_CHUNKS) \
	  $(if $(R60BAKEPAGES),--pages $(ARTBAKE_PAGESPEC)) \
	  --out sh_src/artbake.bin --hdr sh_src/artbake.h $(ARTBAKE_RUNS)
sh_src/artbake_1.bin sh_src/artbake_2.bin sh_src/artbake_3.bin sh_src/artbake.h $(ARTBAKE_PAGEBINS): sh_src/artbake_0.bin
	@:
sh_src/artbake_data.o: sh_src/artbake_1.bin sh_src/artbake_2.bin sh_src/artbake_3.bin
sh_src/pages_data.o: sh_src/artbake_0.bin $(ARTBAKE_PAGEBINS)
ifdef LAUNCHER
sh_src/launcher_page.bin: tools/launcher_page.py $(LAUNCHER_PAYLOAD) $(FLAGSTAMP)
	python3 tools/launcher_page.py $@ $(LAUNCHER_BASE) $(LAUNCHER_PAYLOAD)
endif
sh_src/r60/r60_main.o: sh_src/artbake.h
ifdef R60WORKSET
sh_src/r60_workingset.bin: tools/bake_sprite_workingset.py tools/bake_art.py tools/bake_zoom.py tools/s16art.py \
		sh_src/artbake_0.bin sh_src/artbake_1.bin sh_src/artbake_2.bin sh_src/artbake_3.bin \
		sh_src/sprites.bin sh_src/sound.bin $(R60WORKSETATLAS) $(R60_WORKSET_POSES_EFFECTIVE) \
		$(R60_MACRO_DEPS) $(FLAGSTAMP)
	python3 tools/bake_sprite_workingset.py --global-index-bin sh_src/artbake_0.bin \
		--global-chunk 0x02375000:sh_src/artbake_1.bin \
		--global-chunk 0x022B1000:sh_src/artbake_2.bin \
		--global-chunk 0x02298000:sh_src/artbake_3.bin \
		--sprites sh_src/sprites.bin --unscaled-manifest $(R60_WORKSET_POSES_EFFECTIVE) \
		--zoom-atlas $(R60WORKSETATLAS) --music sh_src/sound.bin \
		$(R60_CODE_BAKE_ARGS) $(R60_WORKSET_RUN_ARG) $(R60_MACRO_BAKE_ARG) \
		--out $@ --metadata sh_src/r60_workingset.json --header sh_src/r60_workingset_data.h
sh_src/r60_workingset_data.h sh_src/r60_workingset.json: sh_src/r60_workingset.bin
	@:
# THE SPRITE MACRO PACKETS, FROM THE CAPTURE THEY WERE PACKED FROM (2026-11-16).
# R60MACROCAPTURE is a gzipped ares watch of the port's sprite table (one row per emulator
# frame). The page is baked once without macros to learn how many bytes are free
# (free_art_gap_bytes); the packer then fills that gap. Proven: this reproduces the accepted
# blob byte for byte (SHA-256 40256c905430c1a2...).
ifdef R60MACROCAPTURE
$(R60MACROPACKETS): $(R60MACROCAPTURE) tools/pack_sprite_macros.py tools/sprite_macro_census.py \
		tools/bake_sprite_workingset.py sh_src/artbake_0.bin sh_src/artbake_1.bin sh_src/artbake_2.bin \
		sh_src/artbake_3.bin sh_src/sprites.bin sh_src/sound.bin $(R60WORKSETATLAS) $(R60_WORKSET_POSES_EFFECTIVE)
	python3 tools/bake_sprite_workingset.py --global-index-bin sh_src/artbake_0.bin \
		--global-chunk 0x02375000:sh_src/artbake_1.bin \
		--global-chunk 0x022B1000:sh_src/artbake_2.bin \
		--global-chunk 0x02298000:sh_src/artbake_3.bin \
		--sprites sh_src/sprites.bin --unscaled-manifest $(R60_WORKSET_POSES_EFFECTIVE) \
		--zoom-atlas $(R60WORKSETATLAS) --music sh_src/sound.bin \
		$(R60_CODE_BAKE_ARGS) $(R60_WORKSET_RUN_ARG) \
		--out sh_src/macro_base.bin --metadata sh_src/macro_base.json --header sh_src/macro_base.h
	gzip -dc $(R60MACROCAPTURE) > sh_src/macro_capture.csv
	python3 tools/pack_sprite_macros.py sh_src/macro_capture.csv \
		--workset sh_src/macro_base.bin --workset-json sh_src/macro_base.json --global-root . \
		--key $(R60MACROKEY) --min-saved-stores 1000 --shared-assets \
		--budget $$(python3 -c "import json;print(json.load(open('sh_src/macro_base.json'))['free_art_gap_bytes'])") \
		--out $@ --report $(basename $@).json
endif
sh_src/pages_data.o: sh_src/r60_workingset.bin $(if $(LAUNCHER),sh_src/launcher_page.bin)
sh_src/r60/r60_main.o: sh_src/r60_workingset_data.h games/$(GAME)/r60_game_workingset.h sh_src/r60/r60_workingset.h
endif
ifdef R60WORKSETCODE
sh_src/r60/r60_sprite_blocks_code.o: sh_src/r60/r60_sprite_blocks_code.inc \
		sh_src/r60_workingset_data.h tools/check_workset_code.py $(FLAGSTAMP)
	$(SHCC) $(SHCCFLAGS) $(filter-out -flto -fuse-linker-plugin,$(SHEXTRA)) $(SHINCS) \
		-Ish_src -Ish_src/r60 -x c -m2 -mb -O2 -fomit-frame-pointer \
		-fno-lto -ffreestanding -fno-pic -fno-pie -MMD -MP \
		-MF $(@:.o=.d).tmp -MT $@ -c $< -o $@.tmp
	python3 tools/check_workset_code.py $@.tmp $(if $(R60MACROLIVE),--entry _r60_mc_code_draw_packet_ready,$(if $(R60TEMPORALLIVE),--entry _r60_tp_code_run,))
	mv $(@:.o=.d).tmp $(@:.o=.d)
	mv $@.tmp $@
endif
endif

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
              sndtest/sh/speech_bank.o sndtest/sh/opm_bench.o sndtest/opm/opm.o

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
# The music: the 8 VGZ rips (gitignored, srcref/audio) transcoded through
# tools/build_vgm_music.py with the tracked trims (sndtest/music_trims.json,
# tools/fit_voices.py). render_music.h is generated and gitignored, so this
# is the ONLY record of the song-to-command map; `make sndtest-music` then
# `make sndtest`. rom/sndtest-tuned3.32x (accepted by Mike 2026-10-31) is
# exactly this recipe.
SNDAUDIO = srcref/audio/Altered_Beast_(Sega_System_16B)
# 0x93 = round clear (10 s one-shot), 0x95 = rounds 2/5: the game posts them so (HANDOFF-SOUND.md:612) and
# the real driver plays 0x93 for 10 s and 0x95 past 76 s (build/tapes/song_len.txt, 2026-11-02); they were swapped.
SNDMAP   = "0x90=03 Gaum Hermer (Boss Theme).vgz" \
           "0x91=02 Altered Beast (Power Up).vgz" \
           "0x92=08 Game Over ~ Name Entry.vgz" \
           "0x93=04 Crystal's Theme (Round Clear).vgz" \
           "0x94=01 Rise From Your Grave (Round 1, 4).vgz" \
           "0x95=05 Closed In Upon Me (Round 2, 5).vgz" \
           "0x96=06 I'll Be Back (Round 3).vgz" \
           "0x97=07 Who's Afraid of the Big Bad Wolf (Ending).vgz"
.PHONY: sndtest-music
sndtest-music:
	python3 tools/build_vgm_music.py --map $(SNDMAP) --audio "$(SNDAUDIO)" \
	    --trims sndtest/music_trims.json --out sndtest/md/render_music.h
	rm -f sndtest/md/md_main.o
# `make sndtest-music-psg` (2026-11-01, Mike: "LETS USE IT"): channels 6/7 on the Mega Drive PSG
# from tools/psg_bake.py's per-frame streams (PSG_BAKE: one <cmd>_psg.json a song, baked by
# `python3 tools/psg_bake.py --vgz ... --cmd 0x9N`), merged into the Z80 stream on the song's
# own timeline. The sound cart built after it (make sndtest) is sndtest-psg1's recipe.
PSG_BAKE ?= build/gate/psg
.PHONY: sndtest-music-psg
sndtest-music-psg:
	python3 tools/build_vgm_music.py --map $(SNDMAP) --audio "$(SNDAUDIO)" \
	    --trims sndtest/music_trims.json --psg-bake $(PSG_BAKE) --out sndtest/md/render_music_psg.h
	cp sndtest/md/render_music_psg.h sndtest/md/render_music.h
	rm -f sndtest/md/md_main.o

# sndmap_data.h (tools/soundmap_build.py over a tools/cmd_sweep.lua
# log) is generated from transient MAME tap captures and committed —
# see docs/sound/SOUND.md P4.
sndtest/md/md_main.o: sndtest/md/z80_player.h sndtest/md/test_track.h \
                      sndtest/md/sndmap_data.h
SNDMDASFLAGS = -x assembler-with-cpp -Isndtest/md -m68000 -Wa,--register-prefix-optional
SNDSHASFLAGS = -Isndtest/sh --small
SNDEXTRA     = -O2 -fomit-frame-pointer
# OPM_BENCH=1: the sound cart times the YM2151 channel engine at boot (sndtest/sh/opm_bench.c);
# OPM_BENCH_SONG=1 adds a replay of a song's overflow channels (sndtest/sh/opm_bench_song.h,
# generated by tools/opm_song_bench_gen.py; tools/opm_bench.sh does both)
ifeq ($(OPM_BENCH),1)
SNDEXTRA += -DOPM_BENCH
endif
# The sound cart's objects track SNDEXTRA through this stamp (like .build_flags for the
# game): a flag build after a plain build used to reuse objects compiled without the flag --
# the lane cart shipped with a bench-compiled m_main.o that ran opm_bench() at boot
# (2026-10-31). The stamp is rewritten only when the flags change.
SNDFLAGS = sndtest/.sndflags
ifeq ($(OPM_BENCH_SONG),1)
SNDEXTRA += -DOPM_BENCH_SONG
endif
# OPM_LANE=1: the sound cart synthesizes YM2151 channels 6/7 on the secondary SH-2 into the
# PWM lane (sndtest/sh/opm_lane.c, hybrid 3) and the Z80 stream leaves them out; the inputs
# are `make sndtest-music-lane` (render_music_lane.h + opm_music.h). tools/opm_lane_build.sh
# does the whole thing into build/lane; the objects do not track these flags, it removes them.
OPM_LANE_RATE ?= 16002.3           # the PWM clock as it runs: 23011361 / 1438 (tools/opm_lanesync.sh)
OPM_LANE_ROWSCALE ?= 1.00028       # the Z80 player's clock against the PWM's, measured (tools/opm_lanesync.sh)
ifeq ($(OPM_LANE),1)
SNDEXTRA += -DOPM_LANE $(OPM_LANE_DEFS)
SNDSHOBJS += sndtest/sh/opm_lane.o
endif
sndtest/sh/opm_lane.o: sndtest/sh/opm_music.h sndtest/sh/opm_lane.h
.PHONY: sndtest-music-lane
sndtest-music-lane:
	python3 tools/build_vgm_music.py --map $(SNDMAP) --audio "$(SNDAUDIO)" \
	    --trims sndtest/music_trims.json --drop-psg --out sndtest/md/render_music_lane.h
	python3 tools/opm_music_gen.py --map $(SNDMAP) --audio "$(SNDAUDIO)" \
	    --rate $(OPM_LANE_RATE) --row-scale $(OPM_LANE_ROWSCALE) --out sndtest/sh/opm_music.h
	rm -f sndtest/md/md_main.o sndtest/sh/opm_lane.o
SNDEXTRA += $(OPM_BENCH_DEFS)     # e.g. -DOPM_BENCH_SKIP_OUT: cost attribution (sndtest/opm/opm.c)
$(SNDFLAGS): FORCE_SNDFLAGS
	@[ "$$(cat $@ 2>/dev/null)" = "$(SNDEXTRA)" ] || echo "$(SNDEXTRA)" > $@
.PHONY: FORCE_SNDFLAGS

.PHONY: sndtest
sndtest: $(SNDTARGET).32x

sndtest/md/%.o: sndtest/md/%.s
	@echo "MDAS $<"
	@$(MDCC) $(SNDMDASFLAGS) -c $< -o $@

sndtest/md/%.o: sndtest/md/%.c $(SNDFLAGS)
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

sndtest/sh/%.o: sndtest/sh/%.c $(SNDFLAGS)
	@echo "SHCC $<"
	@$(SHCC) $(SHCCFLAGS) $(SNDEXTRA) $(SHINCS) -MMD -MP -c $< -o $@

# the YM2151 channel engine (sndtest/opm, host-gated against jt51) for the SH-2: code and the
# tables it reads in SDRAM (OPM_RAM: gcc promotes a never-written static into .rodata, the cart)
sndtest/opm/%.o: sndtest/opm/%.c $(SNDFLAGS)
	@echo "SHCC $<"
	@$(SHCC) $(SHCCFLAGS) $(SNDEXTRA) $(SHINCS) '-DOPM_CODE=__attribute__((section(".ramtext")))' \
	    '-DOPM_RAM=__attribute__((section(".data")))' -MMD -MP -c $< -o $@

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
# `make ... PORTRIG=1 PORTRIGCLK=1` = THE WALL CLOCK: rows 216-219 = the master's vint count,
# rows 220-223 = the game's frame counter, each a raw 16-bit word as 16 binary blocks of 16 px.
# Two rig screenshots at known wall times give vints/s and game ticks/s (tools/rig_clock.py).
# Built 2026-11-03 because the x5 bar saturated and could not say whether the 64-vint window
# itself was honest. NEVER SHIP.
ifdef PORTRIGCLK
SHCCFLAGS += -DPORT_RIGCLK=1
endif

ifdef PORTFFBM
SHCCFLAGS += -DPORT_FFBM=$(PORTFFBM)
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
	    CPORT_ENTRY_GUARDS='$(CPORT_ENTRY_GUARDS)' \
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
sh_src/portfull/port.o: $(CPORT_ROOTFILE) docs/proof/$(GAME).c $(FLAGSTAMP) $(PORTSTAMP) docs/audit/width_census.json docs/audit/regwrites.json tools/cport_prep.py tools/cport_entry_guards.py tools/cport_build.sh $(CPORT_HAND)
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
$(shell f='$(CPORT_ROOTS_SET) | $(CPORT_STUB_WAIT) | $(CPORT_OPT_SET) | $(CPORT_CLAMP) | $(CPORT_A6) | $(CPORT_HOT) | $(CPORT_AFTER) | $(CPORT_ENTRY_GUARDS)'; \
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
# CARTSSF=1: the 4 MB layout with the rom region opened to 16 MB and the paged region
# (.pages) placed at 0x400000. Nothing addressed straight moves; the 68K-visible bound holds.
# THE RULE IS tools/gen_ld_ssf.py (2026-11-02). db2404e4 added it, 37273e6f reverted it, 7873fcad
# brought the repacked layout back WITHOUT the rule: every build since linked against a stale
# generated mars4s.ld in the shadow (the inline python below it only widened the rom and added
# .pages). Deleting that file reproduced the old layout. The generator is the one source.
sh_src/mars4s.ld: sh_src/mars4.ld tools/gen_ld_ssf.py $(FLAGSTAMP)
	python3 tools/gen_ld_ssf.py $< $@ $(R60_CODE_LD_ARGS) $(if $(LAUNCHER),--launcher)
