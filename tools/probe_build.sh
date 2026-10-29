#!/bin/sh
# Apply a probe patch, then build and gate -- IN THAT ORDER, AND ONLY ON SUCCESS.
#
#   tools/probe_build.sh <patch.py> [PORT_EXTRA...] > log 2>&1
#
# 2026-10-06: a patch script asserted out half-applied and the build ran anyway, because
# the two were separate commands in one shell line. A build of a source the patch did not
# finish writing is not a measurement of anything. `set -e` plus this order is the whole
# fix: the patch's exit status gates the chain, and a half-applied patch leaves no rom.
set -e
P=${1:?usage: tools/probe_build.sh <patch.py> [PORT_EXTRA...]}
shift
python3 "$P"
exec make check-demo PORT_EXTRA="$*"
