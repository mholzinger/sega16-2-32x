#!/usr/bin/env python3
"""Ares bs9 offline audit: MD VRAM planes vs md_dbg_nt mirror, TEXT_U,
sbuf render, snap regs, staged VDP records.

Usage: bs9_audit.py <state.bs9> [--sbuf ADDR] [--snap ADDR] [--png OUT]

ADDRs are the FLAVOR-MATCHED .bss addresses from the lst of the binary
the state was played on (grep ' _sbuf$\\| _snap$' rom/s16.lst) — never
carry them across flavors.

VRAM BASE IS ANCHORED ON MIRROR CONTENT, not fingerprints: the file
offset of md_dbg_nt row bytes minus that row's VRAM offset gives the
base. (The margin-0x03FF fingerprint locked onto the +0x2000 alias
TWICE — see LOOP13 2026-08-15. Mirror[0] pairs with plane B (0xE000),
mirror[1] with plane A (0xC000).)
"""
import argparse
import struct
import sys
import zlib

SD = 0x23B                    # SDRAM offset in the BST1 container
MDNT = 0x3D200                # md_dbg_nt fixed block [2][28][40]
TEXT = 0x26000                # TEXT_U SDRAM offset
CRAM = 0x28900                # cram_mirror
W, H = 336, 240

ap = argparse.ArgumentParser()
ap.add_argument("state")
ap.add_argument("--sbuf", type=lambda x: int(x, 16), default=None)
ap.add_argument("--snap", type=lambda x: int(x, 16), default=None)
ap.add_argument("--png", default=None)
args = ap.parse_args()

st = open(args.state, "rb").read()


def swap16(b):
    return b"".join(b[i + 1:i + 2] + b[i:i + 1] for i in range(0, len(b), 2))


def rd(off, n):
    lo = off & ~1
    hi = (off + n + 1) & ~1
    return swap16(st[SD + lo:SD + hi])[off - lo:off - lo + n]


def r16(off):
    return struct.unpack(">H", rd(off, 2))[0]


def mirror_row(plane, row):
    return [r16(MDNT + ((plane * 28 + row) * 40 + c) * 2) for c in range(40)]


# ---- VRAM base by mirror-content anchor (mirror[1] -> plane A 0xC000) ----
base = None
for row in (10, 6, 14, 20):
    needle = b"".join(struct.pack("<H", w) for w in mirror_row(1, row))
    if len(set(mirror_row(1, row))) < 4:
        continue                      # too uniform: risks false hits
    j = st.find(needle)
    hits = []
    while j >= 0:
        hits.append(j)
        j = st.find(needle, j + 1)
    # the mirror itself also matches; the VRAM copy is the OTHER hit
    for h in hits:
        cand = h - (0xC000 + row * 128)
        if cand > 0x40000:            # past SDRAM = plausible VRAM chunk
            base = cand
            break
    if base:
        print(f"VRAM base 0x{base:X} (anchored on mirror[1] row {row})")
        break
if base is None:
    sys.exit("no VRAM base found — mirror rows too uniform or state empty")


def vw(off):
    return struct.unpack("<H", st[base + off:base + off + 2])[0]


# ---- plane windows vs mirrors, margins ----
for plane, nt, nm in ((0, 0xE000, "B"), (1, 0xC000, "A")):
    diff = sum(1 for r in range(28) for c in range(40)
               if vw(nt + r * 128 + c * 2) != mirror_row(plane, r)[c])
    bad = sum(1 for r in range(28) for c in range(40, 64)
              if vw(nt + r * 128 + c * 2) != 0x03FF)
    print(f"plane {nm} (0x{nt:X}): window diffs {diff}/1120, "
          f"margin non-03FF {bad}/672")

# ---- TEXT_U nonzero map ----
words = struct.unpack(">2048H", rd(TEXT, 4096))
for row in range(32):
    cells = words[row * 64:(row + 1) * 64]
    nz = [(c, w) for c, w in enumerate(cells) if w]
    if nz:
        s = " ".join(f"{c}:{w:04x}" for c, w in nz[:12])
        print(f"TEXT row {row:2d} ({len(nz):2d}): {s}"
              + (" ..." if len(nz) > 12 else ""))

# ---- staged VDP records (MD WRAM 0xFFA400) ----
sig = swap16(bytes.fromhex("4a38c02050f8c0204e75"))
i = st.find(sig)
if i >= 0:
    md = i - 0xB380
    buf = swap16(st[md + 0xA400:md + 0xA800])
    off = 16
    print("staged records:")
    for n in range(16):
        if off + 6 > len(buf):
            break
        wlen, chi, clo = struct.unpack(">3H", buf[off:off + 6])
        if wlen == 0 or wlen > 300:
            break
        addr = ((chi & 0x3FFF)) | ((clo & 3) << 14)
        kind = "CRAM" if (chi >> 14) == 3 else "VRAM"
        print(f"  rec{n}: wlen={wlen} ctrl={chi:04x}{clo:04x} "
              f"addr=0x{addr:04X} {kind}")
        off += 6 + wlen * 2

# ---- snap regs ----
if args.snap:
    for which in range(2):
        b2 = (args.snap - 0x06000000) + which * 84
        pq = list(rd(b2, 4))
        vx0 = struct.unpack(">i", rd(b2 + 4, 4))[0]
        vy0 = struct.unpack(">i", rd(b2 + 8, 4))[0]
        print(f"snap[{which}]: pq={pq} vx0={vx0} (xf={vx0 & 7}) "
              f"vy0={vy0} (yf={vy0 & 7})")

# ---- sbuf render through cram_mirror ----
if args.sbuf and args.png:
    sb = rd(args.sbuf - 0x06000000, W * H)
    cram = [struct.unpack(">H", rd(CRAM + 2 * i, 2))[0] for i in range(256)]

    def rgb(c):
        return bytes((((c) & 0x1F) << 3, ((c >> 5) & 0x1F) << 3,
                      ((c >> 10) & 0x1F) << 3))

    pal = [rgb(c) for c in cram]
    rows = [b"\x00" + b"".join(pal[sb[y * W + x]] for x in range(W))
            for y in range(H)]
    raw = zlib.compress(b"".join(rows), 6)

    def ch(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I",
                                                           zlib.crc32(c))

    png = (b"\x89PNG\r\n\x1a\n"
           + ch(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
           + ch(b"IDAT", raw) + ch(b"IEND", b""))
    open(args.png, "wb").write(png)
    print(f"sbuf -> {args.png}")
