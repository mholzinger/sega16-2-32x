#!/bin/sh
# A shadow of the source tree for an out-of-tree build.
#   tools/shadow_tree.sh build/port
# sh_src/, md_src/ and tools/ are REAL COPIES (rsync, newer-wins, objects excluded):
# the build regenerates tracked files in those trees (sh_src/portfull/*, md_src/*.h,
# the art blobs) and several tools write next to their own __file__, so a symlink
# there would write into the real tree (measured 2026-10-04: the first symlinked
# shadow rewrote sh_src/portfull/*, md_src/game_irq.h, sh_src/tiles.bin and
# tools/patch_report.txt in the real tree). Everything else tracked is a per-file
# symlink; large untracked read-only inputs (roms/ mame/ docs/proof/ srcref/) are
# directory symlinks. Re-running refreshes: a source edited in the real tree is newer
# and is copied over; a file the shadow regenerated is newer and is kept.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:?usage: tools/shadow_tree.sh <dir>}
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac
mkdir -p "$OUT"
cd "$ROOT"
for d in sh_src md_src tools; do
    mkdir -p "$OUT/$d"
    rsync -a --update --exclude='*.o' --exclude='*.d' --exclude='__pycache__' \
          --exclude='.DS_Store' "$ROOT/$d/" "$OUT/$d/"
done
# A SOURCE DELETED IN THE REAL TREE MUST DIE IN THE SHADOW TOO. The rsync above is
# --update with no --delete (deliberately: the shadow build regenerates files in these
# trees and newer-wins keeps them), so a .c the real tree no longer has lives on here
# forever -- and the Makefile's object list is a WILDCARD,
# `SHOBJS += $(patsubst %.c,%.o,$(wildcard sh_src/r60/*.c))`, so the stray gets compiled
# and linked. Measured 2026-10-20: sh_src/r60/r60_speech.c became r60_speech.inc in
# 387082a3 and the Oct-18 shadow copy stayed; `make candidate` then failed the link with
# five `multiple definition of _speech_census' against r60_main.o, which includes the
# .inc. Plain `make port` never showed it because the stray is only linked under
# R60SPEECH=1. Only COMPILABLE SOURCES are swept -- generated headers, blobs and the
# build's own outputs are left alone, which is the whole reason --delete is not used.
for f in $(cd "$OUT" && find sh_src md_src tools -type f \
             \( -name '*.c' -o -name '*.inc' -o -name '*.s' \) 2>/dev/null); do
    [ -e "$ROOT/$f" ] || { echo "shadow_tree: dropping stray $f (gone from the real tree)"; \
                           rm -f "$OUT/$f" "$OUT/${f%.*}.o" "$OUT/${f%.*}.d"; }
done
git ls-files -z | while IFS= read -r -d '' f; do
    case "$f" in build/*|rom/*|sh_src/*|md_src/*|tools/*) continue ;; esac
    d="$OUT/$(dirname "$f")"
    [ -d "$d" ] || mkdir -p "$d"
    [ -L "$OUT/$f" ] || { rm -f "$OUT/$f"; ln -s "$ROOT/$f" "$OUT/$f"; }
done
for d in roms mame docs/proof srcref; do
    [ -e "$ROOT/$d" ] || continue
    [ -L "$OUT/$d" ] && continue
    rm -rf "$OUT/$d"; mkdir -p "$(dirname "$OUT/$d")"; ln -s "$ROOT/$d" "$OUT/$d"
done
mkdir -p "$OUT/rom"
echo "shadow: $OUT"
