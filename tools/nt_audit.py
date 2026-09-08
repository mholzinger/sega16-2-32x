#!/usr/bin/env python3
"""nt_audit: replay the MD nt-builder against the dumped SDRAM truth
and diff per cell against the shipped mirror (LOOP14 item 3).

Input: a directory from tools/nt_dump.lua (snap.bin ntmir.bin mdtag.bin
sline.bin tmap.bin). For each plane and 8-row band it recomputes the
expected (code,cset) per cell using EXACTLY the m_main.c nt-walk rules
(rowscroll bit15 -> alt page/scroll set, primary-xscroll bit15 ->
rowscroll word is the band's xscroll, coarse vx/vy, page-quad select),
then classifies each shipped mirror entry:
  ok         tag(slot) == expected key
  blank      shipped the blank slot
  stale      tag(slot) != expected key  (residue: cell names old art)
  freeslot   shipped slot is unclaimed (tag 0xFFFFFFFF)
Prints per-row totals and a sample of stale cells with both keys.
"""
import struct, sys

def be16(b, o): return struct.unpack_from('>H', b, o)[0]
def be32(b, o): return struct.unpack_from('>i', b, o)[0]

class Regs:
    def __init__(self, b, o):
        self.pq    = list(b[o:o+4])
        self.vx0   = be32(b, o+4)
        self.vy0   = be32(b, o+8)
        self.pq_a  = list(b[o+12:o+16])
        self.vx0_a = be32(b, o+16)
        self.vy0_a = be32(b, o+20)
        self.xs_raw = be16(b, o+24)
        self.rs    = [be16(b, o+26+2*i) for i in range(28)]
        self.any_special = b[o+82]

def main(d):
    snap  = open(f'{d}/snap.bin','rb').read()
    ntmir = open(f'{d}/ntmir.bin','rb').read()
    mdtag = open(f'{d}/mdtag.bin','rb').read()
    tmap  = open(f'{d}/tmap.bin','rb').read()
    A, B = Regs(snap, 0), Regs(snap, 84)
    tag = [struct.unpack_from('>I', mdtag, i*4)[0] for i in range(1024)]
    BLANK = 1023
    # bank1: tile bank for codes with bit 12 set — comes from the game's
    # tile-bank register; not in the dump. Report keys modulo bank by
    # masking code to 12 bits on both sides when bit12 differs.
    for isfg, wl, name in ((0, B, 'B'), (1, A, 'A')):
        print(f'--- plane {name} ({"FG cat-0" if isfg else "BG"}) '
              f'vx0={wl.vx0&0x3FF} vy0={wl.vy0&0x1FF} xs_raw={wl.xs_raw:04x} '
              f'any_special={wl.any_special} pq={wl.pq} pq_a={wl.pq_a}')
        tot = {'ok':0,'blank':0,'stale':0,'freeslot':0}
        for row in range(28):
            pqb, vxr, vyr = wl.pq, wl.vx0, wl.vy0
            used_alt = 0
            if wl.any_special:
                rsw = wl.rs[row]
                if rsw & 0x8000:
                    pqb, vxr, vyr = wl.pq_a, wl.vx0_a, wl.vy0_a
                    used_alt = 1
                elif wl.xs_raw & 0x8000:
                    vxr = (0xC0 - (rsw & 0x3FF)) & 0x3FF
            vx00 = vxr & ~7
            vy = (vyr - (vyr & 7) + row*8) & 0x1FF
            pg0 = pqb[((vy >> 7) & 2)]     * 0x800 + ((vy >> 3) & 0x1F) * 64
            pg1 = pqb[((vy >> 7) & 2) + 1] * 0x800 + ((vy >> 3) & 0x1F) * 64
            rowstat = {'ok':0,'blank':0,'stale':0,'freeslot':0}
            samples = []
            for col in range(40):
                vx = (vx00 + col*8) & 0x3FF
                base = pg1 if (vx >> 9) & 1 else pg0
                w = be16(tmap, (base + ((vx >> 3) & 0x3F)) * 2)
                ent = be16(ntmir, ((1120 if isfg else 0) + row*40 + col) * 2)
                slot, line = ent & 0x1FFF & 0x7FF, ent >> 13
                slot = ent & 0x7FF
                if isfg and (w == 0 or (w & 0x8000)):
                    # builder ships blank for these; anything else is stale
                    k = 'ok' if slot == BLANK else 'stale'
                    rowstat[k] += 1
                    continue
                code = w & 0x1FFF
                cset = (w >> 6) & 0x7F
                if slot == BLANK:
                    rowstat['blank'] += 1
                    continue
                t = tag[slot]
                if t == 0xFFFFFFFF:
                    rowstat['freeslot'] += 1
                    continue
                tcode, tcset = t & 0xFFFF, (t >> 16) & 0x7F
                # bank fold: compare low 12 bits when either has bit12
                same_code = (tcode == code) or \
                    ((code & 0x1000) and (tcode & 0xFFF) == (code & 0xFFF))
                if same_code and tcset == cset and ((t >> 31) & 1) == isfg:
                    rowstat['ok'] += 1
                else:
                    rowstat['stale'] += 1
                    if len(samples) < 3:
                        samples.append((col, f'want c{code:04x}/s{cset:02x}',
                                        f'got c{tcode:04x}/s{tcset:02x}'
                                        f'{"F" if t>>31 else ""} slot={slot}'))
            for k in tot: tot[k] += rowstat[k]
            flag = ' <<' if rowstat['stale'] or rowstat['freeslot'] else ''
            if rowstat['stale'] or rowstat['freeslot'] or rowstat['blank']:
                print(f'  row {row:2d} alt={used_alt} ok={rowstat["ok"]:2d} '
                      f'blank={rowstat["blank"]:2d} stale={rowstat["stale"]:2d} '
                      f'free={rowstat["freeslot"]:2d}{flag} {samples}')
        print(f'  TOTAL {tot}')

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '/tmp/nt_dump')
