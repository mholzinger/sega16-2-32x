#!/bin/sh
# Watch a recorded tape play in the GUI (2026-11-05). KIT. The recorder's twin.
#
#   tools/ares_play.sh build/port/rom/p15bpt2.32x build/tapes/p15bpt2_1105-1014.csv
#
# Launches the FORK's desktop ares (~/src/ares-debug, desktop-ui/program/record.cpp InputPlayer)
# with the tape driving the pads, keyed to the port's own frame counter (_port_gf_count from the
# .elf beside the rom) exactly as tools/ares_record.sh stamped it and tools/tape_check.py replays
# it headless. Watch, take screenshots, and after the tape's last edge the pad is yours: a tape
# that reaches the boss is a way to START at the boss. Run-ahead must be off (the player refuses
# to arm under it, as the recorder does). Never launch through `open rom.32x`: the stock ares has
# no player. A replay is the same INPUTS, not the same run: the game's own timing can diverge
# from the recording, so a tape that reached Aggar on one build may die earlier on another.
set -e
ROM=$1
TAPE=$2
ARES=${ARES_DESKTOP:-$HOME/src/ares-debug/build_macos/desktop-ui/Release/ares.app/Contents/MacOS/ares}
NM=${SH_NM:-$HOME/src/marsdev/mars/sh-elf/bin/sh-elf-nm}
[ -f "$ROM" ] || { echo "ares_play: no rom $ROM" >&2; exit 1; }
[ -f "$TAPE" ] || { echo "ares_play: no tape $TAPE" >&2; exit 1; }
[ -x "$ARES" ] || { echo "ares_play: no fork desktop ares at $ARES (cmake --build ~/src/ares-debug/build_macos --target desktop-ui --config Release)" >&2; exit 1; }
ELF=${ROM%.32x}.elf
[ -f "$ELF" ] || { echo "ares_play: no $ELF beside the rom (make port keeps one; the tape cannot be keyed without it)" >&2; exit 1; }
GF=$("$NM" "$ELF" | python3 -c 'import sys
for l in sys.stdin:
    p = l.split()
    if len(p) == 3 and p[2] == "_port_gf_count": print("0x%X" % (int(p[0], 16) - 0x06000000))')
[ -n "$GF" ] || { echo "ares_play: $ELF has no _port_gf_count" >&2; exit 1; }
echo "ares_play: $ROM counter sdram:$GF <- $TAPE ($(grep -vc '^#' "$TAPE") edges; the pad is yours after the last one)"
ARES_INPUT_PLAY="$TAPE" ARES_INPUT_GAMEFRAME="sdram:$GF" "$ARES" "$ROM"
