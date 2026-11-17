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
        --name) [ $# -ge 2 ] || { echo "$0: --name requires a host" >&2; exit 2; }; name_opt="$2"; shift 2 ;;
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
# A destination is a basename, not another remote path. Control characters also
# cannot be represented unambiguously in the launch log protocol.
case "$name" in ''|.|..|*/*) echo "$0: destination must be a basename" >&2; exit 2 ;; esac
# Preserve the validated scp transport. Its legacy remote shell cannot safely
# receive arbitrary path text, so refuse it rather than silently changing to -O.
case "$name$MISTER_DIR" in *[!a-zA-Z0-9_./-]*)
    echo "$0: remote paths support ASCII letters, digits, underscore, dot, slash and hyphen only" >&2
    exit 2 ;;
esac
dest="$MISTER_DIR/$name"
# Quote ssh command arguments independently of the local shell.
shell_quote() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\''/g")"; }

echo "deploy  $rom -> $MISTER:$dest  ($(wc -c < "$rom" | tr -d ' ') bytes)"
echo "rig     $MISTER_HOST"
scp -q "$rom" "$MISTER:$dest"

[ "$launch" = 1 ] || exit 0

# Capture the boundary BEFORE the API request. Neither old matching lines nor
# ACTIVEGAME prove that this invocation launched anything. stat is BusyBox/GNU.
baseline=$(ssh -o ConnectTimeout=5 "$MISTER" \
    "if [ -f /tmp/remote.log ]; then stat -c '%d:%i:%s' /tmp/remote.log; else printf 'missing\\n'; fi") || {
    echo "$0: cannot establish launch-log boundary; launch not requested" >&2
    exit 1
}
case "$baseline" in
    missing) identity=missing; offset=0 ;;
    *:*:*) identity=${baseline%:*}; offset=${baseline##*:}
           printf '%s\n' "$baseline" | LC_ALL=C grep -Eq '^[0-9]+:[0-9]+:[0-9]+$' || {
               echo "$0: invalid launch-log boundary" >&2; exit 1;
           } ;;
    *) echo "$0: invalid launch-log boundary" >&2; exit 1 ;;
esac

# Keep JSON encoding separate from remote-shell quoting.
json_dest=$(printf '%s' "$dest" | sed 's/\\/\\\\/g; s/"/\\"/g')
echo "launch  http://$MISTER_HOST:8182/api/games/launch"
if ! curl -fsS -X POST "http://$MISTER_HOST:8182/api/games/launch" \
     -H 'Content-Type: application/json' \
     -d "{\"path\":\"$json_dest\"}" >/dev/null; then
    echo "$0: launch API request failed" >&2
    exit 1
fi

# Read only bytes appended after the boundary, rejecting a replaced/truncated
# log rather than accidentally accepting a historical launch after rotation.
# Fresh bytes are scanned locally, so a ROM name is never shell/regex code.
read_fresh='log=/tmp/remote.log
[ -f "$log" ] || exit 3
before=$(stat -c "%d:%i:%s" "$log") || exit 3
ident=${before%:*}; size=${before##*:}
[ "$1" = missing ] || [ "$ident" = "$1" ] || exit 4
[ "$size" -ge "$2" ] || exit 4
tail -c +$(($2 + 1)) "$log" || exit 3
# The sentinel joins an unterminated line; the local reader drops that last line.
printf "__MISTER_PUSH_END__\n"
after=$(stat -c "%d:%i:%s" "$log") || exit 3
[ "${after%:*}" = "$ident" ] && [ "${after##*:}" -ge "$size" ] || exit 4'
i=0
while [ $i -lt 10 ]; do
    sleep 1
    if fresh=$(ssh -o ConnectTimeout=5 "$MISTER" \
        "sh -c $(shell_quote "$read_fresh") sh $(shell_quote "$identity") $(shell_quote "$offset")"); then
        line=$(printf '%s\n' "$fresh" | sed '$d' | MISTER_EXPECTED_NAME="$name" awk '
            index($0, "game started:") {
                path = substr($0, index($0, "game started:") + length("game started:"))
                sub(/^[ \t]+/, "", path)
                sub(/\r$/, "", path)
                # mrext appends elapsed time to real launch-log paths.
                sub(/ \([0-9]+s\)$/, "", path)
                sub(/^.*\//, "", path)
                if (path == ENVIRON["MISTER_EXPECTED_NAME"]) found = $0
            }
            END { if (found != "") print found }')
        if [ -n "$line" ]; then
            echo "ok      launch log confirmed: $line"
            echo "        display and cold-boot state are not verified by this log"
            exit 0
        fi
    else
        echo "$0: fresh launch log unavailable, replaced or truncated; launch unconfirmed" >&2
        exit 1
    fi
    i=$((i + 1))
done

echo "WARN    no fresh matching 'game started:' entry after 10 polls; launch unconfirmed" >&2
exit 1
