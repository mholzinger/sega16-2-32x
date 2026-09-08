#!/usr/bin/env python3
"""R60/E4: offline FADE-STABLE tile palette pack analysis (LOOP19's
untested lever — tiles hold 19-20 of the 32 CRAM groups, but section 11
said only ~10 distinct tile palettes exist at an instant; runtime dedup
(TILEDEDUP) died on comparison cost, so this asks what a STATIC table
could do).

Input: the corpus from tools/palharvest_tiles_ares.py (or the lua twin):
  T f scene n  cc,mm,gg,w0..w7 ...   per live tile set: colour set cc,
                                     pixel-usage mask mm, group gg,
                                     8 palette words
  G f k0,...,k31                     grp_key snapshot

Three sharing strategies, strictest first:

  PER-CYCLE FLOOR   distinct full 8-colour rows among the live sets at
                    each cycle — what a clairvoyant per-instant packer
                    would need.  Lower bound, not implementable as a
                    static table.
  FADE-STABLE FULL  two sets share a class only if their full 8-colour
                    rows are IDENTICAL in EVERY sampled cycle where
                    either is live (LOOP19: runtime sharing died because
                    fades split shared groups — this criterion is immune
                    by construction: the rows never differ, so any fade
                    writes both identically).  True equivalence, so the
                    static table is just set -> class id.
  + USED-PEN MERGE  greedy merge of fade-stable classes whose pens never
                    collide: for every cycle where classes A,B are both
                    live, and every pen v lit in both pixel-usage masks,
                    rowA[v] == rowB[v].  (mdp_s_used accumulates at
                    claim time; the max mask over the run is used —
                    conservative.)  Not an equivalence — verified
                    pairwise against every member after each merge.

Reported for each: the WORST-CYCLE demand (max over cycles of distinct
classes among live sets) — that is the true minimum tile group count.

  python3 tools/palpack_tiles.py /tmp/palharvest_t.txt
"""
import sys
from collections import defaultdict


def parse(path):
    cycles = []          # (frame, scene, {set: (mask, group, row8)})
    for ln in open(path):
        p = ln.split()
        if not p or p[0] != 'T':
            continue
        f, scene, n = int(p[1]), int(p[2], 16), int(p[3])
        ent = {}
        for e in p[4:]:
            fld = e.split(',')
            c, m, g = int(fld[0], 16), int(fld[1], 16), int(fld[2], 16)
            row = tuple(int(w, 16) for w in fld[3:11])
            ent[c] = (m, g, row)
        assert len(ent) == n
        cycles.append((f, scene, ent))
    return cycles


def main():
    cycles = parse(sys.argv[1])
    sets = sorted({c for _, _, ent in cycles for c in ent})
    live_rows = {c: {} for c in sets}      # set -> {frame: row}
    mask = defaultdict(int)                # set -> max pixel-usage mask
    groups_used = []
    for f, scene, ent in cycles:
        for c, (m, g, row) in ent.items():
            live_rows[c][f] = row
            mask[c] |= m
        groups_used.append((f, scene, len(ent), len({g for _, g, _ in ent.values()})))

    print("corpus: %d cycles, %d distinct tile colour sets ever live"
          % (len(cycles), len(sets)))
    worst = max(groups_used, key=lambda t: t[2])
    print("live sets per cycle: min %d  max %d (frame %d, scene %02X)"
          % (min(t[2] for t in groups_used), worst[2], worst[0], worst[1]))
    print("groups actually held per cycle (tile_grp targets): max %d"
          % max(t[3] for t in groups_used))

    # ---- per-cycle floor -------------------------------------------------
    floor = []
    for f, scene, ent in cycles:
        floor.append((len({row for _, _, row in ent.values()}), f, scene))
    fmax = max(floor)
    print("\nPER-CYCLE FLOOR (distinct rows among live sets):")
    print("  worst cycle: %d distinct rows (frame %d, scene %02X)"
          % (fmax[0], fmax[1], fmax[2]))

    # ---- fade-stable full-row classes -----------------------------------
    # A ~ B iff rows identical at every cycle where EITHER is live.  A row
    # is readable at every cycle (PAL_SH persists), so extend each set's
    # signature over the union of live cycles pairwise.  For transitivity
    # use the signature over ALL cycles where the set itself is live, and
    # verify the union rule pairwise inside each class afterwards.
    def compatible(a, b):
        fa, fb = live_rows[a], live_rows[b]
        for f in set(fa) | set(fb):
            ra, rb = fa.get(f), fb.get(f)
            if ra is not None and rb is not None and ra != rb:
                return False
            # one live, one not: the non-live one's PAL_SH row was not
            # captured; treat as compatible (it is not rendered then, and
            # apply_cram paints from the OWNER's slot which is the live one)
        return True

    classes = []           # list of [members]
    for c in sets:
        placed = False
        for cl in classes:
            if all(compatible(c, m) for m in cl):
                cl.append(c)
                placed = True
                break
        if not placed:
            classes.append([c])
    cls_of = {c: i for i, cl in enumerate(classes) for c in cl}
    fs = []
    for f, scene, ent in cycles:
        fs.append((len({cls_of[c] for c in ent}), f, scene))
    fsmax = max(fs)
    print("\nFADE-STABLE FULL-ROW classes (share iff identical every co-live cycle):")
    print("  global classes: %d (of %d sets)" % (len(classes), len(sets)))
    print("  worst cycle: %d classes live (frame %d, scene %02X)"
          % (fsmax[0], fsmax[1], fsmax[2]))
    big = sorted(classes, key=len, reverse=True)[:6]
    for cl in big:
        if len(cl) > 1:
            print("    class %s: sets %s" % (cls_of[cl[0]],
                  " ".join("%02X" % c for c in sorted(cl))))

    # ---- used-pen merge --------------------------------------------------
    def pen_ok(a, b):
        both = mask[a] & mask[b]
        if both == 0:
            return True
        fa, fb = live_rows[a], live_rows[b]
        for f in set(fa) & set(fb):
            ra, rb = fa[f], fb[f]
            for v in range(8):
                if (both >> v) & 1 and ra[v] != rb[v]:
                    return False
        return True

    merged = [list(cl) for cl in classes]
    changed = True
    while changed:
        changed = False
        for i in range(len(merged)):
            for j in range(len(merged) - 1, i, -1):
                if all(pen_ok(a, b) for a in merged[i] for b in merged[j]):
                    merged[i] += merged[j]
                    del merged[j]
                    changed = True
    mrg_of = {c: i for i, cl in enumerate(merged) for c in cl}
    ms = []
    for f, scene, ent in cycles:
        ms.append((len({mrg_of[c] for c in ent}), f, scene))
    msmax = max(ms)
    print("\n+ USED-PEN MERGE (pens that never collide share a class):")
    print("  global classes: %d" % len(merged))
    print("  worst cycle: %d classes live (frame %d, scene %02X)"
          % (msmax[0], msmax[1], msmax[2]))

    # ---- emit the ROM table (--emit path.h) ------------------------------
    # Fade-stable classes ONLY (not pen-merged): full-row identity at every
    # co-live cycle is airtight for a shared CRAM group; pen-merge depends
    # on usage masks and is not worth the risk for 2 groups of margin.
    if len(sys.argv) > 3 and sys.argv[2] == '--emit':
        out = sys.argv[3]
        rep = []
        for cl in classes:
            rep.append(max(cl, key=lambda c: len(live_rows[c])))
        tbl = [0xFF] * 128
        for i, cl in enumerate(classes):
            for c in cl:
                tbl[c] = i
        with open(out, 'w') as fo:
            fo.write("/* GENERATED by tools/palpack_tiles.py --emit — do not edit.\n"
                     " * Fade-stable tile palette classes from the harvest corpus\n"
                     " * (%d cycles, %d sets -> %d classes; see docs/design/REBUILD.md 2026-08-25).\n"
                     " * tile_class_rom[c]: S16 tile colour set -> class id, 0xFF =\n"
                     " * never observed -> dynamic fallback path. Class k owns CRAM\n"
                     " * group 1+k; grp_key repointed to a LIVE member per cycle so\n"
                     " * apply_cram always paints from a live row. */\n"
                     % (len(cycles), len(sets), len(classes)))
            fo.write("#define TILE_NCLASS %d\n" % len(classes))
            fo.write("static const unsigned char tile_class_rom[128] = {\n")
            for r in range(0, 128, 16):
                fo.write("    " + ",".join("0x%02X" % v for v in tbl[r:r+16]) + ",\n")
            fo.write("};\n")
            fo.write("static const unsigned char tile_class_rep[TILE_NCLASS] = {\n    "
                     + ",".join("0x%02X" % v for v in rep) + ",\n};\n")
        print("\nemitted %s: %d classes" % (out, len(classes)))

    # demand timeline (fade-stable), for the write-up
    print("\ntimeline (frame: live sets -> fade-stable classes -> pen-merged):")
    for (f, scene, ent), (nf, _, _), (nm, _, _) in zip(cycles, fs, ms):
        print("  %5d  %02X  %2d -> %2d -> %2d"
              % (f, scene, len(ent), nf, nm))


if __name__ == "__main__":
    main()
