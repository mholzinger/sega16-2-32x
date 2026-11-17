# The shipping line: the flag lists, the out-of-tree port build, `make candidate`,
# `make release-rom` and the gates (check-demo, parity).
# Part of the top-level Makefile; included, never run on its own.
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
              BANDSHIFT=$(SHIPBANDSHIFT) BLITSKIP=1 DIRTYROW=1 \
                ROWDEFER=1 \
               EDGE42=1 \
              TILESLIM=1 SLIMCAP=80 FBXECHO=1 BGBOTTOM=1 \
              ARTTAIL=1 CUTPREFETCH=1
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
SHIP_US = $(SHIP_COMMON) $(if $(NOSPRBAKE),,SPRBAKE=1) MDSPR=1 PALSTATIC=1 $(if $(NOGLOW),,PALGLOW=1) MDSTATIC=1
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
LINE_FLAGS = FBXSTAGE=1 FBXPEND=1 \
             TEXTCAPMASTER=1 GAMEGATE=1 GAMEGATEWAIT=1 \
              C1NOFB=1 C1PUNCH=1 \
                MDSREFUSE=1 \
                TEXTCAPMASK=1 TEXTMASKPKT=1 \
             TILESMD=1
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
	PORTPLAY=1 PORTBOOT=1 PORTCORO=1 \
	MDATTRACTSPLIT=1 MDLINES4=1 MDSPROFF=1 PORTTILEDIRTY=1 \
	$(R60_LINE_FLAGS)
# THE R60 RENDERER'S LINE (2026-10-16, sw121, Mike's office-PVM pass): a plain `make port`
# builds it. Every sw-rom since 2026-10-14 passed these through PORT_EXTRA by hand; a
# plain `make port` built the deleted renderer's path instead. Probe flags still go in
# PORT_EXTRA on top.
R60_LINE_FLAGS = R60NEW=1 GOVERNOR=1 R60SLAVEWIN=1 \
	 PALFIRST=1 R60TEXT=1 R60RECTCLR=1
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
sh_src/port_l2.o: games/$(GAME)/glue.c games/$(GAME)/mcu.inc games/$(GAME)/coro_redirect.inc games/$(GAME)/port_game.h
# the title's 68K program, read as data by the port (sh_src/port_rom_data.s .incbin)
sh_src/port_rom_data.o: SHASFLAGS += -I$(GAMEROMS)
sh_src/port_rom_data.o: $(GAMEROMS)/prog68k.bin
sh_src/port_l2.o: SHCCFLAGS += -Igames/$(GAME)
.PHONY: romset

port: romset
	GAME=$(GAME) tools/shadow_tree.sh $(PORT_DIR)
	$(MAKE) -C $(PORT_DIR) line $(PORT_FLAGS) $(PORT_EXTRA) ARTBAKE_CORPUS=$(abspath $(GAME_ART_CORPUS))
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
CANDIDATE_FLAGS = R60PLOT5=1 R60ROWCLR=1 \
	 R60KEEPSPR=1 R60EMPTYPKT=1 R60PRIO=1 \
	  R60IDLECONSUME=1 R60LIVEPAD=1 R60FRONTEND=1 NOTILESMD=1 \
	R60SPEECH=1 R60SFX=1 R60CUTPF=1 R60PALMAP=1 R60ZOOMFAST=1 R60ROWFAST=1 PORTHOT2=1 CARTMB=4
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
LINE_HOT_SET ?= $(GAME_HOT_SET)
LINE_ATLAS = build/graphics/$(GAME)/$(GAME_ZOOM_ATLAS).zb1
LINE_NOLAUNCHER = $(or $(NOLAUNCHER),$(findstring NOLAUNCHER=1,$(EXTRA)))
CANDIDATE_LINE = CARTSSF=1 R60SOUND=1 R60PSGMUSIC=1 R60SOUNDCOMPACT=1 \
	R60SPANCAP=1536 R60TXTPOOLHYB=1 R60BAKEART=1 R60BAKEPAGES=1 R60PUNCHTHRU=1 R60SPRPREP=1 \
	R60SPRPREP2=1 R60SPRPREP2S=256 R60SPRPREP2H=24 \
	R60SPANPROGRESS=1 R60TILEBATCH=1 R60WORKSET=1 \
	R60WORKSETATLAS=$(abspath $(LINE_ATLAS)) R60WORKSETPOSES=$(abspath games/$(GAME)/graphics/$(GAME_WORKSET_POSES)) \
	 R60ZOOMRUN=1 R60WORKSETCODE=4096 \
	R60MACROPROBE=1 R60MACROLIVE=1 R60MACROLIVEDRAW=1 \
	$(if $(LINE_HAS_MACROS),R60MACROPACKETS=$(abspath $(LINE_MACROS))) \
	$(if $(and $(LINE_HAS_MACROS),$(wildcard $(LINE_MACRO_CAPTURE))),R60MACROCAPTURE=$(abspath $(LINE_MACRO_CAPTURE)) R60MACROKEY=$(GAME_MACRO_KEY)) \
	HOT_SET=$(abspath $(LINE_HOT_SET)) \
	$(if $(LINE_NOLAUNCHER),,LAUNCHER=1 LAUNCHER_PAYLOAD=$(abspath $(LINE_LAUNCHER)))
CANDIDATE_ALL = $(CANDIDATE_FLAGS) $(CANDIDATE_LINE) $(EXTRA)
# the scaled-pose atlas the working-set page is baked from; rebuilt every time, it is cheap
.PHONY: line-atlas
line-atlas:
	@[ -f sh_src/sprites.bin ] || { python3 tools/romset.py $(GAME); GAME=$(GAME) python3 tools/gen_sprites.py; }
	@mkdir -p $(dir $(LINE_ATLAS))
	python3 tools/extend_zoom_atlas.py --empty-base --manifest games/$(GAME)/graphics/$(GAME_ZOOM_MANIFEST) \
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
	  test -d "$(SNDAUDIO)" || { echo "music-inputs: no $(SNDAUDIO) -- unpack the music pack there: $(GAME_SND_PACK_NAME)"; exit 2; }; \
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
#   make parity ROM=build/port/rom/hp1.32x [TAPE=tools/inputs/level1_full.csv]
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
