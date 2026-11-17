# Agent tools in this repo

## Asking Claude (billed to the API key, not the Max plan)

Use these for open work that reads a lot and returns a short answer: function
audits, log and census reading, FACTS/LESSONS/STATE lookups, second opinions.

### tools/ask_api.sh: one read-only question

    tools/ask_api.sh -n <name> -b <budget_usd> [-m <model>] "<self-contained question>"

- `-n` names the record, `-b` caps the spend in USD (default 2), `-m` defaults
  to `claude-sonnet-5-5`.
- The session sees this repo and `CLAUDE.md`, and nothing of your conversation.
  The question must stand alone: name the files, the addresses, and what form
  the answer should take.
- It is read-only: Read, Grep, Glob, `git log/show/diff`, `grep`, `rg`,
  `sed -n`, `head`, `tail`, `wc`, `ls`, and the m68k objdump. It cannot edit,
  build, or run an emulator.
- The answer prints on stdout. The cost line prints on stderr:
  `[ask_api] <name>: $cost, turns, paid by ANTHROPIC_API_KEY | today | month | <record path>`.
- The full record is `build/decomp/ask/<stamp>-<name>.json`. Costs append to
  `build/decomp/runs.jsonl`.

### tools/decomp_runner.py: a fix attempt judged by the gate

    build/decomp/.venv/bin/python tools/decomp_runner.py altbeast [--runs N] [--dry-run]

Takes the top queue item, has a Claude session write the fix in its own
worktree, then runs `make check-demo` itself and accepts or rejects on the
per-handler diff. Read the docstring at the top of the file before using it.
Use `--dry-run` first. Ask Mike before a real run: it spends about $5 a fix
session and takes the emulator lock.

### Rules

1. Never use a bare `claude -p` for this. Measured 2026-10-25: even with
   `ANTHROPIC_API_KEY` set it billed the Max plan. Only the two tools above
   reach the API key.
2. The key is the file `~/.config/anthropic/api_key`. It is not in the
   environment, and that is expected. Never print, log, or copy it.
3. If the cost line says `paid by` anything other than `ANTHROPIC_API_KEY`, or
   a `WARNING` appears, stop and tell Mike.
4. Always pass `-b`. Check the `today` and `month` totals on the cost line
   before sending a batch of questions.
5. An answer from these tools is a lead, not a fact. Verify it against the
   cited `file:line`, the gate, or Mike's pass before building on it.
6. Do not start emulators around the runner. It serialises them through
   `~/.cache/sega16-2-32x/emu.lock`.
