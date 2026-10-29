# RING-BAKE — baking a colour-cycled set's pen map from the ring script

**Status 2026-09-25 22:35 builder time: APPLIED (commit 765de95a), and
verified on ares against a control aligned to its own cut.** The derivation
below is correct and carried straight through. Two things in it were wrong in
ways that mattered; both are corrected in place and called out in "What
changed when it was applied" at the end.

**It was applied to `tools/bake_tilecram.py`, NOT `tools/mdpen_bake.py`.**
`MDROUND=1` is on the line, so `sh_src/pal_rounds_md.h` (per-ROUND tables,
written by `bake_tilecram.py --emit-mds`) is what the runtime installs;
`mdpen_bake.py` writes the older per-SCENE header, which `m_main.c:1907`
overrides. The per-scene path took the same change and is inert by default,
but it CANNOT hold the cycler at full demand: its exhaustive
`partition()` reports 51 pens wanted against 45 (3 lines x 15), and fails
loudly, which is the designed behaviour.

## The defect

`mdpen_bake.py` builds every set's pen map from ONE anchor frame
(`bake_scene`, `quant(anchor[s * 8 + p])`). For a set driven by the game's
colour cycler that is wrong by construction: the anchor catches whichever ring
step happened to be live at capture. Altered Beast's transformation cut was
anchored one or two vints before the cycler's first write, while set 19 was
still flat, so all eight pixels deduped onto ONE pen. On screen the ring then
rotates seven blues through those pixels and they all render the same colour.

The builder confirmed the map is baked, not claimed: the rom's table for the
cut's round holds set 19 at line 1, used 0xFF, map `[14,7,7,7,7,7,7,7]` —
byte for byte what live memory reads. That is why five configurations produced
identical tables and why the chevron flag's branches could not help.

## The correct demand, and the trap

`tools/ring_script.py` reads the ring out of the program ROM. The first
instinct — count the distinct colours in the union — is WRONG, and this note
exists partly to record that:

**Two pixels may share an MD pen only if they carry the same colour at EVERY
step of the ring.** A shared pen holds one colour at a time, so a pair that
ever disagrees renders one of them wrong. The demand is the number of distinct
per-pixel colour *sequences*, not the number of distinct colours.

For a rotation the two numbers diverge sharply, because every pixel visits the
same colours in a different phase:

    slot 19   ring 0x01A720, 7 steps, timer 1 every step
              union 8 colour words -> only 4 distinct MD colours
              but 8 DISTINCT PIXEL SEQUENCES -> 8 PENS
              pixel -> pen class = [0,1,2,3,4,5,6,7]  (the identity map)
              pixel 0 alone is constant (7FFF, white)

    slot 20   ring 0x01A7A0, 6 steps, timers 4/2/2/2/2/4
              7 PENS. pixel -> pen class = [0,1,2,3,4,5,6,6]
              pixels 0, 6, 7 constant; 6 and 7 agree, so they share.

    slot 21   THE SAME RING POINTER as slot 20 -> identical demand, and the
              two sets can share one 7-pen allocation losslessly (which is
              what the live refcounts already showed: every pen refcount 2).

**Total for the cut: 8 + 7 shared = 15 pens = exactly one full MD line**
(`NPENS = 15`). The current bake packs 1 + 6 = 7, so this is +8.
`STATIC-SCENE.md` records 9 pens spare on the normal scene's 15/15/6
partition, so it fits with 1 spare — tight, and `partition()` will fail loudly
rather than silently degrade if it does not.

## Patch sketch

A cycled set's pens are PRIVATE: their colours change over time, so they can
never be merged with another set's pen that happens to match at step 0. The
cheapest correct way to get that through the existing `partition()` unchanged
is to give each sequence class its own opaque token, since `partition()` only
ever takes `len()` of a union of hashables.

1. In `ring_script.py`, expose the derivation as a function (it is currently
   only a `__main__` report):

       def demand(img, script_addr):
           """-> {slot: (pen_class_per_pixel, [step0 quantised colour per class])}"""

2. In `mdpen_bake.py`, load it once next to `ALSO`:

       CYC = {}      # --ring: {slot: (classes, step0_colours)}

   and add `--ring <prog.bin>` / `--ring-script <addr>` to `main()`, filling
   `CYC` for the slots the script names. The cycled slots are exactly today's
   `--also` list (19,20,21), so the two options should cross-check and warn if
   they disagree.

3. In `bake_scene`, before the `ALSO` loop, handle cycled sets separately:

       for s, (cls, c0) in CYC.items():
           colsets[s] = frozenset(('cyc', s, k) for k in set(cls))
           masks = dict(masks); masks[s] = 0xFF

   and in the pen-assignment loop, take a cycled set's pen from its class
   rather than from the anchor colour:

       if s in CYC:
           cls, c0 = CYC[s]
           for p in range(8):
               k = cls[p]
               tok = ('cyc', s, k)
               pen = row.index(tok) if tok in row[1:] else row.index(0xFFFF, 1)
               row[pen] = tok
               s_map[s][p] = pen
           # then write the STEP 0 colour into line_c for each class's pen
       else:
           ... existing anchor path ...

   `line_c` must end up holding real 9-bit colours, so replace each `('cyc',
   …)` token with its class's step-0 colour when the row is finalised. The
   runtime repaints those pens as the ring advances; step 0 only has to be
   right for the first frame.

4. Sanity gates worth adding, both cheap:
   - assert every cycled slot named by the script got a line, and fail loudly
     if `partition()` refuses — a silent fallback here is what produced the
     flat field.
   - print the per-slot pen demand next to the existing `lines [...] (N pens
     spare)` line, so a future ring change shows up in the bake log.

## Verdict

A rig capture of the cut on a rom built from the regenerated header. The
failure mode to watch is not colour but MOTION: the field should step through
its seven blues, not sit on one. `tools/presented_fps.py` MOTION, not a still.

## Generality

The script address is the only title-specific input, so this carries to the
next title: `tools/ring_script.py --zip <set>.zip --script <addr>`. It skips
FD1094 sets by their `.key` member — those programs are encrypted and out of
scope for this repo. Finding the installer in a new title is the decompile
thread's job: in Altered Beast it is `0x1A6BA`, which zeroes 32 longwords at
`0xFFF300` (16 8-byte slot entries) and then walks the script at `0x1A70C`.


## What changed when it was applied

Two corrections to the patch sketch above, both load-bearing.

**1. The sharing token must not be keyed on the slot.** The sketch's
`('cyc', s, k)` is per-SLOT, so slots 19, 20 and 21 would have taken 8 + 7 + 7
= 22 pens and overflowed the line — the opposite of the 15 the same note
derives. What is shareable is a RING, not a slot. The applied version drops
the synthetic token altogether and uses **the pixel's ordered quantised
sequence as its own key**: two pixels share a pen exactly when they agree at
every step, which is the correctness rule itself, so slots on one ring produce
identical keys and share for free. Measured after the change: set 19 takes 8
pens, sets 20/21 share 7, round 9's line 0 fills to exactly 15.

**2. Shared ring pens would have FROZEN, and the note did not see it.**
`mds_install` (m_main.c:3745-3750) increments `mdp_pen_rc` once per owning
pixel, so a pen shared by sets 20 and 21 installs at refcount 2 — and the
in-place recolour that tracks a ring fires only at refcount 1 (m_main.c:18138,
"SOLE-OWNER DRIFT"). Sharing the allocation would therefore have pinned those
seven pens at their install-time colour for the life of the screen. That is
not a new hazard: it is exactly the CHEVPEN freeze recorded at m_main.c:3197,
and it explains the observation logged there that every one of sets 20/21's
pens reads `0x0007` — step 0 of their ring is uniform, and nothing repainted
them afterwards.

The bake now emits `mdr_pen_ring[MDROUND_N][48]`, one flag per pen, as its
CERTIFICATE that every co-owner of that pen holds the same colour at every
ring step (true by construction, since the key IS the sequence). The runtime
latches the installed row in `mds_ring_cur` at install and lets the owner
drive the recolour whatever the refcount. Rounds 0-8 emit all-zero rows, so
the relaxation is live only where the cycler is.

## The measurement

`--ring` is not optional: without it the cycled palettes fall back to the
sampled per-pixel keys, and those use `tuple(sorted(colours))` — the SET of
colours a pixel was seen holding. Under a rotation every pixel visits the same
set in a different phase, so all of them produce one identical key and
collapse onto one pen. That, not the anchor frame, is what the shipped table
was doing.

The bots never transform (`tools/boss_census.py`), so the cut is reached
through the ATTRACT demo, which is also where scene 9 was harvested. Our rom's
cut spans attract frames 1459-1571; the control's own cut is at 1456-1568, and
each was screenshot at its OWN frames (the alignment error in LESSONS has been
paid for twice already). Blue shades in the active area, rows 11-234:

    control (shipped tables)   1 shade per frame, all 78,176 px one colour,
                               the whole field strobing between three levels
    ring bake                  3 shades coexisting, pixel counts
                               redistributing every frame

Three is the ceiling the live quantiser allows, not a shortfall: `PAL_QUANT_RAMP`
is off, so `(5bit+2)>>2` maps the ring's seven blues 19,21 -> 5; 23,25 -> 6;
27,29,31 -> 7. Raising that is a separate card and must not ride in this
build — it differs on 16 of 32 inputs and changes colour globally, so neither
change would be attributable.


---

## What this note got wrong (2026-09-25 23:30, all three found by the builder)

The derivation above — the hardware rule, the sequence metric, slot 19 needing
8 pens — held. The engineering did not. Recorded because each error would have
cost a build:

**1. THE WRONG GENERATOR.** This note and LESSONS both named
`tools/mdpen_bake.py` as the fix site. The table the runtime installs for this
scene comes from **`tools/bake_tilecram.py`**. Six tools in `tools/` write a
`*_s_map`; `grep -ln 'mds_s_map\|mdr_s_map' tools/*.py` lists them in one
second, and CLAUDE.md's own rule says to read the implementing file before
theorising about a feature. This thread derived the site instead of grepping
for it. `bake_tilecram.py` was ALREADY keying that scene per pixel — its key
was the *set* of colours a pixel had been seen holding, and under a rotation
every pixel visits the same set in a different phase, so all seven keys matched
and deduped to one pen. The bug was one level subtler than "anchored on one
frame".

**2. THE TOKEN MUST BE KEYED ON THE RING, NOT THE SLOT.** The sketch's
`('cyc', s, k)` includes the slot, so slots 20 and 21 — which share a ring
pointer — would have been given disjoint pens: **22 pens, not 15.** The
shareable unit is a RING, not a slot. Keying on the ordered quantised sequence
itself makes the sharing fall out for free, and makes the key's identity the
correctness condition rather than something enforced alongside it.

**3. THE SHARED PENS WOULD HAVE FROZEN, and this explains CHEVPEN.**
`mds_install` refcounts a pen once per owning pixel, so two sets sharing a pen
land at refcount 2 — and the in-place recolour only fires at refcount 1. Shared
pens therefore never get repainted as the ring advances. **That is the CHEVPEN
freeze**, and it accounts for its own logged symptom: those pens all read
`0x0007`, which is step 0 of their ring. The bake now emits a per-pen
certificate that the co-owners agree at every step, and the runtime honours it.
The sketch above would have shipped the freeze under a new name.

## Result as measured

Each rom screenshotted at its OWN cut (aligning one build to another's frames
has cost this project twice):

    control (shipped tables)   1 blue shade, all 78,176 px — the field strobes
    ring bake                  3 coexisting shades, counts redistributing
                               every frame — motion

Three is the quantiser's ceiling with `PAL_QUANT_RAMP` off, not a shortfall;
two independent hardware captures of the title screen show the same three pure
blues at different phases. Rounds 0-8 have byte-identical tables and zero ring
flags, so gameplay is untouched.

## Reaching the cut

The attract bots never transform, so **the cut is only reachable through the
attract demo**. A recorded playthrough cannot drive it: replaying arcade inputs
into our rom diverges and the round never advances. Let the attract run.

## For O-14

The ring script settles the slot census: the cycler declares **exactly three
slots, 19, 20 and 21**. That bounds O-14's open ask (what share of the rotor's
41 lines those slots are) to a three-slot measurement.


---

## CORRECTION 2026-09-26 13:00 ET — THE 23:30 "CORRECTION" ABOVE WAS ITSELF WRONG

The ring addresses in the ORIGINAL derivation (script 0x1A6FA, rings 0x01A70E
and 0x01A78E) were RIGHT. Last night I "corrected" them to 0x1A70C / 0x01A720 /
0x01A7A0 from an image I had built myself out of `mame/altbeast.zip` — and that
image was **altbeast4, set 4. The port ships SET 8** (the parent pair
epr-11907.a7 high + epr-11906.a5 low), which is what `tools/patch_game.py`
targets and what `roms/altbeast/prog68k.bin` has held all along.

    set 8 (SHIPPED)   script 0x01A6FA   rings 0x01A70E (s19)  0x01A78E (s20/21)
    set 4 (my error)  script 0x01A70C   rings 0x01A720        0x01A7A0

Set 4 carries the same cycler data shifted by exactly 0x12 — ONE RING STEP — so
the wrong set yields the RIGHT pen demand at the WRONG addresses and nothing
complains. Verified field by field: `steps`, `cls`, `c0` and `seqw` are
identical between the two sets, and only the informational `ring` address
differs. **So ring1.32x and its tables are correct** — the emitted bake is
unaffected and no rebuild is needed.

What was actually broken was PROVENANCE, in the build path: `ring_script.py`'s
`program_from_zip` iterated members in sorted order and returned altbeast4
before the parent, and both it and `bake_tilecram.py` defaulted to set 4's
0x1A70C. Consistent with each other, and wrong about the shipped rom. Fixed
2026-09-26: the tool defaults to `roms/<game>/prog68k.bin` (the image the rest
of the toolchain already uses), `--zip` prefers the parent set, and both default
addresses are 0x1A6FA.

I had a verification available and did not use it: `roms/altbeast/prog68k.bin`
has existed since 2026-08-20 and one `cmp` would have caught this before the
"correction" was written.
