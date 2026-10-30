#!/usr/bin/env bash
# CODE HEALTH GATE — `make lint`.
#
#   tools/lint.sh              everything
#   tools/lint.sh --generic    C linting only (cppcheck, gcc warnings, flags)
#   tools/lint.sh --32x        32X/project rules only (lint32x, ctr_audit)
#   tools/lint.sh --fast       skip the gcc strict-warning census (no compile)
#
# EXIT 1 if any ERROR-class finding is present. WARN and INFO never fail
# the gate: this codebase pivots, and a gate that cries wolf gets muted.
#
# THE FLAG SET IS NOT OPTIONAL. A lint run under the wrong -D set analyses
# code that does not ship and skips code that does. Every tool here is fed
# the defines of THE SHIPPING BUILD, `make candidate-defs` (line + PORT_FLAGS
# + CANDIDATE_FLAGS), never `.build_flags` -- that is the LAST BUILD, which is
# usually somebody's probe (LESSONS.md). Until 2026-10-29 this read
# `make line-defs` and sh_src/m_main.c, which the shipping build does not compile.
#
# The rom-reading rules (H7, H10, region guard) read build/port/rom/$LINT_ROM.*,
# default the newest candidate there.
#
# .build_flags is SAVED AND RESTORED around the make call: the FLAGSTAMP
# `$(shell ...)` in the Makefile runs at parse time, so merely asking
# make a question rewrites the stamp and would force a needless rebuild.
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD

DO_GENERIC=1; DO_32X=1; DO_GCC=1
for a in "$@"; do case $a in
  --generic) DO_32X=0 ;;
  --32x)     DO_GENERIC=0 ;;
  --fast)    DO_GCC=0 ;;
  -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
esac; done

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
ERRORS=0

hdr() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
err() { ERRORS=$((ERRORS+1)); }

# ---- the line's -D sets, without disturbing the stamp -----------------
[ -f .build_flags ] && cp .build_flags "$TMP/build_flags.save"
make candidate-defs > "$TMP/defs" 2>"$TMP/defs.err" || { cat "$TMP/defs.err"; echo "lint: 'make candidate-defs' failed"; exit 2; }
[ -f "$TMP/build_flags.save" ] && cp "$TMP/build_flags.save" .build_flags
MDDEFS=$(grep '^MD ' "$TMP/defs" | tr ' ' '\n' | grep '^-D' | sort -u | tr '\n' ' ')
SHDEFS=$(grep '^SH ' "$TMP/defs" | tr ' ' '\n' | grep '^-D' | sort -u | tr '\n' ' ')
echo "lint: shipping flags — MD $(wc -w <<<"$MDDEFS") defines, SH $(wc -w <<<"$SHDEFS") defines"
# the SH-2 sources the shipping build compiles (R60NEW: sh_src/r60 replaces m_main.c)
SHFILES="sh_src/r60/r60_main.c sh_src/r60/r60_planes.c sh_src/port_l2.c sh_src/port_selftest.c
         sh_src/mars.c sh_src/portfull/objdispatch.c"
LINT_ROM=${LINT_ROM:-$(ls -t build/port/rom/*.lst 2>/dev/null | head -1 | xargs -n1 basename 2>/dev/null | sed 's/\.lst$//')}
echo "lint: rom rules read build/port/rom/${LINT_ROM:-<none>}"

# ======================= GENERIC C =====================================
if [ $DO_GENERIC = 1 ]; then

hdr "cppcheck (shipping configuration)"
if command -v cppcheck >/dev/null; then
  CPPARGS="--enable=warning,style,performance,portability --inline-suppr
           --suppress=missingIncludeSystem --suppress=unusedStructMember
           --suppress=syntaxError --suppress=unknownMacro
           --platform=unix32 --std=c99 -q --max-configs=1
           --template={severity}:{id}:{file}:{line}: {message}"
  # shellcheck disable=SC2086
  cppcheck $CPPARGS $SHDEFS -I sh_src -I sh_src/r60 $SHFILES 2>"$TMP/cc_sh"
  # shellcheck disable=SC2086
  cppcheck $CPPARGS $MDDEFS -I md_src md_src/*.c 2>"$TMP/cc_md"
  cat "$TMP/cc_sh" "$TMP/cc_md" > "$TMP/cc"
  if [ -s "$TMP/cc" ]; then
    cut -d: -f1,2 "$TMP/cc" | sort | uniq -c | sort -rn | sed 's/^/  /'
    echo "  ---"
    # error and warning severities are worth a human; style is a census.
    grep -E '^(error|warning):' "$TMP/cc" | sed 's/^/  /' || true
    grep -qE '^error:' "$TMP/cc" && err
  else
    echo "  clean"
  fi
else
  echo "  SKIP — cppcheck not installed (brew install cppcheck)"
fi

hdr "gcc strict-warning census (beyond -Wall -Wextra)"
# NOT -Werror. These are the classes that have actually drawn garbage on
# this hardware; -Werror=return-type is already in the Makefile because
# a missing `return 0` handed bake_find's caller a frame pointer and it
# DREW (Makefile:44). The rest are a ranked census, not a gate: turning
# them fatal in a 16k-line file mid-pivot stops all work.
STRICT="-Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes
        -Wpointer-arith -Wcast-align -Wwrite-strings -Wredundant-decls
        -Wnested-externs -Wno-unused-parameter"
SHCC=${MARSDEV:-$HOME/src/marsdev/mars}/sh-elf/bin/sh-elf-gcc
MDCC=${MARSDEV:-$HOME/src/marsdev/mars}/m68k-elf/bin/m68k-elf-gcc
if [ $DO_GCC = 1 ] && [ -x "$SHCC" ]; then
  # shellcheck disable=SC2086
  $SHCC -m2 -mb -std=c99 -ffreestanding -fsyntax-only -Wall -Wextra $STRICT \
        $SHDEFS -I sh_src -I sh_src/r60 $SHFILES 2>"$TMP/w_sh"
  # shellcheck disable=SC2086
  $MDCC -m68000 -mshort -std=c99 -ffreestanding -fsyntax-only -Wall -Wextra $STRICT \
        $MDDEFS -I md_src md_src/md_main.c 2>"$TMP/w_md"
  grep -ohE '\[-W[a-z-]+\]' "$TMP/w_sh" "$TMP/w_md" | sort | uniq -c | sort -rn | sed 's/^/  /'
  N=$(grep -c 'warning:' "$TMP/w_sh" "$TMP/w_md" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
  echo "  total $N warnings (census, not a gate)"
  cat "$TMP/w_sh" "$TMP/w_md" > "$ROOT/docs/audit/gcc_strict.log" 2>/dev/null || true
  echo "  full text: docs/audit/gcc_strict.log"
elif [ $DO_GCC = 1 ]; then
  echo "  SKIP — sh-elf-gcc not found at $SHCC"
else
  echo "  SKIP — --fast"
fi

hdr "flag audit (dead flags, inert state)"
# A CENSUS, NOT A GATE. The dead flags it finds are pre-existing; making
# them fatal on day one means the gate is muted on day two. Run
# `tools/flag_audit.py --quiet` directly when you want it to fail.
python3 tools/flag_audit.py --quiet >"$TMP/fa" 2>&1 || true
if [ -s "$TMP/fa" ]; then sed 's/^/  /' "$TMP/fa"; else echo "  clean"; fi

fi

# ======================= 32X / PROJECT =================================
if [ $DO_32X = 1 ]; then

hdr "32X hardware rules (tools/lint32x.py)"
if [ -n "$LINT_ROM" ]; then
  python3 tools/lint32x.py --candidate "$LINT_ROM" --sh-defs "$SHDEFS" --md-defs "$MDDEFS" || err
else
  echo "  SKIP — no candidate rom in build/port/rom (make candidate NAME=...)"
fi

hdr "counter registry (tools/ctr_audit.py)"
python3 tools/ctr_audit.py --sh-defs "$SHDEFS" --md-defs "$MDDEFS" || err

hdr "region guard"
if [ -n "$LINT_ROM" ] && [ -f "build/port/rom/$LINT_ROM.lst" ]; then
  END=$(grep ' _end$' "build/port/rom/$LINT_ROM.lst" | awk '{print $1}' | tail -1)
  LIM=06019000
  if [ -n "$END" ]; then
    if [ "$((16#$END))" -lt "$((16#$LIM))" ]; then
      echo "  _end 0x$END < 0x$LIM — $(( (16#$LIM - 16#$END) )) bytes free"
    else
      echo "  _end 0x$END >= 0x$LIM — OVER"; err
    fi
  fi
else
  echo "  SKIP — no candidate .lst in build/port/rom"
fi

fi

hdr "result"
if [ $ERRORS -gt 0 ]; then
  echo "  $ERRORS ERROR-class section(s). Gate FAILS."
  exit 1
fi
echo "  no ERROR-class findings."
