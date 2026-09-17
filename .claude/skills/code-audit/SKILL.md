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
| `tools/lint32x.py` | 10 hardware rules (H1–H10) | anything needing flow analysis |
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
2. **H7 is not a style note.** If `make lint` is red on H7, the shipping
   rom is fetching per-vint code across the cart bus. Do not baseline it
   to get a green gate; migrate what fits the `.ramtext` budget, rebuild,
   re-run. `tools/lint32x.py --rule H7` prints the ranked list.
3. **`tools/code_census.py`.** Report the four headline numbers: dead %,
   the biggest live function, the per-vint reachable set, duplicated
   blocks. A function over ~300 live lines with 20+ distinct guards is a
   deletion candidate, not a refactor candidate — check `REBUILD.md` §5
   (WHAT BURNS) before touching it.
4. **Attribute the dead half.** `tools/code_census.py --guards` ranks the
   outermost guard owning each dead line. A guard that owns 400 dead lines and is not in `LINE_FLAGS` and is not
   named in `STATE.md` as a live card is dead weight with a name.
5. **Only then read code.** Name the file and line for every claim.

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

The next four decide whether the rom **runs on real hardware**. None of
them reproduce under emulation, which is why none was caught by a test.

- **H7 RAMCODE** — a per-vint SH-2 function linked into the cart window
  (`0x02xxxxxx`) instead of SDRAM. `NOTES.md:181`: "While RV=1 the SH-2s
  must NEVER touch ROM", and RV=1 is held for the whole game because the
  arcade binary's self-references need the cart identity-mapped
  (`NOTES.md:162`). Where it merely works it is a 4-6x fetch stall on
  every instruction. Fix is `RAMCODE __attribute__((noinline))` — a static
  with a section placement still inlines under LTO. **The fix has a
  budget**: `.ramtext` is 28,672 B (`mars.ld:147`, its ASSERT fails the
  build), so the rule prints bytes needed against bytes free and ranks the
  migration. Reads `rom/s16.lst` and `rom/s16.elf`.
- **H8 FBBYTE** — a byte-wide access to the framebuffer. `TOOLKIT.md:617`:
  "FB byte-write zero-drop". A byte write to the FB does not write a byte,
  it drops.
- **H9 FSSPIN** — a spin on the FBCTL frame-select latch. `TOOLKIT.md:615`:
  the flip is gated on the MD V-counter, never the 32X VBLK bit. The
  hardware defers the latch to vblank, so spinning on it with FM held
  stalls the master for the frame — 12 points, 2026-09-10. `flip_span`'s
  spin is a known bounded exception and is baselined with its argument.
- **H10 VDPLEAK** — an arcade hardware address surviving in the **patched
  game body of the built rom**. The arcade's I/O is at `0xC4xxxx` and its
  low byte lands on the Mega Drive VDP, so one missed operand is the 68K
  writing game data into the VDP on hardware. Byte scan of `rom/s16.32x`;
  confirm a hit with `tools/code_stream.py` before believing it, and see
  `tools/hazard_census.py` for the instruction-class half of this question.
- **COLLISION / NO-WRITER / OVERFLOW / OVERLAP** — the instrument classes.
  Nine instruments have been caught lying here; two of those classes are
  static properties of the source and never need finding by contradiction
  again (`LESSONS.md`, "Counter indices collide", "A documented counter
  index may have NO WRITER AT ALL").

## Adding a rule

A rule earns its place when a real defect in this repo would have been
caught by it. Put it in `tools/lint32x.py` with the `docs/` citation in its
docstring, add a fixture to `tools/lint_selftest.py` that makes it **fire
and stay silent**, and run `tools/lint_selftest.py`. Three of the first six rules
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
