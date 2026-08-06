#!/bin/bash
# Extract frames from a screen recording and remove consecutive duplicates.
# Usage:
#   ./capture.sh                      # full recording, default 2:50
#   ./capture.sh 00:01:30             # first 1m30s only
#   ./capture.sh 00:00:10 00:01:00    # from 10s to 1m
#   ./capture.sh dedup                # just dedup existing frames (skip extract)
#   ./capture.sh raw [start] [end]    # NO dedup, keep frame_NNNNNN.png — for TIMING
#
# WHICH MODE?
#   default (dedup) -> VISUAL debugging. Hunting a seam, a wrong tile, a ghost
#                      row. Duplicate frames are noise; killing them makes the
#                      interesting frames easy to page through.
#   raw             -> TIMING / tools/frame_profiler.py. Dedup BIASES that
#                      measurement: the profiler counts red-bordered (overrun)
#                      frames, and the border flipping black<->red is itself a
#                      visual difference, so overrun frames always survive dedup
#                      while static ok-frames get deleted. The surviving set is
#                      enriched for exactly the thing being counted, and the
#                      resulting percentage is not a per-frame overrun rate.
#                      raw keeps every recorded frame, so the number means what
#                      it says.

set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
SSDIR="$DIR/screenshots"
mkdir -p "$SSDIR"

dedup_only=false
raw=false
start="00:00:00"
end="00:02:50"

if [[ "${1:-}" == "dedup" ]]; then
    dedup_only=true
elif [[ "${1:-}" == "raw" ]]; then
    raw=true
    shift
    [[ $# -ge 1 ]] && end="$1"
    [[ $# -ge 2 ]] && { start="$1"; end="$2"; }
elif [[ $# -eq 1 ]]; then
    end="$1"
elif [[ $# -eq 2 ]]; then
    start="$1"
    end="$2"
fi

# --- Extract ---
if [[ "$dedup_only" == false ]]; then
    rm -f "$SSDIR"/frame_*.png
    # Find newest Screen Recording on Desktop
    newest=$(find "$HOME/Desktop" -maxdepth 1 -name "Screen Recording*.mov" -print0 \
        | xargs -0 ls -t 2>/dev/null | head -1)
    if [[ -z "$newest" ]]; then
        echo "No Screen Recording found on Desktop"
        exit 1
    fi
    mv "$newest" "$SSDIR/capture.mov"
    echo "Extracting frames ($start → $end)..."
    # -fps_mode passthrough: emit exactly the frames stored in the .mov, no
    # duplication or dropping to hit a constant rate. macOS screen recordings are
    # variable-frame-rate, and for timing work an ffmpeg-invented duplicate frame
    # would read as a real emulator frame.
    ffmpeg -hide_banner -loglevel error -ss "$start" -to "$end" \
        -i "$SSDIR/capture.mov" -fps_mode passthrough "$SSDIR/frame_%06d.png" 2>/dev/null \
    || ffmpeg -hide_banner -loglevel error -ss "$start" -to "$end" \
        -i "$SSDIR/capture.mov" -vsync 0 "$SSDIR/frame_%06d.png"
fi

if [[ "$raw" == true ]]; then
    n=$(ls "$SSDIR"/frame_*.png 2>/dev/null | wc -l | tr -d ' ')
    echo "raw mode: $n frames kept (no dedup, no renumber)."
    echo "  python3 tools/frame_profiler.py screenshots/"
    exit 0
fi

# --- Dedup visually identical consecutive frames ---
# Video compression means byte-identical is rare; use ImageMagick RMSE instead.
# Threshold 0.005 catches compression artifacts but keeps real scene changes.
THRESHOLD="0.005"

frames=("$SSDIR"/frame_*.png)
total=${#frames[@]}
if [[ "$total" -eq 0 ]]; then
    echo "No frames to dedup"
    exit 0
fi

echo "Deduplicating $total frames (RMSE threshold $THRESHOLD)..."
prev="${frames[0]}"
removed=0

set +e  # magick compare exits 1 when images differ
for ((i=1; i<total; i++)); do
    f="${frames[$i]}"
    raw=$(magick compare -metric RMSE "$prev" "$f" null: 2>&1)
    rmse=$(echo "$raw" | grep -oE '\([0-9.e+-]+\)' | tr -d '()')
    if [[ -n "$rmse" ]] && (( $(echo "$rmse < $THRESHOLD" | bc -l) )); then
        rm "$f"
        ((removed++))
    else
        prev="$f"
    fi
done
set -e

remaining=$((total - removed))
echo "Done: $total extracted → $remaining unique ($removed duplicates removed)"

# --- Renumber surviving frames 1.png, 2.png, ... ---
echo "Renumbering frames..."
i=1
for f in "$SSDIR"/frame_*.png; do
    [[ -e "$f" ]] || continue
    mv "$f" "$SSDIR/$i.png"
    ((i++))
done
