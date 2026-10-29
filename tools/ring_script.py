#!/usr/bin/env python3
"""Extract a System 16B colour-CYCLER ring script from a 68K program image
and report the PEN DEMAND its slots really need.

Why this exists (2026-09-25): mdpen_bake.py bakes each colour set's pen map
from ONE anchor frame. For a set driven by the game's colour cycler that is
wrong by construction -- the anchor catches whichever step of the ring was
live at capture time. Altered Beast's transformation cut was anchored one or
two vints before the cycler's first write, when set 19 was still FLAT, so the
baker gave it ONE pen; across the ring's seven steps the set needs THREE
distinct MD levels, and the field collapses to a single blue on screen.

The ring is STATIC DATA in the program ROM, so the true demand is derivable
at bake time with no observation, no runtime re-claim, and no capture: take
the UNION of every step's colours, quantise, and count.

Layout, derived from altbeast (US set 8) and verified byte-for-byte:

    installer 0x1A6BA   runs at scene setup; zeroes 32 longwords of the
                        slot table at 0xFFF300 (16 8-byte entries) and walks
                        the script below.
    script    0x1A70C   count word N, then N x 6-byte (slot word, ring
                        pointer long) pairs at 0x1A70E.
    ring      <ptr>     step-count word, then that many 18-byte steps:
                        a 2-byte timer then 8 colour words = 8 pens.

Colour words are decoded with mdpen_bake.quant() -- the SAME function the
baker uses -- so the pen counts here are directly comparable to its output.

PROVENANCE MATTERS AND BIT US ONCE (2026-09-26). altbeast.zip holds nine
program sets. The port ships SET 8, the parent pair epr-11907.a7 (high) +
epr-11906.a5 (low), which is what tools/patch_game.py targets and what
roms/<game>/prog68k.bin already contains. Set 4 (altbeast4) carries the SAME
cycler data shifted by exactly 0x12 -- one ring step -- so a wrong-set read
yields the right pen demand at the wrong addresses and nothing complains. So:
DEFAULT TO prog68k.bin, the image the rest of the toolchain uses. --zip is for
a title that has no prog68k.bin yet, and it prefers the parent set.

    set 8 (SHIPPED)  script 0x01A6FA  rings 0x01A70E (s19) 0x01A78E (s20/21)
    set 4            script 0x01A70C  rings 0x01A720      0x01A7A0

Usage:
    tools/ring_script.py                          (roms/altbeast/prog68k.bin)
    tools/ring_script.py <prog.bin> [--script 0x1A6FA]
    tools/ring_script.py --zip mame/altbeast.zip  (parent set; last resort)
"""
import argparse, struct, sys, zipfile

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from mdpen_bake import quant                  # single source of truth


def interleave(hi: bytes, lo: bytes) -> bytes:
    out = bytearray()
    for i in range(len(hi)):
        out.append(hi[i]); out.append(lo[i])
    return bytes(out)


def program_from_zip(path: str) -> bytes:
    """The S16B program is an even/odd EPROM pair. Pick the pair whose
    interleaving yields a sane reset vector rather than hardcoding names."""
    z = zipfile.ZipFile(path)
    # Skip any set shipping an FD1094 .key: those programs are ENCRYPTED and
    # this repo cannot decrypt them (CLAUDE.md scope). Altered Beast US set 8
    # is a flat unencrypted set, which is why it is the port's base.
    enc = {n.rsplit("/", 1)[0] for n in z.namelist() if n.endswith(".key")}
    big = {n: z.read(n) for n in z.namelist()
           if z.getinfo(n).file_size >= 0x20000
           and n.rsplit("/", 1)[0] not in enc}
    # PARENT SET FIRST: members with no '/' are the parent (set 8 for altbeast,
    # the set patch_game.py targets). A clone subdirectory is only a fallback,
    # because clones shift the data tables (see the header).
    names = sorted(big, key=lambda n: ("/" in n, n))
    for a in names:
        for b in names:
            if a >= b:
                continue
            for hi, lo in ((a, b), (b, a)):
                img = interleave(big[hi], big[lo])
                pc = struct.unpack(">I", img[4:8])[0]
                if pc and pc < len(img) and not pc & 1:
                    if struct.unpack(">H", img[pc:pc + 2])[0] in (0x6000, 0x4EF9):
                        print(f"# program = {hi} (high) + {lo} (low), "
                              f"reset -> 0x{pc:06X}")
                        return img
    sys.exit("could not identify the program pair in " + path)


def demand(img, script_addr=0x1A70C):
    """Per-pixel pen demand of every colour-cycled slot in the script.

    -> {slot: {'ring': addr, 'steps': n, 'cls': [class per pixel 0..7],
               'c0': [step-0 quantised colour per class]}}

    THE PEN COUNT IS NOT THE COLOUR COUNT. Two pixels may share an MD pen
    only if they carry the same colour at EVERY step -- a shared pen holds
    one colour at a time, so a pair that ever disagrees renders one of them
    wrong. The demand is the number of distinct per-pixel colour SEQUENCES.
    For a rotation every pixel visits the same colours in a different phase,
    so the union is small (slot 19: 4 MD levels) while the demand is 8.

    'ring' is returned because slots driven by the SAME ring pointer carry
    identical sequences and may share one allocation (altbeast 20/21). That
    sharing is keyed on the ring address, NOT the slot: keying per slot
    would cost 8+7+7=22 pens and overflow the line.
    """
    be16 = lambda o: struct.unpack(">H", img[o:o + 2])[0]
    be32 = lambda o: struct.unpack(">I", img[o:o + 4])[0]
    n = be16(script_addr)
    if not 1 <= n <= 64:
        raise ValueError(f"0x{script_addr:06X}: count word {n} implausible")
    out = {}
    for k in range(n):
        e = script_addr + 2 + k * 6
        slot, ptr = be16(e), be32(e + 2) & (len(img) - 1)
        steps = be16(ptr)
        if not 1 <= steps <= 64:
            continue                       # format differs for this slot
        seqs = [tuple(quant(be16(ptr + 2 + s * 18 + 2 + i * 2))
                      for s in range(steps)) for i in range(8)]
        classes = []
        for q in seqs:
            if q not in classes:
                classes.append(q)
        out[slot] = {'ring': ptr, 'steps': steps,
                     'cls': [classes.index(q) for q in seqs],
                     'c0': [c[0] for c in classes],
                     # RAW ring words per pixel, in step order. A caller that
                     # has its own quantiser (tools/bake_tilecram.py md())
                     # should build the classes from these, so the pen keys
                     # are in ITS colour space and cannot disagree with the
                     # table it emits.
                     'seqw': [tuple(be16(ptr + 2 + st * 18 + 2 + i * 2)
                                    for st in range(steps)) for i in range(8)]}
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("prog", nargs="?")
    ap.add_argument("--zip")
    ap.add_argument("--script", type=lambda s: int(s, 0), default=0x1A6FA,
                    help="address of the script COUNT word (altbeast SET 8, the "
                         "shipped set; set 4 has it at 0x1A70C)")
    a = ap.parse_args()
    default_prog = __file__.rsplit("/", 2)[0] + "/roms/altbeast/prog68k.bin"
    img = (program_from_zip(a.zip) if a.zip
           else open(a.prog or default_prog, "rb").read())

    be16 = lambda o: struct.unpack(">H", img[o:o + 2])[0]
    be32 = lambda o: struct.unpack(">I", img[o:o + 4])[0]

    n = be16(a.script)
    if not 1 <= n <= 64:
        sys.exit(f"0x{a.script:06X}: count word {n} implausible -- wrong address?")
    print(f"script 0x{a.script:06X}: {n} cycled slots")

    for k in range(n):
        e = a.script + 2 + k * 6
        slot, ptr = be16(e), be32(e + 2) & (len(img) - 1)
        steps = be16(ptr)
        print(f"\nslot {slot}  ring 0x{ptr:06X}  {steps} steps")
        if not 1 <= steps <= 64:
            print("  step count implausible -- format differs for this slot")
            continue
        union, per_pen = [], [[] for _ in range(8)]
        for s in range(steps):
            b = ptr + 2 + s * 18
            cols = [be16(b + 2 + i * 2) for i in range(8)]
            print(f"  step {s}: timer {be16(b):3d}  " +
                  " ".join(f"{c:04X}" for c in cols))
            for i, c in enumerate(cols):
                if c not in union:
                    union.append(c)
                if quant(c) not in per_pen[i]:
                    per_pen[i].append(quant(c))
        # THE PEN COUNT IS NOT THE COLOUR COUNT (corrected 2026-09-25 23:05).
        # Two pixels may share an MD pen only if they carry the same colour at
        # EVERY step -- a shared pen has one colour at a time, so a pixel pair
        # that ever disagrees renders one of them wrong. The demand is
        # therefore the number of distinct per-pixel colour SEQUENCES. For a
        # rotation like slot 19 every pixel visits a different phase, so the
        # union is small (4 levels) while the true demand is 8 pens.
        seqs = [tuple(quant(be16(ptr + 2 + s * 18 + 2 + i * 2))
                      for s in range(steps)) for i in range(8)]
        classes = []
        for q in seqs:
            if q not in classes:
                classes.append(q)
        qs = sorted({q for seq in seqs for q in seq})
        print(f"  union {len(union)} colour words / {len(qs)} distinct MD colours")
        print(f"  PENS NEEDED = {len(classes)} distinct pixel sequences")
        print(f"    pixel -> pen class: {[classes.index(q) for q in seqs]}")
        print(f"    pixels constant across the ring: "
              f"{[i for i in range(8) if len(set(seqs[i])) == 1]}")
        print("    " + " ".join(f"{c:04X}" for c in union))


if __name__ == "__main__":
    main()
