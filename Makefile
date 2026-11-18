# Altered Beast System 16B -> 32X. Build layout from 32x-builder (marsdev).
MARSDEV ?= ${HOME}/src/marsdev/mars
# The recipes use bash arithmetic ($$((16#...)) in mk/rom.mk) and tools/shadow_tree.sh
# uses read -d. macOS /bin/sh is bash; Debian/Ubuntu /bin/sh is dash, where both fail.
SHELL := /bin/bash
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

# The title's own build settings: decompile tables, music, art names. A title without the
# file can still unpack its rom set and bake tables; the port refuses it (PORT_TITLES).
-include games/$(GAME)/game.mk

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
include mk/options.mk

.PHONY: all release debug clean

all: release

include mk/line.mk

include mk/rom.mk

include mk/sndtest.mk

include mk/lint.mk

include mk/decomp.mk

include mk/stamp.mk
