# The eleven bounding defects, resolved

`tools/bound_ref.py` against `docs/audit/function_map.md`, 2026-09-11
(LOOP-DECOMPILE 73). Entry 54 left ten and described them as "5 genuinely
truncated, 5 stubs that resisted every automated rule". The real split is
**six truncations and five blocks that are not code at all**, and the
"stubs" are 16 to 114 bytes, not 1 to 16.

**Nothing here is applied to the Ghidra project.** It is a repair list.
The deletions are in `kill_funcs.py`'s input format already
(`docs/audit/bound_kill.txt`).

## Six are code, and genuinely truncated. Extend them.

Each walks from its own entry to the first terminator past the declared
end and crosses no other function entry on the way.

| entry | size now | true end | size then | why it is code |
|---|---|---|---|---|
| 0x0040E | 240 | 0x0057A | 364 | reset; `bra.w` at 0x00400 |
| 0x05FA8 | 50 | 0x0602C | 132 | `bsr.s` at 0x05F96 |
| 0x060E6 | 124 | 0x06168 | 130 | `bsr.w` at 0x05F08 |
| 0x063CC | 22 | 0x0644E | 130 | `bsr.w` at 0x06356 |
| 0x06C44 | 14 | 0x06CB8 | 116 | ten call sites, first `bsr.w` at 0x06B38 |
| 0x18146 | 60 | 0x181E4 | 158 | `movel #0x18146,%fp@(2)` at 0x18034 — object $02 is the routine pointer |

## Five are not code. Remove them.

| entry | size | what it actually is |
|---|---|---|
| 0x06E7A | 88 | an entry in the pointer-table run at 0x06D98-0x06DAC, and its own body is 22 longs that all land inside the rom |
| 0x08532 | 24 | `movel #0x8532,%a0@(36)` at 0x082A2 — object **$24 is the anim script pointer** |
| 0x0DE56 | 16 | an entry in the pointer-table run at 0x0DE1E-0x0DE2E |
| 0x18F38 | 24 | `movel #0x18F38,%fp@(36)` at 0x18990 — object $24 again |
| 0x1A0B8 | 114 | `movel #0x1A0B8,%fp@(36)` at 0x19F2E — object $24 again |

Object $24 is the anim script pointer in the struct map, so three of the
five are animation scripts installed into a field that has never held
code. That is a consuming instruction, not a plausibility filter.

## What the audit had wrong before this

Three entries — 0x061D6, 0x06396, 0x16BF0 — read as defects until the
tool's own bug was found. **objdump wraps an instruction longer than six
bytes onto a second line carrying an address and bytes but no mnemonic.**
Dropping those lines makes every `movel #next,%fp@(2)` — the state
machine's exit idiom and the last instruction of many object routines —
measure two bytes short, so the fall-through test looks at the wrong
address. All three install the function that physically follows them and
are correctly bounded.
