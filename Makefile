# Altered Beast System 16B -> 32X. Build layout from 32x-builder (marsdev).
MARSDEV ?= ${HOME}/src/marsdev/mars
MDBIN    = $(MARSDEV)/m68k-elf/bin
SHBIN    = $(MARSDEV)/sh-elf/bin

ROMDIR  := rom
TARGET  ?= $(ROMDIR)/s16
MDTARGET = $(ROMDIR)/md_start

MDCC   = $(MDBIN)/m68k-elf-gcc
MDNM   = $(MDBIN)/m68k-elf-nm
MDOBJC = $(MDBIN)/m68k-elf-objcopy
SHCC   = $(SHBIN)/sh-elf-gcc
SHAS   = $(SHBIN)/sh-elf-as
SHNM   = $(SHBIN)/sh-elf-nm
SHOBJC = $(SHBIN)/sh-elf-objcopy

MDCC_VER := $(shell $(MDCC) -dumpversion)
SHCC_VER := $(shell $(SHCC) -dumpversion)

MDPLUGIN = $(MARSDEV)/m68k-elf/libexec/gcc/m68k-elf/$(MDCC_VER)/liblto_plugin.so
SHPLUGIN = $(MARSDEV)/sh-elf/libexec/gcc/sh-elf/$(SHCC_VER)/liblto_plugin.so

MDINCS   = -I$(MARSDEV)/m68k-elf/lib/gcc/m68k-elf/$(MDCC_VER)/include
SHINCS   = -I$(MARSDEV)/sh-elf/lib/gcc/sh-elf/$(SHCC_VER)/include
MDLIBS   = -L$(MARSDEV)/m68k-elf/lib/gcc/m68k-elf/$(MDCC_VER) -lgcc
SHLIBS   = -L$(MARSDEV)/sh-elf/lib/gcc/sh-elf/$(SHCC_VER) -lgcc

MDCCFLAGS  = -m68000 -mshort -Wall -Wextra -std=c99 -ffreestanding
SHCCFLAGS  = -m2 -mb -Wall -Wextra -std=c99 -ffreestanding
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
SHCCFLAGS += -DMD_BG -DMD_BG_FG0
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
# Candidate for BOTH flavors if gates hold (shipping's truth measured
# clean, but the bound is cheap insurance there too).
ifdef PGROTOR
SHCCFLAGS += -DPG_ROTOR
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
MDLDFLAGS  = -T md_src/md.ld -nostdlib
SHLDFLAGS  = -T sh_src/mars.ld -nostdlib

MDEXTRA =
SHEXTRA =

MDOBJS  = $(patsubst %.s,%.o,$(wildcard md_src/*.s))
MDOBJS += $(patsubst %.c,%.o,$(wildcard md_src/*.c))
SHOBJS  = $(patsubst %.s,%.o,$(wildcard sh_src/*.s))
SHOBJS += $(patsubst %.c,%.o,$(wildcard sh_src/*.c))

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

$(MDOBJS) $(SHOBJS): $(FLAGSTAMP)

# build stamp header: git short hash as u32 -> DIAG[18] at boot, so every
# savestate self-identifies its commit. Regenerated when HEAD changes.
BUILD_HASH := $(shell git rev-parse --short=8 HEAD 2>/dev/null || echo 0)
sh_src/buildstamp.h: .git/HEAD $(wildcard .git/refs/heads/*)
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
	@$(SHOBJC) -O binary $< temp.32x
	@dd if=temp.32x of=$@ bs=8192 conv=sync 2>/dev/null
	@rm -f temp.32x
	@# BUILD STAMP at file offset 0x3C0 (unused header pad): git hash +
	@# epoch + PRESSURE flag. Every savestate self-identifies its build
	@# (tools/build_id.py) — no more provenance arguments.
	@python3 tools/build_id.py stamp $@ $(if $(PRESSURE),PRESSURE,$(if $(SPROBE),SPROBE,$(if $(TAILPROBE),TAILPROBE,$(if $(WINSPLIT),WINSPLIT,normal))))
	@python3 tools/build_id.py show $@

$(TARGET).elf: $(SHOBJS) | $(ROMDIR)
	$(SHCC) $(SHLDFLAGS) $^ -o $@ $(SHLIBS)

$(ROMDIR):
	@mkdir -p $(ROMDIR)

# Patched arcade game body + boot RAM copy, .incbin'd by mars_start.s
md_src/game_body.bin md_src/boot_copy.bin md_src/game_high.bin &: roms/altbeast/prog68k.bin tools/patch_game.py
	@python3 tools/patch_game.py
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
sh_src/tiles.bin: tools/gen_tiles.py roms/altbeast/opr-11674.a14
	@python3 tools/gen_tiles.py
sh_src/tiles_data.o: sh_src/tiles.bin

# Arcade sprite data: 16-bit-BE interleave, .incbin'd by sprites_data.s
sh_src/sprites.bin: tools/gen_sprites.py roms/altbeast/epr-11677.b1
	@python3 tools/gen_sprites.py
sh_src/sprites_data.o: sh_src/sprites.bin

sh_src/%.o: sh_src/%.s
	@echo "SHAS $<"
	@$(SHAS) $(SHASFLAGS) $< -o $@

sh_src/%.o: sh_src/%.c
	@echo "SHCC $<"
	@$(SHCC) $(SHCCFLAGS) $(SHEXTRA) $(SHINCS) -MMD -MP -c $< -o $@

-include $(MDOBJS:.o=.d)
-include $(SHOBJS:.o=.d)

clean:
	rm -f $(MDOBJS) $(SHOBJS) $(MDOBJS:.o=.d) $(SHOBJS:.o=.d)
	rm -f $(MDTARGET).bin $(MDTARGET).elf $(MDTARGET).lst
	rm -f $(TARGET).32x $(TARGET).elf $(TARGET).lst
	rm -f sh_src/md_start.bin sh_src/tiles.bin sh_src/sprites.bin
	rm -f $(FLAGSTAMP)
