#!/usr/bin/env bash
# Cut a release: the built rom goes to a GitHub release.
#
# The itch.io page holds no files. It links to
#   https://github.com/<repo>/releases/latest
# which always resolves to the newest published release, so the page never
# needs editing when a build ships.
#
# Built locally, never on a hosted runner: the build needs mame/altbeast.zip,
# MAME for the sound capture and art that is not in git.
#
#   tools/release.sh ROM                 # dry run: prints what would happen
#   tools/release.sh ROM --go            # GitHub DRAFT release
#   tools/release.sh ROM --go --public   # published at once
#
#   --commit REV   the commit the rom was built from (default HEAD); the build
#                  number is its commit count, the same scheme as Backrooms
#
# Auth: gh reads the login already stored on this machine.
set -euo pipefail
cd "$(dirname "$0")/.."

REPO=${REPO:-mholzinger/sega16-2-32x}
GAME=${GAME:-altbeast}

ROM= GO=0 DRAFT=--draft COMMIT=HEAD
while [ $# -gt 0 ]; do
    case "$1" in
        --go) GO=1 ;;
        --public) DRAFT= ;;
        --commit) COMMIT=$2; shift ;;
        -*) echo "release: unknown option $1" >&2; exit 2 ;;
        *) ROM=$1 ;;
    esac
    shift
done
[ -n "$ROM" ] && [ -f "$ROM" ] || { echo "usage: tools/release.sh ROM [--go] [--public] [--commit REV]" >&2; exit 2; }

run() { if [ "$GO" = 1 ]; then "$@"; else printf '  would run:'; printf ' %q' "$@"; echo; fi; }

echo "[1/3] Identify the build"
SHA=$(git rev-parse --short "$COMMIT")
BUILD=$(git rev-list --count "$COMMIT")
TAG=build-$BUILD
STAMP=$(python3 tools/build_id.py show "$ROM")
echo "      $STAMP"
echo "      commit $SHA -> $TAG"
# PRESSURE roms are a MAME-side proxy and must never be handed over (CLAUDE.md).
case "$STAMP" in *PRESSURE*) echo "release: $ROM is a PRESSURE build; refusing." >&2; exit 1 ;; esac
# The stamp names the commit the rom was built from; a mismatch means the wrong
# rom is about to ship under this build number.
case "$STAMP" in *"$SHA"*) ;; *) echo "release: the rom's stamp does not name commit $SHA (pass --commit)." >&2; exit 1 ;; esac

echo "[2/3] Stage the asset and notes"
OUT=build/release/$TAG
mkdir -p "$OUT"
ASSET=$OUT/$GAME-32x-$TAG.32x
cp "$ROM" "$ASSET"
ROM_SHA=$(shasum -a 256 "$ASSET" | cut -d' ' -f1)
NOTES=$OUT/notes.md
cat > "$NOTES" <<EOF
$GAME for the Sega 32X, $TAG.

\`$(basename "$ASSET")\` is the cartridge image. Load it from a flash cartridge, on a MiSTer, or in ares.

SHA-256 \`$ROM_SHA\`
Built from commit $SHA.
EOF
echo "      $ASSET"
sed 's/^/      /' "$NOTES"

echo "[3/3] GitHub release ${DRAFT:+(draft) }on $REPO"
if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
    echo "      $TAG already exists on GitHub; leaving it alone."
else
    run gh release create "$TAG" "$ASSET" -R "$REPO" $DRAFT \
        --target main --title "$GAME $TAG" --notes-file "$NOTES"
fi

[ "$GO" = 1 ] || echo "Dry run. Nothing was uploaded. Add --go to do it."
