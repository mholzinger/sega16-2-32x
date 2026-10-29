#!/bin/sh
# Copy a rom to the MiSTer and launch it, the way the rig recipes do
# (docs/handoff/HANDOFF-DREQ.md "THE RIG").
#
#   tools/mister_push.sh rom/night/vi25.32x
#   tools/mister_push.sh -n rom/s16.32x        # copy only, no launch
#   tools/mister_push.sh rom/s16.32x probe.32x # land under another name
#   tools/mister_push.sh --name mister.tv.local rom/s16.32x   # the other rig
#
# THERE ARE TWO RIGS (2026-09-27) AND NEITHER IS THE DEFAULT:
# mister.office.local belongs to the DECOMPILE thread, mister.tv.local to the
# BUILDER. --name is REQUIRED. It takes the host and sets both the ssh
# target and the launch API host together, because setting one and forgetting
# the other is the mistake that env overrides invite -- and a push landing on
# the wrong rig is indistinguishable from a rom that did not take.
#
# Still overridable by environment, and --name wins over MISTER:
#   MISTER=root@other.local  MISTER_HOST=other.local  MISTER_DIR=/path

set -e

launch=1
name_opt=""
while [ $# -gt 0 ]; do
    case "$1" in
        --name) name_opt="$2"; shift 2 ;;
        --name=*) name_opt="${1#--name=}"; shift ;;
        -n|--no-launch) launch=0; shift ;;
        -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        --) shift; break ;;
        -*) echo "$0: unknown option $1" >&2; exit 2 ;;
        *) break ;;
    esac
done

if [ -n "$name_opt" ]; then
    MISTER="root@$name_opt"
    MISTER_HOST="$name_opt"
fi
# THERE IS NO DEFAULT RIG, ON PURPOSE (2026-09-27). mister.office.local is the
# DECOMPILE thread's and mister.tv.local is the BUILDER's. A default sends every
# un-flagged invocation at one of them, which is how the wrong rig got a rom
# more than once -- and the push that misfires is invisible from here, because
# the launch API answers "game started" whichever machine it was. Naming the rig
# is one word; guessing it wrong costs somebody else's session. So: name it.
if [ -z "$MISTER" ] && [ -z "$MISTER_HOST" ]; then
    echo "$0: name the rig -- there is no default." >&2
    echo "  --name mister.tv.local      the BUILDER's rig" >&2
    echo "  --name mister.office.local  the DECOMPILE thread's rig" >&2
    exit 2
fi
MISTER="${MISTER:-root@$MISTER_HOST}"
MISTER_HOST="${MISTER_HOST:-${MISTER#*@}}"
MISTER_DIR="${MISTER_DIR:-/media/fat/games/S32X}"
MISTER_DIR="${MISTER_DIR%/}"

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
echo "rig     $MISTER_HOST"
scp -q "$rom" "$MISTER:$dest"

[ "$launch" = 1 ] || exit 0

echo "launch  http://$MISTER_HOST:8182/api/games/launch"
curl -sS -X POST "http://$MISTER_HOST:8182/api/games/launch" \
     -H 'Content-Type: application/json' \
     -d "{\"path\":\"$dest\"}" >/dev/null

# /tmp/remote.log is the real signal; /tmp/ACTIVEGAME is written by mrext
# itself and proves nothing. The launch appends "core stopped/started" lines
# right after "game started", so scan the recent tail, not just the last line.
i=0
while [ $i -lt 10 ]; do
    sleep 1
    line=$(ssh -o ConnectTimeout=5 "$MISTER" \
        "tail -10 /tmp/remote.log | grep 'game started' | grep '$name' | tail -1" \
        2>/dev/null || true)
    if [ -n "$line" ]; then
        echo "ok      $line"
        exit 0
    fi
    i=$((i + 1))
done

echo "WARN    no 'game started: ...$name' in /tmp/remote.log after 10s" >&2
echo "        recent log:" >&2
ssh -o ConnectTimeout=5 "$MISTER" 'tail -5 /tmp/remote.log' 2>/dev/null | sed 's/^/        /' >&2 || true
echo "        if MiSTer main is not running the cmd FIFO has no reader:" >&2
echo "        ssh $MISTER 'ps aux | grep -c [M]iSTer'   then reboot" >&2
exit 1
