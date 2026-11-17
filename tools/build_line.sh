#!/bin/sh
# Build the shipping line under a name and record how it was built.
# Usage: tools/build_line.sh NAME [EXTRA...]   -> build/port/rom/NAME.32x (extra = probe flags, never a player ROM)
# Named rebuilds of one commit differ only inside the build stamp, the rom's last 48 bytes.
set -e
cd "$(dirname "$0")/.."
name=${1:?usage: tools/build_line.sh NAME [EXTRA...]}
shift
probe="$*"
# The option list and the three inputs outside git live in the Makefile
# (THE SHIPPING LINE); this script adds only the recipe file beside the rom.
go="$PWD/build/graphics/altbeast"
inputs=${LINE_INPUTS:-build/line-inputs}
make candidate GAME=altbeast NAME="$name" EXTRA="$probe"

# THE RECIPE TRAVELS WITH THE ROM (2026-11-13). macrolive-transformfix03 could not be
# rebuilt from its saved .flags: those are compiler defines, not this command, and five of
# its inputs are untracked. NAME.cmd records the command, the commit, and the SHA-256 of
# every input git does not hold: each file named by a KEY=/path argument plus the fixed set.
cmd="build/port/rom/$name.cmd"
{
  echo "# how build/port/rom/$name.32x was built; rerun from the repository root"
  echo "commit $(git rev-parse HEAD)$(git diff --quiet HEAD || echo ' +uncommitted-changes')"
  echo "sound.bin cut from: $(cat sh_src/sound.bin.src 2>/dev/null || echo unknown)"
  echo "tools/build_line.sh $name $probe"
  echo "# SHA-256 of inputs outside git"
  for f in $(for a in $probe; do case "$a" in (*=/*) echo "${a#*=}";; esac; done) \
           "$go/neff45.zb1" "$inputs/sprite_macros.mc1" "$inputs/sprite_watch.csv.gz" "$inputs/launcher_payload.bin" sh_src/sound.bin sh_src/speech.bin sh_src/sfx.bin sndtest/md/sndmap_data.h \
           mame/altbeast.zip; do
    if [ -f "$f" ]; then echo "$(shasum -a 256 "$f" | cut -d' ' -f1)  $f"; else echo "MISSING  $f"; fi
  done
  echo "rom $(shasum -a 256 "build/port/rom/$name.32x" | cut -d' ' -f1)  (the build stamp, the last 48 bytes, differs per build)"
} > "$cmd"
echo "recipe: $cmd"
