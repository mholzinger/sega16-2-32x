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
