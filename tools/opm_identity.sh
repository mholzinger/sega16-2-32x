#!/bin/sh
# The engine identity gate: a reference build of sndtest/opm (build/opm_host/opm_render_ref,
# copied from a trusted build before an optimisation) against the current source, byte for
# byte, on the 97-test sequence and on Rise (all 8 channels), at the native rate and at
# 15625 Hz. An engine change that is meant to be exact must print IDENTICAL four times.
#
#   cp build/opm_host/opm_render build/opm_host/opm_render_ref    # before the change
#   tools/opm_identity.sh                                          # after it
set -e
cd "$(dirname "$0")/.."
REF=build/opm_host/opm_render_ref
[ -x $REF ] || { echo "no $REF: copy a trusted build/opm_host/opm_render there first"; exit 2; }
NEW=$(python3 -c 'import sys; sys.path.insert(0, "tools"); import opm_ab; print(opm_ab.build_opm())')
W=build/opm_identity
mkdir -p $W
fail=0
for set in "testseq 3875000" "rise05 8125000"; do
    name=${set% *}; n=${set#* }
    ev=build/opm_ab/$name/events.bin
    [ -f $ev ] || { echo "missing $ev (tools/opm_ab.py --out build/opm_ab/$name ...)"; exit 2; }
    for rate in 0 15625; do
        [ $rate = 0 ] && m=$n || m=$((n / 4))
        $REF $ev $W/ref.s16 $m $rate 255 4000000 - 2 2 > /dev/null
        $NEW $ev $W/new.s16 $m $rate 255 4000000 - 2 2 > /dev/null
        if cmp -s $W/ref.s16 $W/new.s16; then
            echo "$name rate $rate: IDENTICAL ($m samples)"
        else
            echo "$name rate $rate: DIFFERS"; cmp $W/ref.s16 $W/new.s16 | head -1; fail=1
        fi
    done
done
exit $fail
