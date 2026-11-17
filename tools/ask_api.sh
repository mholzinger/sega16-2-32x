#!/bin/zsh
# One read-only question to Claude on the API key (not the Max plan); cost logged to
# build/decomp/runs.jsonl. See tools/ask_api.py.
#   tools/ask_api.sh [-n name] [-b budget_usd] [-m model] "<self-contained question>"
ROOT=${0:A:h:h}
exec $ROOT/build/decomp/.venv/bin/python $ROOT/tools/ask_api.py "$@"
