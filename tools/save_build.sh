#!/bin/sh
# Keep EVERY artefact of the build just made under one probe name, so a later
# build cannot silently replace the symbol files a measurement reads.
#   tools/save_build.sh rd2    ->  rom/rd2.32x rom/rd2.lst rom/rd2.elf rom/rd2.md.lst rom/rd2.md.elf rom/rd2.flags
# 2026-10-04: rom/md_start.lst belonged to the LINE rebuilt 33 s after rd1; every
# MD cell read for rd1 (slim_ready, slim_diag, slim_rec) was 4 bytes off and two
# hours went into a "stomp" that was slim_rec itself. The MD listing is per build.
set -e
N="$1"; [ -n "$N" ] || { echo "usage: $0 NAME" >&2; exit 1; }
cp rom/s16.32x "rom/$N.32x"
cp rom/s16.lst "rom/$N.lst"
cp rom/s16.elf "rom/$N.elf"
cp rom/md_start.lst "rom/$N.md.lst"
cp rom/md_start.elf "rom/$N.md.elf"
cp .build_flags "rom/$N.flags"
echo "saved rom/$N.{32x,lst,elf,md.lst,md.elf,flags}"
