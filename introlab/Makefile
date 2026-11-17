# introlab: a standalone 32X test rom for the Sega logo intro and the 3D cabinet showcase.
# Built on the sndtest skeleton and deliberately separate from the port: nothing here
# reads or writes sh_src/, md_src/ or rom/.
#
#   make -C introlab            -> introlab/rom/introlab.32x
#   make -C introlab shot [INPUT=pad.csv] [SHOTS="400 430"]
#                               -> build, run FRAMES (default 300) in headless ares, write
#                                  rom/introlab.png and print frames flipped per vblank
#   make -C introlab cabinet TITLE=altbeast
#                               -> art/altbeast/: low-poly mesh, baked textures, the
#                                  header the rom compiles in, and a host preview
#   make -C introlab reference TITLE=altbeast [AZ=30]
#                               -> art/altbeast/reference.png, the detailed model
#   make -C introlab clean
MARSDEV ?= $(HOME)/src/marsdev/mars
ARES    ?= $(HOME)/src/ares-debug/build_macos/headless-ui/Release/ares-headless
MDBIN = $(MARSDEV)/m68k-elf/bin
SHBIN = $(MARSDEV)/sh-elf/bin
MDCC = $(MDBIN)/m68k-elf-gcc
MDOBJC = $(MDBIN)/m68k-elf-objcopy
SHCC = $(SHBIN)/sh-elf-gcc
SHAS = $(SHBIN)/sh-elf-as
SHNM = $(SHBIN)/sh-elf-nm
SHOBJC = $(SHBIN)/sh-elf-objcopy
MDCC_VER := $(shell $(MDCC) -dumpversion)
SHCC_VER := $(shell $(SHCC) -dumpversion)
MDLIBS = -L$(MARSDEV)/m68k-elf/lib/gcc/m68k-elf/$(MDCC_VER) -lgcc
SHLIBS = -L$(MARSDEV)/sh-elf/lib/gcc/sh-elf/$(SHCC_VER) -lgcc
MDCCFLAGS = -m68000 -mshort -Wall -Wextra -Werror=return-type -std=c99 -ffreestanding -O2 -fomit-frame-pointer
SHCCFLAGS = -m2 -mb -Wall -Wextra -Werror=return-type -std=c99 -ffreestanding -O2 -fomit-frame-pointer
all:

# A/B switches for measuring, e.g. `make -C introlab shot ONECPU=1 SWDIV=1`:
#   ONECPU=1     the primary draws the whole screen, the secondary idles
#   SWDIV=1      software divides (libgcc) instead of the SH-2's hardware divider
#   NORAMCODE=1  the rasteriser runs from cart ROM instead of SDRAM
#   NOHUD=1      no frame-rate numbers on the MD text plane
#   TEXROM=1     textures are read from the cart instead of an SDRAM copy
#   SMOKE=1      the smoke layer on MD plane A, between the wall and the cabinet (lab only)
SHCCFLAGS += $(foreach f,ONECPU SWDIV NORAMCODE NOHUD TEXROM SMOKE,$(if $($(f)),-D$(f)))
SHCCFLAGS += $(EXTRA)
FLAGSTAMP := $(strip $(foreach f,ONECPU SWDIV NORAMCODE NOHUD TEXROM SMOKE TITLE EXTRA PORTHUD AUTOLAUNCH AUTOJP,$(f)=$($(f))))
# Objects depend on the switches, so changing one rebuilds instead of reusing stale objects.
.flags: FORCE
	@echo '$(FLAGSTAMP)' | cmp -s - $@ || echo '$(FLAGSTAMP)' > $@
FORCE:
MDASFLAGS = -x assembler-with-cpp -Imd -m68000 -Wa,--register-prefix-optional

ROM    = rom/introlab
# The rom shows one title's cabinet: art/$(TITLE)/cabinet_data.h, built by `make cabinet`.
TITLE ?= altbeast
CABDATA = art/$(TITLE)/cabinet_data.h
SHCCFLAGS += -Iart/$(TITLE) -Iart/logo
DOORDATA = art/logo/doors_data.h
MDOBJS = md/md_start.o md/font.o md/md_main.o
SHOBJS = sh/mars_start.o sh/m_main.o sh/s_main.o sh/video.o sh/hw.o sh/libmem.o sh/cabinet.o sh/poly.o sh/doors.o

all: $(ROM).32x

rom:
	@mkdir -p rom

md/%.o: md/%.s
	@$(MDCC) $(MDASFLAGS) -c $< -o $@
md/%.o: md/%.c
	@$(MDCC) $(MDCCFLAGS) -MMD -MP -c $< -o $@
rom/md.elf: $(MDOBJS) | rom
	@$(MDCC) -T md/md.ld -nostdlib $^ -o $@ $(MDLIBS)
sh/md_start.bin: rom/md.elf
	@$(MDOBJC) -O binary $< $@
sh/mars_start.o: sh/md_start.bin

sh/smoke_data.h: tools/gen_wisps.py
	@python3 $< --header sh/smoke_data.h md/smoke_sine.h
md/smoke_sine.h: sh/smoke_data.h
sh/hw.o: sh/smoke_data.h
md/md_main.o: md/smoke_sine.h

sh/gen_tables.h: tools/gen_tables.py
	@python3 $< $@
$(CABDATA): titles/$(TITLE).json $(wildcard tools/cabinet/*.py)
	@python3 tools/cabinet/build.py titles/$(TITLE).json
sh/cabinet.o: $(CABDATA) sh/gen_tables.h
# The doors and their logo are the same for every title.
$(DOORDATA): $(wildcard tools/logo/*.py)
	@python3 tools/logo/build_logo.py && cd tools/logo && python3 pack_doors.py
sh/doors.o: $(DOORDATA) sh/gen_tables.h
sh/poly.o sh/m_main.o sh/s_main.o: sh/gen_tables.h

sh/%.o: sh/%.s
	@$(SHAS) -Ish --small $< -o $@
# libmem holds the string.h primitives gcc calls behind your back; compiled with builtins
# on, gcc rewrites them into calls to themselves.
sh/libmem.o: sh/libmem.c
	@$(SHCC) $(SHCCFLAGS) -fno-builtin -c $< -o $@
sh/%.o: sh/%.c .flags
	@$(SHCC) $(SHCCFLAGS) -MMD -MP -c $< -o $@
$(ROM).elf: $(SHOBJS) | rom
	@$(SHCC) -T sh/mars.ld -nostdlib $^ -o $@ $(SHLIBS)
$(ROM).32x: $(ROM).elf
	@$(SHOBJC) -O binary $< $(ROM).tmp
	@dd if=$(ROM).tmp of=$@ bs=8192 conv=sync 2>/dev/null
	@$(SHNM) -n $< > $(ROM).lst
	@echo "introlab rom: introlab/$@ ($$(wc -c < $@ | tr -d ' ') bytes)"

# THE SAME INTRO AS THE PORT'S LAUNCHER: `make -C introlab port` -> rom/launcher_payload.bin,
# a program linked for its own cart page (sh/port.ld) and loaded by the port
# (sh_src/r60/r60_launcher.c, LAUNCHER=1 LAUNCHER_PAYLOAD=<this file>). No 68K code: the
# clock, pad and words come from the port side (sh/port_video.c, sh/text.c). Textures are
# read from the page and there are no frame-rate numbers.
PORTOBJS = $(addprefix port/,port_entry.o m_main.o s_main.o port_video.o text.o libmem.o cabinet.o poly.o doors.o carpet.o crowd.o)
# Rig probes, never candidates: PORTHUD=1 keeps the frame-rate line, AUTOLAUNCH=<vblanks>
# presses LAUNCH by itself that long after the cabinet parks, AUTOJP=1 crosses to JP first.
PORTFLAGS = $(SHCCFLAGS) -DPORT -DTEXROM $(if $(PORTHUD),,-DNOHUD) $(if $(AUTOLAUNCH),-DAUTOLAUNCH=$(AUTOLAUNCH)) $(if $(AUTOJP),-DAUTOJP)
port: rom/launcher_payload.bin
FONTDATA = art/$(TITLE)/font_arcade.h
$(FONTDATA): tools/gen_arcade_font.py titles/$(TITLE).json
	@python3 $^ $@
# The crowd's loop, from the arcade speech the sound lab decoded (untracked), if it is there.
SPEECH_DIR ?= ../sndtest/speech
CROWDDATA = art/$(TITLE)/crowd_data.h
$(CROWDDATA): tools/gen_crowd.py $(wildcard $(SPEECH_DIR)/utt_*.wav)
	@python3 tools/gen_crowd.py $(SPEECH_DIR) $@
port/crowd.o: $(CROWDDATA)
port/text.o: $(FONTDATA)
# The 68K half of the smoke as the port runs it: a relocatable program, then the picture.
port/smoke68k.bin: md/smoke68k.c md/smoke68k.ld
	@mkdir -p port
	@$(MDCC) $(MDCCFLAGS) -Os -mpcrel -fno-toplevel-reorder -nostdlib -T md/smoke68k.ld md/smoke68k.c -o $@
# The background music: the songs under art/$(TITLE)/music, if the owner captured any.
AMBIENCE = art/$(TITLE)/ambience.bin
$(AMBIENCE): tools/gen_ambience.py $(wildcard art/$(TITLE)/music/*)
	@python3 tools/gen_ambience.py art/$(TITLE)/music $@
# The room's sounds: the effects titles/$(TITLE).json picks from art/$(TITLE)/room, if captured.
ROOMSND = art/$(TITLE)/room.bin
ROOM_BUDGET ?= 6000
$(ROOMSND): tools/gen_room.py titles/$(TITLE).json $(wildcard art/$(TITLE)/room/*.csv)
	@python3 tools/gen_room.py titles/$(TITLE).json art/$(TITLE)/room $@ $(ROOM_BUDGET)
sh/smoke68k.h: tools/bin2h.py port/smoke68k.bin sh/smoke_data.h $(AMBIENCE) $(ROOMSND)
	@python3 tools/bin2h.py smoke68k $@ port/smoke68k.bin sh/smoke_data.pic $(AMBIENCE) $(ROOMSND)
port/port_entry.o: sh/smoke68k.h
port/cabinet.o: $(CABDATA) sh/gen_tables.h
port/doors.o: $(DOORDATA) sh/gen_tables.h
port/poly.o port/m_main.o port/s_main.o port/port_video.o: sh/gen_tables.h
port/libmem.o: sh/libmem.c
	@mkdir -p port
	@$(SHCC) $(PORTFLAGS) -fno-builtin -c $< -o $@
# The page is nearly full: what does not run every frame is built for size. The port's
# build stamp (tools/build_id.py) needs the rom's last 48 bytes, which are this page's.
PORT_SMALL = port/carpet.o port/port_video.o port/m_main.o port/text.o port/port_entry.o port/crowd.o
$(PORT_SMALL): PORTFLAGS += -Os
port/%.o: sh/%.c .flags
	@mkdir -p port
	@$(SHCC) $(PORTFLAGS) -MMD -MP -c $< -o $@
rom/launcher.elf: $(PORTOBJS) sh/port.ld | rom
	@$(SHCC) -T sh/port.ld -nostdlib $(PORTOBJS) -o $@ $(SHLIBS)
rom/launcher_payload.bin: rom/launcher.elf
	@$(SHOBJC) -O binary $< $@
	@$(SHNM) -n $< > rom/launcher.lst
	@echo "launcher payload: introlab/$@ ($$(wc -c < $@ | tr -d ' ') bytes)"

FRAMES ?= 300
shot: $(ROM).32x
	@python3 tools/shot.py $(ARES) $(ROM) $(FRAMES) '$(INPUT)' $(SHOTS)

# One title's cabinet: titles/$(TITLE).json names the model, the art and the tuning;
# everything it produces lands in art/$(TITLE)/ (gitignored). FORCE=1 re-runs Blender.
cabinet:
	@python3 tools/cabinet/build.py titles/$(TITLE).json $(if $(FORCE),--force)
reference:
	@blender -b --python tools/cabinet/reference_render.py -- titles/$(TITLE).json $(AZ) 2>&1 | grep REFERENCE

clean:
	rm -f md/*.o md/*.d sh/*.o sh/*.d sh/md_start.bin sh/gen_tables.h sh/font8.h sh/smoke_data.h sh/smoke_data.pic sh/smoke68k.h md/smoke_sine.h .flags rom/introlab.* rom/md.elf rom/launcher*
	rm -rf port

-include $(MDOBJS:.o=.d) $(SHOBJS:.o=.d) $(PORTOBJS:.o=.d)
.PHONY: all port shot clean cabinet reference FORCE
