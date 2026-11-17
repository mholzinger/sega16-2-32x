# Build options removed from the Makefile, verbatim, oldest removal first.
# Their code is gone (tools/strip_dead.py); the comments are the record of what
# each one measured. Read by tools/build_facts.py. Nothing includes this file.
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
ifdef CMDINT
SHCCFLAGS += -DCMD_INT
MDCCFLAGS += -DCMD_INT
endif
ifdef ADAPTERCAL
MDCCFLAGS += -DADAPTER_CAL
endif
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
ifdef CARTREADADDR
MDCCFLAGS += -DCART_READ_ADDR=$(CARTREADADDR)
endif
ifdef CARTREADDELAY
MDCCFLAGS += -DCART_READ_DELAY=$(CARTREADDELAY)
endif
ifdef SLIMVALUE
MDCCFLAGS += -DSLIM_VALUE
endif
# `make line BOOTFLIPRATE=1 GLOWRATE=1 MDSVALUE=1` = the SH-2's tile-table install
# state on the value channel, flooded in alternate 4-second windows.
ifdef MDSVALUE
SHCCFLAGS += -DMDS_VALUE
MDCCFLAGS += -DMDS_VALUE
endif
# `make line CRAMPROBE=1` = BG palette readback (CRAM vs shadow) with the picture kept
ifdef CENSUS2
SHCCFLAGS += -DCENSUS2
MDCCFLAGS += -DCENSUS2
endif
ifdef CRAMPROBE
MDCCFLAGS += -DCRAM_PROBE
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
# `make ... BOOTFLIPRATE=1 CONSUMECENSUS=1` = LOOP29 262: packet B's consume
# split in lines (preamble / rows / tail / whole) as mean and max per 64 vints.
ifdef CONSUMECENSUS
MDCCFLAGS += -DCONSUME_CENSUS
endif
ifdef C1NOMASK
SHCCFLAGS += -DC1_NOMASK
endif#   C1RTOFF=1   the punch code is compiled but never taken at run time
ifdef C1RTOFF
SHCCFLAGS += -DC1_RTOFF
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
ifdef BOOTMDMODE
MDCCFLAGS += -DBOOT_MDMODE
endif
ifdef BOOTSLVALIVE
MDCCFLAGS += -DBOOT_SLVALIVE
endif
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
ifdef R60PLOT2
SHCCFLAGS += -DR60_PLOT2
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
ifdef R60CRAMPROBE
SHCCFLAGS += -DR60_CRAMPROBE
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
# `R60FSPROBE=1` = 68K probe: count A consumes during which FS (0xA1518A bit 0) changed
# (0xFFA07E), FS at the start (0xFFA080), A consumes (0xFFA082). 2026-10-16.
# `R60PENIDX=1` = MD pens owned per S16 colour index (shared across sets only by index) and
# recoloured in place when their owners' colours move: the title logo's white->red is a CRAM
# write instead of a 900-tile re-bake. Costs pens in busy scenes (no within-set dedupe). 2026-10-16.
ifdef R60PENIDX
SHCCFLAGS += -DR60_PENIDX
endif
ifdef R60EVPROBE
SHCCFLAGS += -DR60_EVPROBE   # PROBE: the master's tick start (9) and planes-prepare end (10) in the event ring
endif
ifdef R60PADBAR
SHCCFLAGS += -DR60_PADBAR
endif
ifdef R60LOGOPROBE
SHCCFLAGS += -DR60_LOGOPROBE
endif
# `R60OPMLANE=1` = the YM2151's two overflow channels synthesized on the slave into the PWM
# mixer (sh_src/r60/r60_opm.inc, the sound lab's engine sndtest/opm with OPM_MAXCH=2 and its
# tables in the cart), the Z80 stream without them (render_music_lane.h), the rows in cart
# .opmrows (sh_src/opm_rows.c from tools/opm_music_gen.py). Needs CARTSSF=1 R60SOUND=1
# R60SPEECH=1 R60SPEECHSLAVE=1. The sound cart's recipe (rom/sndtest-lane1.32x, accepted
# 2026-10-31) moved into the game.
ifdef R60OPMLANE
SHCCFLAGS += -DR60_OPMLANE -DOPM_MAXCH=2 -DOPM_TABLES_CONST -DOPM_TABLES_IN_OPMROWS   # NO QUOTES OR PARENS IN A FLAG: the flag stamp is a $(shell f='...') and a quoted attribute here silently froze it (stale objects linked, 2026-10-31)
MDCCFLAGS += -DR60_OPMLANE
endif
# `R60NORECOLOUR=1` = the recolour/split pass off (sw293, 2026-10-22): the rig A/B for the
# jutter Mike reported on sw292; the pass scans PAL_SH and the owner tables every 4th window,
# memory traffic ares does not charge (LESSONS: ares charges instructions only).
ifdef R60NORECOLOUR
SHCCFLAGS += -DR60_NORECOLOUR
endif
# `R60IDLE=1` = PROBE (2026-10-31): FRT ticks each core spends IDLE per vint (the master's wait
# for pp_due, the slave's gap from DONE to its next pass) into r60_idle_m / r60_idle_s (.bss,
# addresses in the .lst), read with ares --gameframe-watch. For the two-voice YM2151 synth:
# which core has the cycles, and in one piece or fragments. ares = instruction cycles only.
ifdef R60IDLE
SHCCFLAGS += -DR60_IDLE
endif
# `R60PHBARS=1` (with `R60NOBARS=`) = PROBE: the bottom bars show the slave's average scanlines
# per pass for prep / sprites / text / whole pass, one pixel a line (sw263, the speed card).
ifdef R60PHBARS
SHCCFLAGS += -DR60_PHBARS
endif
# `R60MIXBAR=1` = PROBE: screen rows 216..219 = the mixer's real underrun count, 220..223 = the
# synth lane's samples / 4096, one pixel each after a 16 px floor, cart-resident, readable on the
# rig strip (2026-11-01, the lane10 echo). R60PHBARS does not fit beside a lane build.
ifdef R60MIXBAR
SHCCFLAGS += -DR60_MIXBAR
endif
# `R60MIXBAR2=1` (with R60MIXBAR=1, a lane build) = PROBE: rows 220..223 show the lane's trigger
# instead of its samples: starts x 32 px + last command's low nibble x 2 px + 8 px while
# COMM14 carries a song (the rig strip of lane10mb read the lane's samples at ZERO).
ifdef R60MIXBAR2
SHCCFLAGS += -DR60_MIXBAR2
endif
# `R60SPUNDERSIL=1` (lane11, 2026-11-01) = a PWM side that plays without its fill plays silence
# from where the fill stopped instead of its ring-old content (sh_src/r60/r60_speech.inc,
# amb_dma_handler). The underrun count is unchanged; what one sounds like is.
ifdef R60SPUNDERSIL
SHCCFLAGS += -DR60_SPUNDERSIL
endif
# `R60LANESPINBOUND=1` (lane12, 2026-11-01) = the synth lane's vblank-edge spins give up after
# ~2 frames instead of holding the core for good (sh_src/r60/r60_opm.inc lane_start).
ifdef R60LANESPINBOUND
SHCCFLAGS += -DR60_LANESPINBOUND
endif
# `SPCHUNK=N` (2026-11-01) = samples the slave's pump fills per wait-site visit (SP_CHUNK,
# sh_src/r60/r60_speech.inc; 32 with the lane, 64 without). The rig probe: lane12tp made
# 2200-3500 lane samples/s of the 8000 the PWM plays (build/accept2/rig/pass_lane12tp);
# a 4x chunk tells a visit-rate bound (rate rises) from a synth-cost bound (rate stays).
ifdef SPCHUNK
SHCCFLAGS += -DSP_CHUNK=$(SPCHUNK)
endif
# `R60ARTCACHE=1` (2026-11-03) = the slave decodes a sprite's art ONCE a pass when the snapshot
# holds a twin of it (same art, rows, palette, zoom, 4-px alignment) and draws every instance as
# a longword copy of the packed rows (sh_src/r60/r60_main.c "THE ART CACHE"). Measured need: 47%
# of the words a pass in level 1's first wave are repeats (the ivy fence pillars). Needs
# R60PTONE=1 for the 3.5 KB entry (.bss sits on the 0x19000 guard) and moves the clear emitter to
# SDRAM region 2 to pay for its call site in .ramtext. `R60ARTCACHECHECK=1` replays every cached
# row through the plotter into RAM and counts differences (r60_ac_bad/badx/badn): zero over the
# tape is the identity gate. Rank on the presented census (DIAG[1]) over tape frames 1800-3600.
ifdef R60ARTCACHE
SHCCFLAGS += -DR60_ARTCACHE
endif
ifdef R60ARTCACHECHECK
SHCCFLAGS += -DR60_ARTCACHE -DR60_ARTCACHECHECK
endif
# `R60ARTCLR=1` (2026-11-03) = the row extent clear skips the groups the art cache's planned copies
# will store with every byte non-zero this pass (the pillars' rows were a whole-canvas fill, then
# the same opaque art 1-2 px over). Needs R60ARTCACHE=1 and R60CLREXT=1; table at SDRAM 0x29000-0x293FF.
# Gate: r60_ac_stat[5] == 0 (no planned record refused at draw) and [6] == [7] (every registered solid
# row was copied), with the R60ARTCACHECHECK row identity.
ifdef R60ARTCLR
SHCCFLAGS += -DR60_ARTCACHE -DR60_ARTCLR
endif
# `R60OPMSDRAM=1` (2026-11-01) = the synth lane's two per-sample tables (sine 2 KB, pow 6 KB)
# copied into SDRAM at the first opm_init instead of read from the cart (sndtest/opm/opm.c
# OPM_TABLES_SDRAM). Needs R60PTONE=1 for the 8 KB: .bss must not grow past the 0x06019000
# guard. The pow table drops its 13th row (values 0 and 1 of 2047).
ifdef R60OPMSDRAM
SHCCFLAGS += -DOPM_TABLES_SDRAM
endif
# `R60OPMCOST=1` (2026-11-01, rig probe with R60MIXBAR) = the mixer fill's cost: rows 208..211
# = CPU cycles a filled sample / 16, rows 212..215 = samples a filling visit (r60_speech.inc
# sp_cost_census). Splits "each sample is dear" from "the slave rarely visits the pump".
ifdef R60OPMCOST
SHCCFLAGS += -DR60_OPMCOST
endif
# `R60OPMUNCACHED=1` (2026-11-01) = the synth's sine/pow tables read through the uncached window
# (sndtest/opm/opm.c OPM_TABLES_UNCACHED): with R60OPMSDRAM their 8 KB of cached SDRAM shares
# the 4 KB cache with the synth's cart code. Probe: does cycles/sample (R60OPMCOST) fall?
ifdef R60OPMUNCACHED
SHCCFLAGS += -DOPM_TABLES_UNCACHED
endif
# Read-only camera/enemy/record footer for physical scroll-lock diagnosis.
# No game RAM writes or forced cleanup; absent from ordinary candidates.
ifdef R60LOCKPROBE
SHCCFLAGS += -DR60_LOCKPROBE
endif
# `R60CLRPROBE=1` = PROBE: FRT timers and counts for the ROWCLR clear (it runs before R60_PH[5]++,
# so no PHBARS report times it): total, FEN waits by site, CPU short spans, fills, the span-length
# histogram, in r60_clrp[24] (.bss; map in r60_main.c). 2026-10-24, the clear lane on sw309.
# R60CLRPROBE=2 = the light form (totals and counts, waits untimed) that fits beside R60CLRMERGE.
ifdef R60CLRPROBE
SHCCFLAGS += -DR60_CLRPROBE=$(R60CLRPROBE)
endif
# `R60CLEARPLAN=1`: fold clear-span metadata once per contiguous sprite run;
# retain exact per-row extents and refusal/fallback rules; default off.
ifdef R60CLEARPLAN
ifndef R60CLRPRE
$(error R60CLEARPLAN requires R60CLRPRE)
endif
ifndef R60CLREXT
$(error R60CLEARPLAN requires R60CLREXT)
endif
SHCCFLAGS += -DR60_CLEARPLAN
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
# `R60PUNCHPROF=1` (with `R60NOBARS=`) = PROBE: the punch's cycle split per priority cell (resolve,
# rows, cold mask rebuilds) and its counts, as the R60BKBENCH bar layout; tools/rig_bkbench.py --punchprof.
ifdef R60PUNCHPROF
SHCCFLAGS += -DR60_PUNCHPROF
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
# Experimental 32x32 composed FG opacity cache; default retains the tile punch.
ifdef R60MASKBLOCK
SHCCFLAGS += -DR60_MASKBLOCK
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
ifdef R60MACROAUDIT
ifneq ($(strip $(R60MACROAUDIT)),1)
$(error R60MACROAUDIT must be 1)
endif
ifndef R60MACROLIVE
$(error R60MACROAUDIT requires R60MACROLIVE=1)
endif
SHCCFLAGS += -DR60_SPRITE_MACRO_AUDIT=1
endif
# Admission-only instrumentation.  It performs collision-free old/new table
# lookup but does not alter keep, clear, draw or priority state.
ifdef R60TEMPORALPROBE
ifneq ($(strip $(R60TEMPORALPROBE)),1)
$(error R60TEMPORALPROBE must be 1)
endif
ifndef R60TEMPORAL
$(error R60TEMPORALPROBE requires R60TEMPORAL=1)
endif
SHCCFLAGS += -DR60_TEMPORAL_PROBE=1
endif
# Diagnostic-only page lease for the temporal admission probe.  This changes
# no sprite records and enables no patch stores; it only keeps page 7 mapped
# so a route can prove whether its natural N-2/N tables match the packet.
ifdef R60TEMPORALFORCE
ifneq ($(strip $(R60TEMPORALFORCE)),1)
$(error R60TEMPORALFORCE must be 1)
endif
ifeq ($(strip $(R60TEMPORALPROBE)$(R60TEMPORALLIVE)),)
$(error R60TEMPORALFORCE requires R60TEMPORALPROBE=1 or R60TEMPORALLIVE=1)
endif
SHCCFLAGS += -DR60_TEMPORAL_FORCE=1
endif
# Exact two-bank delta application.  Default off until the fixed transition
# fixture and a natural smoke route both pass framebuffer identity.
ifdef R60TEMPORALLIVE
ifneq ($(strip $(R60TEMPORALLIVE)),1)
$(error R60TEMPORALLIVE must be 1)
endif
ifndef R60TEMPORAL
$(error R60TEMPORALLIVE requires R60TEMPORAL=1)
endif
ifneq ($(strip $(R60WORKSETCODE)),4096)
$(error R60TEMPORALLIVE requires R60WORKSETCODE=4096)
endif
SHCCFLAGS += -DR60_TEMPORAL_LIVE=1
endif
# R60WORKSETCACHE=1: optional typed per-slot packet admission, default OFF.
ifdef R60WORKSETCACHE
ifndef R60WORKSET
$(error R60WORKSETCACHE requires R60WORKSET)
endif
ifneq ($(strip $(R60WORKSETCACHE)),1)
$(error R60WORKSETCACHE must be 1)
endif
SHCCFLAGS += -DR60_WORKSET_CACHE=1
endif
ifdef R60WORKSETSTATS
ifndef R60WORKSET
$(error R60WORKSETSTATS requires R60WORKSET)
endif
ifneq ($(strip $(R60WORKSETSTATS)),1)
$(error R60WORKSETSTATS must be 1)
endif
SHCCFLAGS += -DR60_ZOOMBAKE_STATS
endif
ifdef R60WORKSETFORCE
ifndef R60WORKSET
$(error R60WORKSETFORCE requires R60WORKSET)
endif
ifndef PORTSPRITEFIXTURE
$(error R60WORKSETFORCE is diagnostic-only and requires PORTSPRITEFIXTURE)
endif
ifneq ($(strip $(R60WORKSETFORCE)),1)
$(error R60WORKSETFORCE must be 1)
endif
SHCCFLAGS += -DR60_WORKSET_FORCE
endif
# `R60ZOOMWORDS=1` = optional compacted-word zoom scaler, preserving the old
# onscreen padded stores and strict scalar left clipping. No atlas/RAM arena.
# Host and native-image checks validate this path; the old ZOOMCHECK probes the
# previous byte-push implementation and is not its checker.
ifdef R60ZOOMWORDS
ifndef R60ZOOMFAST
$(error R60ZOOMWORDS requires R60ZOOMFAST=1)
endif
ifndef R60ROWFAST
$(error R60ZOOMWORDS requires R60ROWFAST=1)
endif
ifdef R60ZOOMCHECK
$(error R60ZOOMWORDS uses native-image/host checks, not the old R60ZOOMCHECK)
endif
SHCCFLAGS += -DR60_ZOOMWORDS
endif
# Counter-only event probe: omit phase FRT stamps/bars while retaining the
# native/VInt/ack boundaries and rendered-cohort/pass-edge counters.
ifdef R60EVENTLIGHT
ifeq ($(filter $(R60EVENTLIGHT),0 1),)
$(error R60EVENTLIGHT must be 0 or 1)
endif
ifeq ($(R60EVENTLIGHT),1)
ifndef R60EVENTMETER
$(error R60EVENTLIGHT requires R60EVENTMETER)
endif
ifneq ($(R60RIGSPLIT),4)
$(error R60EVENTLIGHT requires R60RIGSPLIT=4)
endif
SHCCFLAGS += -DR60_EVENT_LIGHT=1
endif
endif
ifdef R60ZOOMCHECK
SHCCFLAGS += -DR60_ZOOMCHECK
endif
ifdef R60PLOTSTATS
SHCCFLAGS += -DR60_PLOTSTATS
endif
ifdef R60PLOT4CHECK
SHCCFLAGS += -DR60_PLOT4CHECK
endif
ifdef R60SPANPROBE
MDCCFLAGS += -DR60_SPANPROBE
endif
ifdef R60ROWDIFFCHECK
ifndef R60ROWDIFF
$(error R60ROWDIFFCHECK requires R60ROWDIFF)
endif
SHCCFLAGS += -DR60_ROWDIFFCHECK
endif
# Fault injection only: stop the consumer after N progress publications.
ifdef R60SPANSTALLAFTER
ifndef R60SPANPROGRESS
$(error R60SPANSTALLAFTER requires R60SPANPROGRESS)
endif
MDCCFLAGS += -DR60_SPANSTALLAFTER=$(R60SPANSTALLAFTER)u
endif
ifdef R60FSPROBE
MDCCFLAGS += -DR60_FSPROBE
endif
ifdef R60CHURNCAP
SHCCFLAGS += -DR60_CHURNCAP=$(R60CHURNCAP)u -DR60_CHURNMIN=$(if $(R60CHURNMIN),$(R60CHURNMIN),100)u
endif
ifdef R60FILLCLR
SHCCFLAGS += -DR60_FILLCLR
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
ifdef R60BARSFIRST
SHCCFLAGS += -DR60_BARSFIRST
endif
ifdef R60RBPROBE
SHCCFLAGS += -DR60_RB_PROBE
MDCCFLAGS += -DR60_RB_PROBE
endif
# `R60PKTPROBE=1` = PROBE ONLY: md_consume records, per buffer and unconditionally,
# what it actually reads at the packet's first word. 0xFFA020 cannot answer that --
# both consume calls write it and B runs second.
ifdef R60PKTPROBE
MDCCFLAGS += -DR60_PKT_PROBE
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
ifdef SHIMNOPUSH
MDCCFLAGS += -DSHIM_NOPUSH
endif
ifdef SHIMBURN
MDCCFLAGS += -DSHIM_BURN=$(SHIMBURN)
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
ifdef MDSNOFLUSH
SHCCFLAGS += -DMDS_NOFLUSH
endif
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
# `make ... NTMAXAGE=n` = LOOP29 171. How many windows a name-table row
# may go unwalked before NTSKIP forces a walk regardless of its key.
# The skip trusts cap_page's content compare; on the FPGA that trust is
# not earned, and an unbounded skip renders missing tiles there while
# reading clean in ares. Default 16.
ifdef NTMAXAGE
SHCCFLAGS += -DNT_MAXAGE=$(NTMAXAGE)
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
ifdef FRAMEDONEWAIT
SHCCFLAGS += -DFRAMEDONE_MAXWAIT=$(FRAMEDONEWAIT)
endif
# `make ... DRIFTTOL=n` = LOOP29 160: the squared-colour-distance at which
# a co-owner set is declared drifted and FREED (default 18, both sites).
# The free destroys ~46 tile slots, 95% of them on screen (156), so this
# constant trades attract-screen colour against background stability.
ifdef DRIFTTOL
SHCCFLAGS += -DDRIFT_TOL=$(DRIFTTOL)
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
# `make ... NEARMERGE=1` = near-pen merge: two pixels of a set within 2/31
# per channel share one MD pen (flattens the sky band's dither like the
# arcade). OFF by default: merging at assignment time is wrong for scenes
# entered through a fade (everything is near black then) — Mike's
# transformation chevrons went flat, the level load-in kept the wrong
# palette. Needs an unmerge-on-drift before it can ship (docs/design/BOSSFIGHT.md).
ifdef NEARMERGE
SHCCFLAGS += -DNEAR_MERGE
endif
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
# PGKEEPB=1 = LOOP29 139 hardware probe: with PGSKIPPKT, still mirror MD-plane
# packet B (page 12 second half) across banks through the page truth.
ifdef PGKEEPB
SHCCFLAGS += -DPG_KEEP_B
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
# NTWIPEGEN=1 = 2026-09-21: bump the name-table walk generation whenever
# slot tags are wiped (install, set free, round change) so rows naming
# wiped slots re-walk at their next visit instead of after NT_MAXAGE
# (the eye scene's stale patches).
ifdef NTWIPEGEN
SHCCFLAGS += -DNT_WIPE_GEN
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
# MDTWOCHOICE=1 (2026-09-25): the MD slot table lets a tile live in either of two hash sets
# (capacity like 16 ways). Off the line: a tile in its alternate set costs four cache lines a
# hit and the FPGA read ~20 lines a pass slower (wc7/wc8). The cset+plane hash alone is the line.
ifdef MDTWOCHOICE
SHCCFLAGS += -DMD_TWO_CHOICE
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
# MDSPRSPIKE=1 = P3 M0 spike (docs/design/P3.md): two hardcoded MD hardware
# sprites (SAT prio 1 and 0) + test art + line-0 colors at first
# vint. Proves SAT@0xF000 alignment, sprite-vs-plane priority both
# ways, and FB-vs-sprite mixing in one screenshot. NEVER SHIP.
ifdef MDSPRSPIKE
MDCCFLAGS += -DMDSPR_SPIKE
endif
# MDSPRDOUBLE=1: bisect aid — claim+SAT+DMA all run but compose ALSO
# draws claimed records (double render). Separates MD-side defects
# from compose-skip side effects. NEVER SHIP.
ifdef MDSPRDOUBLE
SHCCFLAGS += -DMDSPR_DOUBLE
endif
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
# `make ... PORTHIST=1` = THE OWED-VINT HISTOGRAM, pp_hist[8] indexed by the vints one
# master frame body spanned (7 = 7 or more). 1 means the port kept arcade time that frame;
# 3 means the body was three vints long and two game frames were dropped. Read it with a
# dump of pp_hist at two frames and difference them -- that is the per-scene histogram.
# Costs one add and one store per body. Not on by default; it is an instrument.
ifdef PORTHIST
SHCCFLAGS += -DPORT_HIST=$(PORTHIST)
endif
# `make ... PORTGENSEED=1` = SEED PAL_SETGEN NEAR ITS 16-BIT CEILING, to reproduce the
# rig's 57-minute black screen in seconds. See m_main.c's note at the seed. NEVER SHIP.
ifdef PORTGENSEED
SHCCFLAGS += -DPORT_GENSEED=1
endif
ifdef PORTTICKFREE
SHCCFLAGS += -DPORT_TICKFREE=$(PORTTICKFREE)
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
# `make ... PORTRESEED=N` = cold boot, then take the arcade's captured state at tick
# N. Needs PORTBOOT and a capture (L2DIR). See sh_src/port_l2.c for why neither half
# works alone: the boot draws but hangs, the seed advances but renders black.
ifdef PORTRESEED
SHCCFLAGS += -DPORT_RESEED=$(PORTRESEED)
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
# Renderer-only pixel fixtures, never a gameplay parity probe.
ifdef R60FIXTURECLEAR
ifndef PORTSPRITEFIXTUREPLANES
$(error R60FIXTURECLEAR requires PORTSPRITEFIXTUREPLANES)
endif
SHCCFLAGS += -DR60_FIXTURECLEAR
endif
ifdef R60PENID
SHCCFLAGS += -DR60_PENID
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
SHCCFLAGS += -DPORT_FORM=$(PORTFORM) $(if $(PORTFORMTICK),-DPORT_FORM_TICK=$(PORTFORMTICK))   # PORTFORMTICK=N: not before game tick N (GD_FORM_TICK's twin; raised in the intro the cut never switches pages)
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
# `make ... PORTSEED=N PORTWATCH=0xFFFA80 PORTTRACE=1` = watch ONE 68K cell through
# that seeded frame and publish every function whose span changed it, innermost
# first. Read with tools/port_watch_read.py.
ifdef PORTWATCH
SHCCFLAGS += -DL2M_WATCH=$(PORTWATCH)u
endif
# `make ... PORTREGS=1` = give each object handler the ARCADE's register file at the
# dispatcher's call site (tools/cport_dispregs.py), instead of A6 alone. A
# DIAGNOSTIC: it proves whether the missing registers are what stops objects being
# hidden, and bounds the payoff before deriving the register chain. Never a ship.
ifdef PORTREGS
SHCCFLAGS += -DPORT_DISPREGS
endif

# --- removed 2026-11-15 by hand: the option blocks that did more than add a define ---
# TEXTCAP_SLAVE is in the bundle but must be switchable: LOOP28 96 measured
# the master waiting 31.8 scanlines for the slave to PICK UP a capture that
# then takes 4.3. `make ... TEXTCAPMASTER=1` runs it inline instead.
ifndef TEXTCAPMASTER
SHCCFLAGS += -DTEXTCAP_SLAVE
endif
# `make ... BOOTSHSTAGE=1` = master SH-2 boot stages: COMM12 tint on the MD backdrop
# (before the 32X display is on), then 32X CRAM 0 colours; exceptions = magenta + halt.
ifdef BOOTSHSTAGE
SHCCFLAGS += -DBOOT_SHSTAGE
MDCCFLAGS += -DBOOT_SHSTAGE
SHASFLAGS += --defsym BOOT_SHSTAGE=1
endif
# `make line CARTREADAT=n` = 64 cart-window reads per vint at position n
# (18 = top of md_consume, 17 = partb_hook after the game's IRQ4); see
# md_main.c cart_read_burst. VERIFY .build_flags carries CART_READ_AT.
ifdef CARTREADAT
MDCCFLAGS += -DCART_READ_AT=$(CARTREADAT)
MDASFLAGS += -Wa,--defsym,PARTB_HOOK=1
endif
# `make ... BOOTGAMERATE=1` = game frames per 64 vints, read exactly off
# the game's scene timer and flooded as a number. 64 = 60 Hz.
ifdef BOOTGAMERATE
MDCCFLAGS += -DBOOT_VALUE -DBOOT_GAMERATE
MDASFLAGS += -Wa,--defsym,BOOT_GAMERATE=1
endif
MDCCFLAGS += -DTAIL_CENSUS
# `PAGEPROBE=1` = the gate: both CPUs page slot 7 to the marker pages and read them back;
# a wrong read hangs the boot (black screen). PAGEPROBE=2 = the control, must hang.
# PAGEPROBE=5 = the colour verdict (68K red, SH-2 green, blue base; md_bank.c). PAGEPROBE=13 =
# the same verdict over SIX marker pages 8..13 (a 7 MB rom): the SH-2 reads pages 8, 9, 12
# and 13 through slot 7. Proves the top page of a per-level art layout (2026-11-04).
ifdef PAGEPROBE
SHCCFLAGS += -DPAGE_PROBE=$(PAGEPROBE)
MDCCFLAGS += -DPAGE_PROBE=$(PAGEPROBE)
SHASFLAGS += --defsym PAGE_PROBE=$(PAGEPROBE)
endif
# `R60OPMLANE8=1` (with R60OPMLANE=1) = the lane at half the mixer rate, each sample held for two
# outputs: half the slave's cost (lane6 underran half its sides on ares once the music started,
# 2026-10-31); the rows on the 8 kHz clock. A fidelity step down (nothing above 4 kHz), a step
# back up as the picture gets cheaper.
ifdef R60OPMLANE8
SHCCFLAGS += -DOPM_LANE_HOLD=2
OPM_LANE_RATE = 8001.15
endif
# `R60PWM8=1` (with R60OPMLANE=1, not R60OPMLANE8) = the PWM itself at 8 kHz (23011361 / 2877 =
# 7998.4 Hz): the lane renders one sample per output, 4 x 512-sample sides = 256 ms of lead in
# the same 4 KB; the trim in 8 kHz samples. A probe for the ears: the carrier sits at 8 kHz.
ifdef R60PWM8
SHCCFLAGS += -DR60_PWM8 -DOPM_LANE_TRIM=360 -DOPM_LANE_FRAMES=20   # the countdown must outlast the lead: 256 ms of sides, 133 ms of count landed 91 ms late (lane8)
MDCCFLAGS += -DOPM_LANE_FRAMES=20
OPM_LANE_RATE = 7998.4
endif
# `R60ZOOMPACK=1` = compact the same scaled-pose selection, then expand admitted
# poses into the existing 4KiB SPRPREP scratch before the established copy loop.
# Default OFF. Reclaims unscaled-art space; unpacking cost must be measured.
ifdef R60ZOOMPACK
R60ZOOMBAKE = 1
ifndef R60SPRPREP
$(error R60ZOOMPACK requires R60SPRPREP=1 for the existing scratch arena)
endif
SHCCFLAGS += -DR60_ZOOMPACK
endif
ifdef R60ZOOMBAKE
ifndef R60BAKEART
$(error R60ZOOMBAKE requires R60BAKEART=1)
endif
endif
# Optional scaled-pose atlas reserves the prefix of existing window 2. No new
# mapper state or relaxed link guards. Remaining unscaled art is re-ranked.
# Row extent extension is private to this flag; absent/clipped poses fall back.
# R60ZOOMBAKESTATS=1 adds atlas request/copy/row/clip counters for fixtures;
# leave off for timing probes (the hot-code guard is deliberately unchanged).
ifdef R60ZOOMBAKE
SHCCFLAGS += -DR60_ZOOMBAKE $(if $(R60ZOOMBAKESTATS),-DR60_ZOOMBAKE_STATS)
SHASFLAGS += --defsym R60_ZOOMBAKE=1
SHASFLAGS += --defsym R60_RAW_ZOOM_EXTENTS=1
ZOOMBAKE_CORPUS ?= $(ARTBAKE_CORPUS)
ZOOMBAKE_RUNS = $(foreach n,0 1 2 3 4,r$(n)=$(ZOOMBAKE_CORPUS)/round$(n)) tape=$(ZOOMBAKE_CORPUS)/tape
ZOOMBAKE_INPUTS = $(foreach n,0 1 2 3 4,$(ZOOMBAKE_CORPUS)/round$(n)/spr.bin) $(ZOOMBAKE_CORPUS)/tape/spr.bin
ARTBAKE_WIN2 = $(shell python3 -c "import os;n=os.path.getsize('sh_src/zoom_bake.bin');print('0x%08X:0x%X'%(0x022B1000+n,0xF000-n))")
endif
ifdef R60EVENTMETER
ifneq ($(strip $(R60WORKSETSTATS)),1)
$(error R60WORKSET with R60EVENTMETER requires R60WORKSETSTATS=1)
endif
endif
ifneq ($(strip $(R60SPRITEBLOCKS)$(R60SPRITEBANDS)),)
$(error R60ZOOMRUN conflicts with the alternate block/band renderers)
endif
# R60TEMPORAL=1: append a small, exact-state transition packet dictionary to
# the working-set page.  Packets are admitted by the runtime only when both
# complete drawing tables match a manifest entry; every miss retains the
# ordinary scalar renderer.  The compact pose manifest makes room in the same
# bounded 512 KiB page and is therefore part of this experiment, not a change
# to the accepted working-set line.
ifdef R60TEMPORAL
ifneq ($(strip $(R60TEMPORAL)),1)
$(error R60TEMPORAL must be 1)
endif
ifndef R60WORKSET
$(error R60TEMPORAL requires R60WORKSET=1)
endif
ifndef R60ZOOMRUN
$(error R60TEMPORAL requires R60ZOOMRUN=1)
endif
ifneq ($(GAME),altbeast)
$(error R60TEMPORAL currently has an Altered Beast exact-state manifest only)
endif
R60TEMPORALMANIFEST ?= games/altbeast/graphics/smoke-transitions-4.json
R60TEMPORALPOSES ?= games/altbeast/graphics/neff-smoke-unscaled-1.json
R60_WORKSET_POSES_EFFECTIVE := $(R60TEMPORALPOSES)
R60_TRANSITION_DEPS = sh_src/r60_transitions.bin
R60_TRANSITION_BAKE_ARG = --transition-packets sh_src/r60_transitions.bin
SHCCFLAGS += -DR60_TEMPORAL=1
endif
# Dense painter-order sprite ranges composed into 32x32 physical blocks.  The
# mapped closure is mandatory; the live adapter itself remains opt-in.
ifdef R60SPRITEBLOCKS
ifneq ($(strip $(R60SPRITEBLOCKS)),1)
$(error R60SPRITEBLOCKS must be 1)
endif
ifndef R60WORKSETCODE
$(error R60SPRITEBLOCKS requires R60WORKSETCODE=8192)
endif
ifneq ($(strip $(R60WORKSETCODE)),8192)
$(error R60SPRITEBLOCKS requires R60WORKSETCODE=8192)
endif
R60WORKSETCACHE ?= 1
$(foreach f,R60WORKSET R60SPRPREP R60SPRPREP2 R60PRIO R60PUNCH2 R60PUNCHTHRU R60KEEPSPR R60ROWCLR R60RECTCLR R60PALMAP,$(if $($(f)),,$(error R60SPRITEBLOCKS requires $(f)=1)))
ifneq ($(strip $(R60MBAND)$(R60MASKBLOCK)$(R60ARTCACHE)$(R60PRIOMASK)$(R60PRIOROW)$(R60BAKEARTCHECK)),)
$(error R60SPRITEBLOCKS conflicts with alternate/check renderer owners)
endif
SHCCFLAGS += -DR60_SPRITE_BLOCKS=1
endif
# Eight-row painter-order compositor.  Kept independent from the rejected
# 32x32 experiment so either implementation can be measured from the same line.
ifdef R60SPRITEBANDS
ifneq ($(strip $(R60SPRITEBANDS)),1)
$(error R60SPRITEBANDS must be 1)
endif
ifndef R60WORKSETCODE
$(error R60SPRITEBANDS requires R60WORKSETCODE=8192)
endif
ifneq ($(strip $(R60WORKSETCODE)),8192)
$(error R60SPRITEBANDS requires R60WORKSETCODE=8192)
endif
ifdef R60SPRITEBLOCKS
$(error select R60SPRITEBANDS or R60SPRITEBLOCKS, not both)
endif
R60WORKSETCACHE ?= 1
$(foreach f,R60WORKSET R60SPRPREP R60SPRPREP2 R60PRIO R60PUNCH2 R60PUNCHTHRU R60KEEPSPR R60ROWCLR R60RECTCLR R60PALMAP,$(if $($(f)),,$(error R60SPRITEBANDS requires $(f)=1)))
ifneq ($(strip $(R60MBAND)$(R60MASKBLOCK)$(R60ARTCACHE)$(R60PRIOMASK)$(R60PRIOROW)$(R60BAKEARTCHECK)),)
$(error R60SPRITEBANDS conflicts with alternate/check renderer owners)
endif
SHCCFLAGS += -DR60_SPRITE_BANDS=1
endif
# Shared clipped loop placement:1 RAM overlay,2 matched resident-ROM control.
# Both copy the linked body at boot; absence retains the four-loop CLIP path.
ifdef R60WORKSETCLIPRAM
ifndef R60WORKSETCLIP
$(error R60WORKSETCLIPRAM requires R60WORKSETCLIP=1)
endif
ifneq ($(words $(R60WORKSETCLIPRAM)),1)
$(error R60WORKSETCLIPRAM must be 1 or 2)
endif
ifeq ($(filter $(R60WORKSETCLIPRAM),1 2),)
$(error R60WORKSETCLIPRAM must be 1 or 2)
endif
ifneq ($(strip $(R60MASTERCHUNK)$(R60BAKEARTCHECK)$(R60PLOT5CHECK)$(R60ZOOMCHECK)$(R60PUNCH2CHECK)$(R60MASKBLOCK)$(R60ARTCACHE)$(R60MBAND)$(R60PRIOMASK)$(R60PRIOROW)$(R60ZOOMBAKE)$(R60ZOOMPACK)$(R60ZOOMWORDS)$(R60SOLID)),)
$(error R60WORKSETCLIPRAM conflicts with overlapping/check/alternate renderer owners)
endif
SHCCFLAGS += -DR60_WORKSET_CLIPRAM=$(R60WORKSETCLIPRAM)
R60_CLIP_LD_ARGS = --workset-clipram=$(R60WORKSETCLIPRAM)
endif
# R60MCLEAD=8/16/32: controlled four-row request lookahead; default32.
ifdef R60MCLEAD
ifndef R60MASTERCHUNK
$(error R60MCLEAD requires R60MASTERCHUNK)
endif
endif
# R60MCQUANTA=1 or8: bounded four-sourceword steps per idle-master pump.
ifdef R60MCQUANTA
ifndef R60MASTERCHUNK
$(error R60MCQUANTA requires R60MASTERCHUNK)
endif
endif
# R60MCSTORESTATS=0 declares destination-store totals unavailable and removes
# prepared-packet recounts. Default1 retains exact counts; MASTER-only.
ifdef R60MCSTORESTATS
ifndef R60MASTERCHUNK
$(error R60MCSTORESTATS requires R60MASTERCHUNK)
endif
endif
# R60MASTERCHUNK=1: optional idle-master raw four-row preparation; =2: idle
# worker control with the same arenas/consumer request/fallback hooks. Both
# preserve SPRPREP4KiB and use only its spare1KiB. Default OFF, not a preset.
ifdef R60MASTERCHUNK
ifndef CARTSSF
$(error R60MASTERCHUNK uses guarded resident CARTSSF code gaps and requires CARTSSF=1)
endif
ifeq ($(filter $(R60MASTERCHUNK),1 2),)
$(error R60MASTERCHUNK must be 1 or 2)
endif
$(foreach f,R60SPRPREP R60BAKEART R60PLOT5 R60ZOOMFAST R60ROWFAST R60SLAVEWIN R60KEEPSPR R60PALMAP,$(if $($(f)),,$(error R60MASTERCHUNK requires $(f)=1)))
ifneq ($(strip $(R60BAKEARTCHECK)$(R60PLOT5CHECK)$(R60ZOOMCHECK)$(R60PUNCH2CHECK)$(R60MASKBLOCK)$(R60ARTCACHE)$(R60MBAND)$(R60PRIOMASK)$(R60PRIOROW)$(R60ZOOMBAKE)$(R60ZOOMPACK)$(R60ZOOMWORDS)$(R60SOLID)),)
$(error R60MASTERCHUNK conflicts with overlapping/check/alternate-renderer experiments)
endif
SHCCFLAGS += -DR60_MASTERCHUNK=$(R60MASTERCHUNK)
R60MCLEAD ?= 32
ifneq ($(words $(R60MCLEAD)),1)
$(error R60MCLEAD must be 8, 16 or 32)
endif
ifeq ($(filter $(R60MCLEAD),8 16 32),)
$(error R60MCLEAD must be 8, 16 or 32)
endif
SHCCFLAGS += -DR60_MC_LOOKAHEAD=$(R60MCLEAD)
R60MCQUANTA ?= 1
ifeq ($(filter $(R60MCQUANTA),1 8),)
$(error R60MCQUANTA must be 1 or 8)
endif
SHCCFLAGS += -DR60_MC_QUANTA=$(R60MCQUANTA)
R60MCSTORESTATS ?= 1
ifeq ($(filter $(R60MCSTORESTATS),0 1),)
$(error R60MCSTORESTATS must be 0 or 1)
endif
ifeq ($(R60MCSTORESTATS),0)
ifdef R60BKBENCH
$(error R60MCSTORESTATS=0 is incompatible with R60BKBENCH)
endif
endif
SHCCFLAGS += -DR60_MC_STORESTATS=$(R60MCSTORESTATS)
SHASFLAGS += --defsym R60_RAW_ZOOM_EXTENTS=1
endif
# `R60EVENTMETER=1` = durable first entrance/death cohorts; =2 zoom-cloud
# handlers only. Probe-only, paired snapshot tags and separate CPU writers.
# Uses30E00..31000 beside RSX, no BSS. FRT work remains explicitly uncertain;
# complete native/VInt/ack event clocks do not depend on sparse screenshots.
ifdef R60EVENTMETER
ifeq ($(filter $(R60EVENTMETER),1 2),)
$(error R60EVENTMETER must be 1 or 2)
endif
ifdef R60SOLID
$(error R60EVENTMETER requires the normal renderer, not R60SOLID)
endif
R60RIGSPLIT ?= 4
ifneq ($(R60RIGSPLIT),4)
$(error R60EVENTMETER requires R60RIGSPLIT=4)
endif
ifdef R60NOBARS
$(error R60EVENTMETER requires R60NOBARS= to expose its telemetry)
endif
ifndef R60SLAVEWIN
$(error R60EVENTMETER requires R60SLAVEWIN=1)
endif
SHCCFLAGS += -DR60_EVENTMETER=$(R60EVENTMETER)
SHLDFLAGS += -Wl,--defsym,__r60_event_probe=1
endif
# `make POSTLATE=1` = 2026-09-06: post+push after the game's IRQ4 (no FM
# gate spins in the game's vint upload; the flip becomes the body's
# deferred flip). Measured for the 60Hz demo.
ifdef POSTLATE
MDCCFLAGS += -DPOST_LATE
MDASFLAGS += -Wa,--defsym,POST_LATE=1
endif
# `DIRTYROWVERIFY=1` adds stage A's check: nothing is skipped, but every
# row the marks claim is zero gets read back and disagreements counted.
# Get that to 0 — and falsify it by removing one RL_MARK — before
# enabling any skipping. Read with tools/dirtyrow_probe.lua.
ifdef DIRTYROWVERIFY
SHCCFLAGS += -DDIRTY_ROW -DDIRTY_ROW_VERIFY
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
SHCCFLAGS += -DEARLY_REC
MDCCFLAGS += -DEARLY_REC
MDCCFLAGS += -DWRAM_CODE
ifdef MDQUIET
MDCCFLAGS += -DMD_QUIET
ifeq ($(MDQUIET),1)
MDASFLAGS += -Wa,--defsym,MD_QUIET_IRQ4=1
endif
endif
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
ifdef R60TEMPORAL
sh_src/r60_transitions.bin: tools/pack_sprite_transitions.py tools/bake_zoom.py tools/s16art.py \
		$(R60TEMPORALMANIFEST) $(R60WORKSETATLAS) sh_src/sprites.bin $(FLAGSTAMP)
	python3 tools/pack_sprite_transitions.py --manifest $(R60TEMPORALMANIFEST) \
		--zoom-atlas $(R60WORKSETATLAS) --sprites sh_src/sprites.bin \
		--out $@ --report sh_src/r60_transitions.json
sh_src/r60_transitions.json: sh_src/r60_transitions.bin
	@:
endif
ifdef R60ZOOMBAKE
sh_src/zoom_bake.bin: tools/bake_zoom.py tools/bake_art.py tools/s16art.py sh_src/sprites.bin $(ZOOMBAKE_INPUTS) $(FLAGSTAMP)
	python3 tools/bake_zoom.py --rom sh_src/sprites.bin --out $@ $(if $(R60ZOOMPACK),--packed) $(ZOOMBAKE_RUNS)
sh_src/artbake_0.bin: sh_src/zoom_bake.bin
sh_src/artbake_data.o: sh_src/zoom_bake.bin
endif
ifdef PORTSPRITEFIXTUREPLANES
ifndef PORTSPRITEFIXTURE
$(error PORTSPRITEFIXTUREPLANES requires PORTSPRITEFIXTURE)
endif
endif
ifdef PORTSPRITEFIXTURE
SHCCFLAGS += -DPORT_SPRITEFIXTURE -DPORT_SPRITEFIXTURE_AT=$(if $(PORTSPRITEFIXTUREAT),$(PORTSPRITEFIXTUREAT),0x710)
# Optional native plane snapshot makes foreground/scroll independent of stop timing.
SPRITE_FIXTURE_PLANES := $(if $(PORTSPRITEFIXTUREPLANES),$(PORTSPRITEFIXTUREPLANES)_tilemap.bin $(PORTSPRITEFIXTUREPLANES)_text.bin)
SPRITE_FIXTURE_ID := $(PORTSPRITEFIXTURE) $(PORTSPRITEFIXTUREBANK) $(PORTSPRITEFIXTUREPLANES) $(shell shasum $(PORTSPRITEFIXTURE) $(SPRITE_FIXTURE_PLANES) 2>/dev/null | shasum | cut -c1-16)
$(shell [ "$$(cat sh_src/sprite_fixture.id 2>/dev/null)" = "$(SPRITE_FIXTURE_ID)" ] || printf '%s' "$(SPRITE_FIXTURE_ID)" > sh_src/sprite_fixture.id)
sh_src/port_l2.o: sh_src/sprite_fixture.h
sh_src/sprite_fixture.h: tools/sprite_fixture.py sh_src/sprite_fixture.id $(PORTSPRITEFIXTURE) $(SPRITE_FIXTURE_PLANES)
	python3 tools/sprite_fixture.py $(PORTSPRITEFIXTURE) $@ $(if $(PORTSPRITEFIXTUREBANK),--bank $(PORTSPRITEFIXTUREBANK)) $(PORTSPRITEFIXTUREPLANES)
endif

# RIGSPAN (removed 2026-11-16): the span probe measured the patched game on the 68K;
# the port does not run it there.
# RIGSPAN=18 (2026-09-24): the sampler's class histogram over the barcode; needs the 16-line sampler.
ifeq ($(RIGSPAN),18)
PCSAMP = 1
PCSAMPN ?= 15
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

# The rig barcode and the options that fed it (removed 2026-11-16): it no longer fits
# in the 68K work RAM beside the port (806 bytes free under the boot stack guard).
# `make line RIGBARCODE=1` = 80-bit name-table barcode on plane A rows 24-27
# (docs/design/RIG-READOUT.md); decode captures with tools/rig_barcode.py
ifdef RIGBARCODE
SHCCFLAGS += -DRIG_BARCODE
MDCCFLAGS += -DRIG_BARCODE
endif
ifdef BCROWS
MDCCFLAGS += -DBC_ROWS=$(BCROWS)
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
# RIGRATE=1 (2026-09-26): THE WHOLE-RUN CADENCE RATE on the barcode. byte 5 =
# game frames per vint x 255 accumulated since boot (255 = 60 Hz, 128 = every
# other vint), byte 6 = vints/256 so the reader can see it has settled. Every
# other rig speed reading is a per-step accumulator sampled once or twice a
# launch, which is why the ~15-line per-launch spread keeps swamping the thing
# being measured; this one CONVERGES, so four launches give four numbers.
# Needs RIGBARCODE=1. Overrides bytes 5/6.
ifdef RIGRATE
MDCCFLAGS += -DRIG_RATE
SHCCFLAGS += -DRIG_RATE
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
# `make ... PORTBODY=n` = run only the FIRST n calls of the arcade's measured frame
# body (FULL-PORT.md 32). The full fifteen regressed the seed frame 9 -> 97 wrong, so
# the sequence is adopted a call at a time against the gate.
ifdef PORTBODY
SHCCFLAGS += -DL2M_BODY_N=$(PORTBODY)
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
ifdef FBXPORT
MDCCFLAGS += -DFB_XPORT
SHCCFLAGS += -DFB_XPORT
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
ifdef R60PLOT3
SHCCFLAGS += -DR60_PLOT3
endif
ifdef R60NOBARS
SHCCFLAGS += -DR60_NOBARS
endif
ifdef R60SPLITFAST
SHCCFLAGS += -DR60_SPLITFAST   # r60_split_row's histogram as a prefix sum (same split; ~4.8 master lines a vint less)
endif
ifdef R60ROWFAST
SHCCFLAGS += -DR60_ROWFAST_OPT   # the hoisted per-sprite row loop (2026-10-23); see R60_ROWFAST in r60_main.c
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
# `R60BLANKFILL=1` = a BG cell whose art cannot ship this window names a SOLID tile of its set's
# colour 0 (slots 1016-1023, one per line/pen) instead of the blank over the black backdrop; a
# stale cell is kept only when it is the same art. The title cut's black blocks. 2026-10-17.
ifdef R60BLANKFILL
SHCCFLAGS += -DR60_BLANKFILL
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
# `PORTSTARTCOIN=1` = START also coins while the credits cannot pay for a start (needs R60LIVEPAD). 2026-10-26, for pads
# the Miyoo / Analogue Pocket cores hand over without X/Y/Z/MODE. Edge-latched in sh_src/port_l2.c l2m_io_live.
ifdef PORTSTARTCOIN
SHCCFLAGS += -DPORT_STARTCOIN
endif
# `R60PTONE=1` (2026-11-01) = the sprite plotter's pen table keeps ONE flip (8 KB, not 16): the
# flipped entry was the byte-swapped twin, so the flipped read swaps (swap.b) instead.
# Gate: attract screenshots byte-identical to the same build without the flag (ares). The
# 8 KB is for the synth lane's hot tables (sh_src/r60/r60_main.c R60_PTONE).
ifdef R60PTONE
SHCCFLAGS += -DR60_PTONE
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
ifdef R60SPRPREP
SHCCFLAGS += -DR60_SPRPREP -DR60_PRIOCACHE512
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
# `make ... CAT1MD=1` = pipelining arc C1 step 1: FG cat-1 cells are emitted
# on MD plane A with the priority bit instead of blanked. Pixel-neutral by
# construction while the FB keeps composing cat1; step 2 restricts the FB
# cat1 pass to SH-2 sprite rows/strips (the 0.44v/gen slave lever).
ifdef CAT1MD
SHCCFLAGS += -DCAT1_MD
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
# TILECLASS=1 = LOOP19 endgame: static fade-stable tile palette classes
# (sh_src/tile_classes.h, generated by tools/palpack_tiles.py --emit from
# the harvest corpus). Tiles collapse to 11 fixed groups, sprites get a
# fixed 8-pair zone — the S11 over-subscription is gone by construction.
# Corpus covers title + round 1; regenerate the table before later rounds.
ifdef TILECLASS
SHCCFLAGS += -DTILE_CLASS
endif
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
# `make ... MDBATCH=N` = LOOP29 199. Tiles shipped to MD VRAM per vint
# while the display is ON. 12 is the R60 default: 40 was calibrated for
# 30Hz windows and overruns vblank at 60, which is the load-in tear and
# the purple band. But 12 is also why cells pop in black -- a scene change
# needs the whole 1120-tile working set, which is 93 vints at 12. The
# blanked path already ships 40 (m_main.c:13268). Mike's eye ranks this.
ifdef MDBATCH
SHCCFLAGS += -DMD_BATCH_N=$(MDBATCH)
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
ifdef PORTFFBM
SHCCFLAGS += -DPORT_FFBM=$(PORTFFBM)
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
ifdef PORTNOPALPROBE
SHCCFLAGS += -DPORT_NOPALPROBE   # the palette-collapse detector (port_pal_probe, 2026-10-04) off the tick: two 128-set uncached scans + a 256-byte WRAM scan a tick, ~5.7 master lines a vint (ares profile, fight 600-700); it only writes port_watch_out[180..188]
endif
