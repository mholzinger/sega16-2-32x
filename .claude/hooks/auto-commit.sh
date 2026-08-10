#!/bin/bash
# Auto-commit hook (adopted from intv-game-builder): fires after every
# Edit/Write. Creates a [wip] commit so every change is revertable and
# the commit timestamps are the honest clock. Squash before handoff.
set -e

INPUT=$(cat)
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')
TOOL_NAME=$(echo "$INPUT" | jq -r '.tool_name // "edit"')

[ -z "$FILE_PATH" ] && exit 0

PROJECT_DIR="${CLAUDE_PROJECT_DIR:-$(git rev-parse --show-toplevel 2>/dev/null)}"
[ -z "$PROJECT_DIR" ] && exit 0
cd "$PROJECT_DIR"
[ ! -f "$FILE_PATH" ] && exit 0

REL_PATH=$(python3 -c "import os.path; print(os.path.relpath('$FILE_PATH','$PROJECT_DIR'))" 2>/dev/null || echo "$FILE_PATH")

# Source only: skip roms, listings, states, captures, configs, scratch
case "$REL_PATH" in
  rom/*|roms/*|mame/*|snap/*|screenshots/*|nvram/*|cfg/*|parity*/*|srcref/*|\
  *.32x|*.lst|*.bin|*.bs[0-9]|*.log|*.png|*.DS_Store|.claude/prompt.log|../*)
    exit 0
    ;;
esac

if git diff --quiet -- "$FILE_PATH" && git diff --cached --quiet -- "$FILE_PATH"; then
  if git ls-files --error-unmatch "$FILE_PATH" >/dev/null 2>&1; then
    exit 0
  fi
fi

LOG_FILE="$PROJECT_DIR/.claude/prompt.log"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
echo "[$TIMESTAMP] $TOOL_NAME: $REL_PATH" >> "$LOG_FILE"

git add "$FILE_PATH"
FILENAME=$(basename "$FILE_PATH")
git commit -m "[wip] $FILENAME" --no-verify 2>/dev/null || true
exit 0
