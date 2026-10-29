#!/bin/sh
# Rebuild the `public` branch from the current HEAD tree minus the game-content
# paths that are tracked on the working branch. The snapshot's only parent is
# origin/main, so publishing is a fast-forward of main and the working history
# (roms, captures) never leaves the machine.
#   tools/public_refresh.sh                  # rebuilds `public`; push only when asked
#   git push origin public:main              # the publish step
# EXCLUDED (tracked here, never published):
#   tests/demo-b/arcade/   the arcade's work RAM at known frames (MAME) -- game data
#   tests/gameplay/arcade/ the same, along the input tape instead of the attract
#   tests/demo-b/picbase/  captured frames of the game's art
#   discover/cram/arcade/  the arcade's palette RAM
#   rom/night/             built roms: the game's program and art
set -e
cd "$(dirname "$0")/.."
EXCLUDE="tests/demo-b/arcade tests/gameplay/arcade tests/demo-b/picbase discover/cram/arcade rom/night"
export GIT_INDEX_FILE=$(mktemp)
git read-tree HEAD
for p in $EXCLUDE; do git rm -r -q --cached "$p" >/dev/null 2>&1 || true; done
T=$(git write-tree)
rm -f "$GIT_INDEX_FILE"; unset GIT_INDEX_FILE
P=$(git rev-parse -q --verify origin/main) && P="-p $P"
C=$(git commit-tree "$T" $P -m "public: $(git rev-parse --short HEAD) minus game-content paths")
git branch -f public "$C"
echo "public -> $C (tree $T); excluded: $EXCLUDE"
L=$(git ls-tree -r --name-only "$C" | grep -i -E '\.(32x|bs1|wav|pcm|mov|mp4)$' || true)
[ -z "$L" ] || { echo "REFUSED: game-content files still in the snapshot:"; echo "$L"; git branch -D public >/dev/null; exit 1; }
