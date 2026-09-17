---
name: code-audit
description: "Complete code-health audit of the sega16-2-32x port: what is dead on the line, which functions have grown past readability, what is duplicated, which counters lie, and which 32X hardware rules the source violates. TRIGGERS: 'code audit', 'audit the code', 'code health', 'what is the state of the code', 'what should we delete', 'is this file too big', 'find dead code', 'find duplication', 'check the counters', 'lint', 'before a refactor', 'what did the pivots leave behind'. Use BEFORE proposing any refactor, deletion, or flag removal, and when a measurement disagrees with the source."
---

# Code audit — sega16-2-32x

This codebase has pivoted eleven times and **50% of its C is `#ifdef`'d out
of the shipping rom**. That is the audit's subject. A generic linter finds
31 style notes in 16.5k lines of `m_main.c` and none of the classes that
have actually cost this project a session.

Everything here is measured **against THE LINE's `-D` set**, never against
`.build_flags` — that is the last build, usually somebody's probe
(`LESSONS.md`). `make line-defs` prints the true set without building and
without restamping.

## Run this first, always

    make lint                    # the gate: 32X rules, counters, region
    tools/code_census.py         # the shape: dead, size, hot, duplication

Under 15 seconds combined. Do not audit by reading; read after measuring.

## What the tools cover, and what they do not

| tool | answers | does NOT answer |
|---|---|---|
| `tools/lint.sh` (`make lint`) | the whole gate below | speed, pixels |
| `tools/lint32x.py` | 6 hardware rules (H1–H6) | anything needing flow analysis |
| `tools/ctr_audit.py` | counter collisions, unwritten slots, block overlap | whether a counter means what it says |
| `tools/code_census.py` | dead %, function size, per-vint reachability, duplication | what actually ran — that is `tools/frame_timeline.py` |
| `tools/flag_audit.py` | flags that cannot do anything | a flag whose effect is undone elsewhere |
| `cppcheck` | generic C defects, in the line configuration | every rule above |
| `tools/lint_selftest.py` | that the rules still fire | — |

`valgrind` and the sanitizers do not apply: two freestanding cross targets
(SH-2, 68000), no host execution, no allocator.

## The audit, in order

1. **`make lint`.** New ERROR = stop and fix. Baselined findings are
   pre-existing and listed in `tools/ctr_baseline.txt` and
   `tools/lint32x_baseline.txt`. **Never re-`--bless` to silence something
   you introduced.**
2. **`tools/code_census.py`.** Report the four headline numbers: dead %,
   the biggest live function, the per-vint reachable set, duplicated
   blocks. A function over ~300 live lines with 20+ distinct guards is a
   deletion candidate, not a refactor candidate — check `REBUILD.md` §5
   (WHAT BURNS) before touching it.
3. **Attribute the dead half.** `tools/code_census.py --guards` ranks the
   outermost guard owning each dead line. A guard that owns 400 dead lines and is not in `LINE_FLAGS` and is not
   named in `STATE.md` as a live card is dead weight with a name.
4. **Only then read code.** Name the file and line for every claim.

## The rules, and why each exists

Each cites the document that established it. Do not restate a rule without
its citation; do not add one without a measurement.

- **H1 VOLATILE** — a hardware address through a non-volatile pointer. gcc
  may hoist or drop the access.
- **H2 ALIAS** — one address spelled cached (`0x06`/`0x20`) and uncached
  (`0x26`/`0x24`). `m_main.c:710`: "a stale cache line is the same bug
  wearing a different hat." A deliberate pair must be named `X_U` / `X_C`.
- **H3 ONE-SOURCE** — an FB-transport address as a literal instead of from
  `md_src/packet_fmt.h`. `SILICON.md` §3.
- **H4 FM-WRITE** — a 68K framebuffer access on a path holding FM=1.
  `SILICON.md` FACT 2: the write does not land. Textual, so WARN.
- **H5 MIRROR** — `*_SH != 0x24000000 + (*_MD - 0x840000)`.
- **H6 NEVERSHIP** — a flag the Makefile marks NEVER SHIP present on the
  line.
- **COLLISION / NO-WRITER / OVERFLOW / OVERLAP** — the instrument classes.
  Nine instruments have been caught lying here; two of those classes are
  static properties of the source and never need finding by contradiction
  again (`LESSONS.md`, "Counter indices collide", "A documented counter
  index may have NO WRITER AT ALL").

## Adding a rule

A rule earns its place when a real defect in this repo would have been
caught by it. Put it in `tools/lint32x.py` with the `docs/` citation in its
docstring, add a fixture to `tools/lint_selftest.py` that makes it **fire
and stay silent**, and run `tools/lint_selftest.py`. Three of the six rules
were wrong on their first cut and the fixtures are what caught them — H4
fired on a tracer comment, and a comment-stripping regex silently disabled
H1, H3 and H4 on every line starting with `*`.

## What the audit must never do

- Propose a refactor of machinery `docs/design/REBUILD.md` §5 says to
  delete. Check WHAT BURNS first; a day was spent optimising it once.
- Treat `.build_flags` as the line.
- Report a count without saying which flag set produced it.
- Rank speed. `make lint` measures structure. MAME cannot speed-rank this
  build and ares charges instruction cycles only (`LESSONS.md`); the rig's
  frame-rate probe is the instrument.
