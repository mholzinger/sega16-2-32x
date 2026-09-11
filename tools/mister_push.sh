#!/bin/sh
# Copy a rom to the MiSTer and launch it, the way the rig recipes do
# (docs/handoff/HANDOFF-DREQ.md "THE RIG").
#
#   tools/mister_push.sh rom/night/vi25.32x
#   tools/mister_push.sh -n rom/s16.32x        # copy only, no launch
#   tools/mister_push.sh rom/s16.32x probe.32x # land under another name
#
# Overridable: MISTER=root@other.local  MISTER_DIR=/media/fat/games/S32X
#              MISTER_HOST=other.local  (the HTTP host for the launch API)

set -e

MISTER="${MISTER:-root@mister.office.local}"
MISTER_HOST="${MISTER_HOST:-${MISTER#*@}}"
MISTER_DIR="${MISTER_DIR:-/media/fat/games/S32X}"
MISTER_DIR="${MISTER_DIR%/}"

launch=1
case "$1" in
    -n|--no-launch) launch=0; shift ;;
    -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
esac

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "usage: $0 [-n] <rom> [dest-name]" >&2
    exit 2
fi

rom="$1"
if [ ! -f "$rom" ]; then
    echo "$0: no such file: $rom" >&2
    exit 1
fi
name="${2:-$(basename "$rom")}"
dest="$MISTER_DIR/$name"

echo "deploy  $rom -> $MISTER:$dest  ($(wc -c < "$rom" | tr -d ' ') bytes)"
scp -q "$rom" "$MISTER:$dest"

[ "$launch" = 1 ] || exit 0

echo "launch  http://$MISTER_HOST:8182/api/games/launch"
curl -sS -X POST "http://$MISTER_HOST:8182/api/games/launch" \
     -H 'Content-Type: application/json' \
     -d "{\"path\":\"$dest\"}" >/dev/null

# /tmp/remote.log is the real signal; /tmp/ACTIVEGAME is written by mrext
# itself and proves nothing.
i=0
while [ $i -lt 10 ]; do
    sleep 1
    line=$(ssh -o ConnectTimeout=5 "$MISTER" 'tail -1 /tmp/remote.log' 2>/dev/null || true)
    case "$line" in
        *"game started"*"$name"*) echo "ok      $line"; exit 0 ;;
    esac
    i=$((i + 1))
done

echo "WARN    no 'game started: ...$name' in /tmp/remote.log after 10s" >&2
echo "        last line: ${line:-<none>}" >&2
echo "        if MiSTer main is not running the cmd FIFO has no reader:" >&2
echo "        ssh $MISTER 'ps aux | grep -c [M]iSTer'   then reboot" >&2
exit 1
