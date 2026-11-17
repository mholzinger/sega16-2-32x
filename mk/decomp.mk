# The decompile-to-C pipeline: Ghidra output -> tools/cport_prep.py -> the port's
# generated C (sh_src/portfull). Kept after every other rule so none of its explicit
# rules becomes the default goal.
# Part of the top-level Makefile; included, never run on its own.
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
CPORT_ROOTS_BASE = $(GAME_CPORT_ROOTS)
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
CPORT_ROOTS_SET = $(CPORT_ROOTS_BASE),frame_rotated$(if $(PORTBOOT),$(COMMA)$(GAME_CPORT_BOOT_ROOT))
CPORT_STUB_WAIT = $(GAME_CPORT_STUBS)
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

ifdef PORTTILEDIRTY
SHCCFLAGS += -DPORT_TILEDIRTY
# AND THE PORT UNIT, which tools/cport_build.sh compiles with its OWN flag line and not
# SHCCFLAGS: the first build of this defined the macro everywhere except the only
# translation unit that calls it, and measured zero marks against 90 hashed changes.
# The flag is the RENDERER's half and is inert until the port marks the page at its store
# sites; see port_tile_dirty in sh_src/m_main.c for why the address macros cannot.
PORT_TRACE_CF += -DPORT_TILEDIRTY
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
CPORT_CLAMP ?= $(GAME_CPORT_CLAMP)
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
CPORT_HI ?= $(GAME_CPORT_HI)

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
CPORT_AREG ?= $(GAME_CPORT_AREG)

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
CPORT_DREG ?= $(GAME_CPORT_DREG)
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
CPORT_ANYNORM ?= $(GAME_CPORT_ANYNORM)

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
CPORT_WIDTH ?= $(GAME_CPORT_WIDTH)

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
CPORT_PARAMNORM ?= $(GAME_CPORT_PARAMNORM)
# CPORT_AFTER: hooks called right after a marked call site, MARKER=hook (the marker is the
# game's breadcrumb _DAT_fffffefc = jsr + 6; tools/cport_prep.py). 574 = the boot's
# set_level_palettes (FUN_00000500), where altbeastj adds its warning page: the hook
# returns at once unless the region byte is 1 (games/altbeast/glue.c port_region_boot).
CPORT_AFTER ?= $(GAME_CPORT_AFTER)
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
