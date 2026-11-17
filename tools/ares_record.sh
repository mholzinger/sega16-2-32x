#!/bin/sh
# Record an input tape from GUI play (2026-11-02). KIT.
#
#   tools/ares_record.sh build/port/rom/psg3.32x [build/tapes/psg3_neff.csv]
#
# Launches the FORK's desktop ares (~/src/ares-debug, desktop-ui/program/record.cpp) with the
# recorder armed: every pad button edge is written in the headless --input format, stamped with
# the port's own frame counter (_port_gf_count from the .elf beside the rom), so the tape replays
# on any later build at the same game ticks (docs/handoff/PARITY-HARNESS.md "Tick-phase
# lottery"). Play, then quit ares (Cmd-Q); the tape is complete when this script prints it.
# Never launch through `open rom.32x`: that is the stock ares, which has no recorder.
# Replay + proof: tools/tape_check.py ROM TAPE.
set -e
ROM=$1
TAPE=${2:-build/tapes/$(basename "${ROM%.32x}")_$(date +%m%d-%H%M).csv}
ARES=${ARES_DESKTOP:-$HOME/src/ares-debug/build_macos/desktop-ui/Release/ares.app/Contents/MacOS/ares}
NM=${SH_NM:-$HOME/src/marsdev/mars/sh-elf/bin/sh-elf-nm}
[ -f "$ROM" ] || { echo "ares_record: no rom $ROM" >&2; exit 1; }
[ -x "$ARES" ] || { echo "ares_record: no fork desktop ares at $ARES (cmake --build ~/src/ares-debug/build_macos --target desktop-ui --config Release)" >&2; exit 1; }
ELF=${ROM%.32x}.elf
[ -f "$ELF" ] || { echo "ares_record: no $ELF beside the rom (make port keeps one; the tape cannot be keyed without it)" >&2; exit 1; }
# (python, not awk: macOS awk has no strtonum and set -e killed the script silently, 2026-11-02)
GF=$("$NM" "$ELF" | python3 -c 'import sys
for l in sys.stdin:
    p = l.split()
    if len(p) == 3 and p[2] == "_port_gf_count": print("0x%X" % (int(p[0], 16) - 0x06000000))')
[ -n "$GF" ] || { echo "ares_record: $ELF has no _port_gf_count" >&2; exit 1; }
mkdir -p "$(dirname "$TAPE")"
echo "ares_record: $ROM counter sdram:$GF -> $TAPE (play, then Cmd-Q)"
ARES_INPUT_RECORD="$TAPE" ARES_INPUT_GAMEFRAME="sdram:$GF" "$ARES" "$ROM"
echo "ares_record: tape $TAPE ($(grep -vc '^#' "$TAPE") edges)"
