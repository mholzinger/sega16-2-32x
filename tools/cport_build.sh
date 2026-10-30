#!/bin/sh
# Build the decompiled corpus for SH-2, closing gcc's undeclared list by feedback.
#   tools/cport_build.sh [outdir]
# Ghidra invents symbol names freely (string literals, switch markers, stack and
# register placeholders). Rather than predict them, compile, harvest what gcc says
# is undeclared, regenerate, and repeat to a fixpoint.
set -e
cd "$(dirname "$0")/.."
OUT=${1:-/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/3ba22ef2-fdb1-416d-8b4f-e64e8f3f046a/scratchpad/cport}
G=$HOME/src/marsdev/mars/sh-elf/bin/sh-elf-gcc
# GCC 15 promotes 68K-normal pointer/integer punning to errors; C23 makes f() mean
# f(void), which breaks Ghidra's inconsistent arities. Both are dialect, not bugs.
# -fno-strict-aliasing IS REQUIRED for correctness, not hygiene. Every decompiled
# body type-puns -- Ghidra reads one cell as int and another as ushort -- and at
# -O2 gcc may assume two differently-typed accesses to the same address do not
# alias, then fold a load to a value it believes still holds. Measured 2026-09-27
# on the SH-2 self-test: FUN_00003f32, verified correct by hand against all four
# oracle vectors, reported 3 of 4 FAIL with its base read as 0. Adding this flag
# took the self-test 27 -> 30 of 39 and left ZERO genuine mismatches.
# CPORT_EXTRA_CFLAGS lets the Makefile add -finstrument-functions for PORTTRACE=1
# CPORT_OPT: the optimisation level for the DECOMPILED UNIT, -O2 unless told
# otherwise. -Os is 352,820 bytes of .text against -O2's 436,752 -- a 19% saving
# that the port needs, because rooting the real main loop (PORTMAIN) takes the
# closure from 429 to 716 functions and .portcode overflowed the 4 MB cart by
# 10,980 bytes at -O2 (measured 2026-10-02). It is a SIZE knob on a rom that
# measures correctness and drives no pixels, so speed is not the constraint --
# but it is still a second variable, so A/B it against the gate before trusting
# any number measured under it.
CPORT_OPT=${CPORT_OPT:--O2}
# THE -D SET COMES FROM THE MAKEFILE, ONE SOURCE. This line used to carry its own flags,
# so a define added to SHCCFLAGS reached every translation unit EXCEPT the generated port
# body -- measured 2026-10-06: PORT_TILEDIRTY was defined everywhere but the only unit that
# calls the macro, and the probe read zero marks against 90 hashed page changes.
CF="-m2 -ffreestanding -c $CPORT_OPT -fno-strict-aliasing -w -std=gnu99 $CPORT_DEFS $CPORT_EXTRA_CFLAGS \
    -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
    -Wno-error=implicit-function-declaration"
# A STALE port.o IS NOT A BUILD. 2026-10-04: five passes ended at 1 error, gcc wrote no
# object, and `ls -l port.o` below blessed the PREVIOUS build's object -- three roms
# (rd44-rd46) were measured as if they carried the new passes. Remove it first.
rm -f "$OUT/port.o"
# THE UNDECLARED-SYMBOL HARVEST IS PER TREE AND PER BUILD. It is a union that only
# grows (see the python below), and it used to live at a fixed /tmp path -- shared by
# the main checkout and .claude/worktrees/advisor-bisect, so one thread's harvest
# fabricated globals in the other thread's rom, and a symbol that stopped being
# undeclared kept its zeroed global for ever. Measured 2026-10-20: a two-day-old copy
# still carried ram0x000070be after the rename. Keep it in $OUT and start it empty;
# the five passes below still accumulate into it, which is what the feedback needs.
# the title directory, so the unit's #include "hand/X.c" finds games/<title>/hand (framework 3c)
HANDINC=${CPORT_HAND_DIR:+-I$(dirname "$CPORT_HAND_DIR")}
CPORT_OVERRIDES="$OUT/cport_overrides.json"
export CPORT_OVERRIDES
rm -f "$CPORT_OVERRIDES"
for i in 1 2 3 4 5; do
  tools/cport_prep.py "${CPORT_CORPUS:?CPORT_CORPUS unset -- run through make (docs/proof/<title>.c)}" "$OUT" >/dev/null
  N=$($G $CF -I"$OUT" $HANDINC "$OUT/altbeast_port.c" -o "$OUT/port.o" 2>&1 | grep -c 'error:' || true)
  echo "pass $i: $N errors"
  [ "$N" = 0 ] && break
  # LOCALE-PROOF (2026-10-11). This grep matched only gcc's CURLY quotes, which gcc
  # emits only in a UTF-8 locale. In a shell with LANG and LC_ALL unset -- any
  # non-interactive or scripted build -- gcc quotes with ASCII ' and the harvest matched
  # ZERO, so extras.h was written as a 136-byte stub and the build died on 133 undeclared
  # symbols. It stayed hidden for a week because build/port/sh_src/portfull/altbeast_port.c
  # was never regenerated; the failure only appears on a CLEAN port build. Accept both
  # quote styles, and set the locale so gcc's own output is predictable either way.
  LC_ALL=${LC_ALL:-en_US.UTF-8} \
  $G $CF -I"$OUT" $HANDINC "$OUT/altbeast_port.c" -o "$OUT/port.o" 2>&1 \
    | grep -oE "[‘'][A-Za-z_][A-Za-z0-9_]*[’'] undeclared" \
    | sed "s/[‘’']//g; s/ undeclared//" | sort -u > "$OUT/undeclared.txt"
  python3 - "$OUT" <<'PY'
import json, sys, os
o=sys.argv[1]
try: d=json.load(open(os.environ['CPORT_OVERRIDES']))
except Exception: d={}
u=set(d.get('undeclared',[]))
u |= {l.strip() for l in open(os.path.join(o,'undeclared.txt')) if l.strip()}
d['undeclared']=sorted(u)
json.dump(d, open(os.environ['CPORT_OVERRIDES'],'w'))
print('  harvested %d undeclared symbols (cumulative)' % len(u))
PY
done
# MAKE THE COMPILER CLEAN, and do it by DERIVING the casts rather than writing them.
# The decompiled program puns pointers and integers constantly -- the 68K stored handler
# addresses in work-RAM longs -- and C requires those conversions to be explicit. They
# are inserted from gcc's own diagnostics, so a clean build cannot have guessed. Casts
# that would LOSE bits (a pointer into a char) are NOT inserted: those are wrong
# declarations, and a cast would only hide them. Mike, 2026-09-27: "we want the compiler
# clean."
python3 tools/cport_cast.py "$OUT/altbeast_port.c" -I "$OUT" || true
# THE OBJECT MUST COME FROM THE TEXT cport_cast.py WROTE. The loop above produced
# port.o from the pre-cast text (the casts are warnings under -Wno-error, so pass N
# reached 0 errors before any were derived), and cport_cast.py now also rewrites the
# derived integer casts of symbol addresses into their 68K values (2026-10-04): an
# object compiled before it carried `&LAB_00003d60` as an SH-2 address and, once the
# text no longer named the symbol, cport_layout.py stopped PROVIDE-ing it (rd107 link:
# undefined reference to _LAB_00003d60). Recompile once more, from the final text.
$G $CF -I"$OUT" $HANDINC "$OUT/altbeast_port.c" -o "$OUT/port.o" 2>&1 | grep -E "error:" | head -5 || true

if [ -f "$OUT/port.o" ]; then ls -l "$OUT/port.o" && echo "*** SH-2 OBJECT PRODUCED ***"; else
  echo "*** NO SH-2 OBJECT: the unit does not compile (see the errors above) ***"
  $G $CF -I"$OUT" $HANDINC "$OUT/altbeast_port.c" -o "$OUT/port.o" 2>&1 | grep -E "error:" | head -5
  exit 1
fi
