#!/bin/zsh
# flagcheck.sh : the port must build with NO optional flags on a tree WITHOUT the gitignored,
# Sega-derived blobs (sh_src/sound.bin, speech.bin, sfx.bin, md_src/sound_tab.h, sh_src/*_tab.h).
# Four times in one week (2026-10-19) something needed unconditionally was declared or depended
# on inside a flag, and the other thread's tree stopped building with the cause far from the
# symptom. Run before handing a renderer/sound change to the builder. Exit 0 = clean.
cd ${0:A:h}/..
B=(sh_src/sound.bin sh_src/speech.bin sh_src/sfx.bin md_src/sound_tab.h sh_src/speech_tab.h sh_src/sfx_tab.h)
HIDE=($B ${B/#/build/port/})   # the shadow tree's rsync never deletes, so its copies must go too
T=$(mktemp -d)
for f in $HIDE; do [ -e $f ] && mv $f $T/${f//\//_}; done
restore() { for f in $HIDE; do [ -e $T/${f//\//_} ] && mv $T/${f//\//_} $f; done; rmdir $T 2>/dev/null; }
trap restore EXIT
make port PORT_EXTRA="" PORT_NAME=flagcheck > $T.log 2>&1; rc=$?
if [ $rc -ne 0 ]; then echo "flagcheck: BARE BUILD FAILED (blobs hidden)"; grep -m5 -E 'error|No rule|\*\*\*' $T.log; exit 1; fi
echo "flagcheck: bare build OK with the Sega-derived blobs hidden"
