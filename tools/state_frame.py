#!/usr/bin/env python3
"""Reconstruct the displayed frame from an ares .bs1 savestate, offline.
    python3 tools/state_frame.py STATE.bs1 [--out frame.png] [--43]
                                 [--report]

Decodes the Mega Drive planes (VRAM name tables + patterns, VSRAM,
hscroll table, CRAM) and composites the 32X framebuffer bank the
hardware is showing (32X CRAM) over them, the way the display does under
MARS_VDP_PRIO_32X: any non-zero 32X pixel wins, pixel 0 is MD-through,
plane A over plane B, pen 0 transparent, backdrop = CRAM entry 0.

--report prints the palette-pack and slot-map facts the STATIC-SCENE arc
gates on (docs/design/STATIC-SCENE.md): per-set MD line / pen map /
fallback count, name-table cells whose line no longer matches their
set, tile codes referenced but not resident, and the count of slots the
planes actually use.

State layout (this ares-debug build; memory note ares-state-layout):
  SDRAM +0x23B (word-swapped), CPU RAM found by the pal_thunks code,
  VRAM found by the md_sprart bytes at VRAM 0x8040, VSRAM 0x50 before
  the CRAM, CRAM found by the SH-2's own mdp_line_c line-1 pens, 32X
  DRAM found by its line table, 32X CRAM right after DRAM.
Every anchor is found by CONTENT, never by a fixed offset, because a
variable-length item before the VDP block shifts the CRAM between
states (measured 0x20 once).
"""
import argparse, struct, sys, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD = 0x23B
# SDRAM tables (sh_src/m_main.c #defines; SDRAM-relative)
DIAG, PAL_SH = 0x28000, 0x27000
MD_TAG, MDP_LINE_C, MDP_S_LINE, MDP_S_MAP, MDP_S_USED = 0x3B400, 0x3C400, 0x3C4A0, 0x3C520, 0x3E380


def swap(b):
    return bytes(x for i in range(0, len(b) & ~1, 2) for x in (b[i + 1], b[i]))


def find_all(hay, needle, start=0):
    p = hay.find(needle, start)
    while p >= 0:
        yield p
        p = hay.find(needle, p + 1)


class State:
    def __init__(self, path):
        self.raw = raw = open(path, 'rb').read()
        self.sdram = swap(raw[SD:SD + 0x40000])
        # VRAM: md_sprart.bin bytes 0x40.. sit at VRAM 0x8040 (word-swapped in the state)
        art = open(os.path.join(ROOT, 'sh_src', 'md_sprart.bin'), 'rb').read()
        hits = list(find_all(raw, swap(art[0x40:0x60])))
        if len(hits) != 1:
            sys.exit(f'VRAM anchor: expected one md_sprart hit, got {hits}')
        self.vram = swap(raw[hits[0] - 0x8040: hits[0] - 0x8040 + 0x10000])
        # MD CRAM: the SH-2's line-1 pens 2..7 (mdp_line_c) appear in the CRAM as LE u16
        lc = struct.unpack_from('>48H', self.sdram, MDP_LINE_C)
        key = b''.join(struct.pack('<H', v) for v in lc[2:8])
        hits = [h for h in find_all(raw, key) if h > SD + 0x40000]
        if len(hits) != 1:
            sys.exit(f'CRAM anchor: expected one MD-side hit, got {hits}')
        c0 = hits[0] - 4 - 32
        self.cram = struct.unpack_from('<64H', raw, c0)
        self.vsram = struct.unpack_from('>40H', raw, c0 - 0x50)   # big-endian words
        # 32X DRAM: line table 0x100,0x1A0,0x240,... LE u16 at DRAM+0
        key = b''.join(struct.pack('<H', 0x100 + i * 0xA0) for i in range(4))
        hits = list(find_all(raw, key))
        if len(hits) != 2 or hits[1] - hits[0] != 0x20000:
            sys.exit(f'DRAM anchor: expected two line tables 0x20000 apart, got {hits}')
        self.dram = hits[0]
        self.x32cram = struct.unpack_from('<256H', raw, self.dram + 0x80000)
        tail = raw[self.dram + 0x80000 + 0x200:]
        self.fb_select = tail[15]           # mode,lines,prio,dotshift,latch*4,afl,afa(2),afd(2),access,active,select
        self.shown_half = 0 if self.fb_select == 1 else 1
        # CPU RAM: the palette thunk code at 0xFFBA08
        key = b''.join(struct.pack('<H', w) for w in (0x2F01, 0x2F09, 0x3200, 0xE649, 0x43F8, 0xBA00))
        hits = list(find_all(raw, key))
        self.wram = swap(raw[hits[0] - 0xBA08: hits[0] - 0xBA08 + 0x10000]) if hits else None

    # ---- MD side ----
    def cell(self, nt, row, col):
        o = nt + (row & 31) * 128 + (col & 63) * 2
        w = (self.vram[o] << 8) | self.vram[o + 1]
        return w & 0x7FF, (w >> 13) & 3, (w >> 11) & 1, (w >> 12) & 1, w >> 15

    def pen(self, slot, hf, vf, x, y):
        xx = 7 - x if hf else x
        yy = 7 - y if vf else y
        b = self.vram[slot * 32 + yy * 4 + xx // 2]
        return (b >> 4) if xx % 2 == 0 else (b & 15)

    def hscroll(self, y):
        o = 0xFC00 + (y // 8) * 32
        return [((self.vram[o + k] << 8) | self.vram[o + k + 1]) & 0x3FF for k in (0, 2)]

    @staticmethod
    def mdrgb(v):
        return ((v & 7) * 36, ((v >> 3) & 7) * 36, ((v >> 6) & 7) * 36)

    @staticmethod
    def x32rgb(v):
        return ((v & 31) * 8, ((v >> 5) & 31) * 8, ((v >> 10) & 31) * 8)

    def frame(self):
        img = Image.new('RGB', (320, 224))
        px = img.load()
        fb = self.raw[self.dram + self.shown_half * 0x20000 + 0x200:][:224 * 320]
        vs = [self.vsram[0] & 0x3FF, self.vsram[1] & 0x3FF]
        back = self.mdrgb(self.cram[0])
        for y in range(224):
            hs = self.hscroll(y)
            for x in range(320):
                p = fb[y * 320 + (x ^ 1)]
                if p:
                    px[x, y] = self.x32rgb(self.x32cram[p])
                    continue
                col = None
                for pl, nt in ((0, 0xC000), (1, 0xE000)):
                    sx = (x - hs[pl]) & 0x1FF
                    sy = (y + vs[pl]) & 0xFF
                    slot, pal, hf, vf, _ = self.cell(nt, sy // 8, sx // 8)
                    pn = self.pen(slot, hf, vf, sx % 8, sy % 8)
                    if pn:
                        col = self.mdrgb(self.cram[pal * 16 + pn])
                        break
                px[x, y] = col if col else back
        return img

    # ---- report ----
    def report(self):
        m = self.sdram
        diag = struct.unpack_from('>64I', m, DIAG)
        tags = struct.unpack_from('>1024I', m, MD_TAG)
        s_line = m[MDP_S_LINE:MDP_S_LINE + 128]
        s_map = m[MDP_S_MAP:MDP_S_MAP + 1024]
        used = m[MDP_S_USED:MDP_S_USED + 128]
        lc = struct.unpack_from('>48H', m, MDP_LINE_C)
        print(f'BUILD 0x{diag[18]:08x}  shown 32X bank {self.shown_half}  vsram A/B {self.vsram[0]},{self.vsram[1]}')
        print(f'nearest-colour fallbacks DIAG[36]={diag[36]}  set assigns DIAG[35]={diag[35]}  frees DIAG[37]={diag[37]}')
        live = sum(1 for t in tags if t != 0xFFFFFFFF)
        ref = set()
        stale = 0
        for nt in (0xC000, 0xE000):
            for row in range(32):
                for col in range(64):
                    slot, pal, _, _, _ = self.cell(nt, row, col)
                    if slot >= 1023 or not any(self.vram[slot * 32:slot * 32 + 32]):
                        continue
                    ref.add(slot)
                    t = tags[slot]
                    if t != 0xFFFFFFFF and s_line[(t >> 16) & 0x7F] and pal != s_line[(t >> 16) & 0x7F]:
                        stale += 1
        print(f'slot map: {live} tags of 1024; planes reference {len(ref)} slots; stale-line cells {stale}')
        for l in range(3):
            occ = sum(1 for p in range(1, 16) if lc[l * 16 + p] != 0xFFFF)
            print(f'MD line {l + 1}: {occ}/15 pens ' + ' '.join('%03x' % v if v != 0xFFFF else '---' for v in lc[l * 16 + 1:l * 16 + 16]))
        def s16q(w):
            r = ((w & 0xF) << 1) | ((w >> 12) & 1)
            g = (((w >> 4) & 0xF) << 1) | ((w >> 13) & 1)
            b = (((w >> 8) & 0xF) << 1) | ((w >> 14) & 1)
            return tuple(round(c / 31 * 7) for c in (r, g, b))
        live_sets = sorted({(t >> 16) & 0x7F for t in tags if t != 0xFFFFFFFF})
        print('set  line  used  exact/fallback pixels')
        for s in live_sets:
            if not s_line[s]:
                print(f'{s:#04x}  --    {used[s]:#04x}  UNASSIGNED')
                continue
            pal = struct.unpack_from('>8H', m, PAL_SH + s * 16)
            ex = fb = 0
            for p in range(8):
                if not used[s] & (1 << p):
                    continue
                q = s16q(pal[p])
                got = lc[(s_line[s] - 1) * 16 + s_map[s * 8 + p]]
                if (got & 7, (got >> 3) & 7, (got >> 6) & 7) == q:
                    ex += 1
                else:
                    fb += 1
            flag = '' if fb == 0 else '  <- FALLBACK'
            print(f'{s:#04x}  {s_line[s]}     {used[s]:#04x}  {ex}/{fb}{flag}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('state')
    ap.add_argument('--out')
    ap.add_argument('--43', dest='four3', action='store_true', help='also write a 1600x1200 nearest-neighbour copy')
    ap.add_argument('--report', action='store_true')
    a = ap.parse_args()
    st = State(a.state)
    if a.report:
        st.report()
    out = a.out or os.path.splitext(a.state)[0] + '_frame.png'
    img = st.frame()
    img.save(out)
    print('frame ->', out)
    if a.four3:
        p43 = out[:-4] + '_43.png'
        img.resize((1600, 1200), Image.NEAREST).save(p43)
        print('4:3   ->', p43)


if __name__ == '__main__':
    main()
