# The build options: each block turns one make variable into compiler defines, objects
# or a linker script. The shipping line sets them through the lists in mk/line.mk.
# Part of the top-level Makefile; included, never run on its own.
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
# `make ... BOOTFLIPRATE=1 TAILCENSUS=1` = NOTES 49 / LOOP29 260: the 68K's
# IRQ4 tail split in lines (entry->A, batch A, batch B, pump, blast,
# blast->post, idle before IRQ4) as means per 64 vints; tag 7 = vints with
# no idle. The GAMEGATE thunk stamps the frame-done V (patch_game.py).
ifdef TAILCENSUS
export TAILCENSUS
endif
# `make ... C1PUNCH=1` = fold 1's hole punch (LOOP29 175/243): sprite
# pixels of priority < 3 are not written into cells the FG holds as
# cat-1 (a 40x28 mask from the master's name-table pass), so the MD's
# plane A HIGH shows the ground over them. Needs CAT1MD + C1NOFB.
ifdef C1PUNCH
SHCCFLAGS += -DC1_PUNCH
SHOBJS += sh_src/cat1hole_data.o
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
CPORT_HOT = $(GAME_CPORT_HOT)
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
ifdef R60RECTCLR
SHCCFLAGS += -DR60_RECTCLR
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
ifdef R60ROWCLR
SHCCFLAGS += -DR60_ROWCLR
endif
ifdef R60PLOT5
SHCCFLAGS += -DR60_PLOT5
endif
# `R60PRIO=1` = priority FG tiles over sprites: after each sprite the slave zeroes its pixels
# under the opaque pixels of every priority (bit 15) FG cell it overlaps (jts16_prio.v:63-86).
ifdef R60PRIO
SHCCFLAGS += -DR60_PRIO
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
ARTBAKE_CORPUS ?= $(abspath $(GAME_ART_CORPUS))
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
# `R60PUNCHTHRU=1` (with R60PUNCH2) = THE NO-READ PUNCH (2026-11-05): the FG punch writes a reserved
# through pen (CRAM bit 15, entry 0's colour; 255 on a NOBARS build, 254 under bars) through the
# overwrite image instead of zeroing by read-modify-write over the uncached alias. On the FPGA every
# uncached access is ~16-18 cycles (R60BKBENCH=2), and a solid cell-row took five of them.
ifdef R60PUNCHTHRU
SHCCFLAGS += -DR60_PUNCHTHRU
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
# the title's native emitter guard (games/$(GAME)/game.mk)
CPORT_ENTRY_GUARDS ?= $(GAME_CPORT_ENTRY_GUARDS)
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
# `R60TILEBATCH=1`: complete FG/BG inline-art backend, grouped pattern uploads,
# persistent blank pattern, and menu/reset invalidation. Requires R60SPANPROGRESS.
ifdef R60TILEBATCH
ifndef R60SPANPROGRESS
$(error R60TILEBATCH requires R60SPANPROGRESS)
endif
SHCCFLAGS += -DR60_TILEBATCH
MDCCFLAGS += -DR60_TILEBATCH
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
# `make ... PHASECENSUS=1` = BOSSFIGHT pipelining arc, datum #1: split the
# per-generation wall into echo / mtask / close-lag / ship / flip (master
# FRT ticks, scratch 0x28E40 — the audited-free span). Probe-only; the
# arithmetic is ROM-resident so .ramtext pays only the stamps. Read with
# tools/nat_score.py (headless dump) or tools/state_health.py (savestate).
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
# `make ... XDEF=NAME` = one extra -DNAME for probe variants (SHEXTRA is
# the optimisation-flag slot; overriding it drops -O2 and bloats RAMCODE).
ifdef XDEF
SHCCFLAGS += -D$(XDEF)
endif
# `make ... ARTTAIL=1` = tile art rides along in every cell chunk (the
# scroll-in pop-in: art used to wait for a tile chunk, phase 0 of the
# 9-window rotation). Both CPUs.
ifdef ARTTAIL
SHCCFLAGS += -DART_TAIL
MDCCFLAGS += -DART_TAIL
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
# SHADCAP=n = max shadow-record height that gets TRUE darkening (taller
# renders silhouette). 48 was the old budget cap; 255 measured clean
# 2026-08-26 (cadence 1.049, bad1 145 best-ever) WITH the stale-LUT
# fallback and the through-stipple — the three ship together.
SHADCAP ?= 255
SHCCFLAGS += -DSHAD_CAP=$(SHADCAP)
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
# GAMEGATE=1 = LOOP29 141, THE PIVOT. The game's frame release (IRQ4 at
# 0x2AB8, LOOP-DECOMPILE 22) is patched to consult a shim token at WRAM
# 0xFFA0F5: the main loop advances one frame per token, the token is set
# on every flip echo (F102) or after GAMEGATE_MAXWAIT vints without one,
# and a held vint takes the game's own short path uncounted. The game
# runs to our presentation clock instead of racing the beam.
ifdef GAMEGATE
MDCCFLAGS += -DGAME_GATE -DGAMEGATE_MAXWAIT=$(if $(GAMEGATEWAIT),$(GAMEGATEWAIT),4)
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
