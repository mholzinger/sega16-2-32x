#!/usr/bin/env python3
"""WRITE-TAX CENSUS on ares-headless (DISCOVERY-TIMING question 1).
    python3 tools/write_census_ares.py ROM [--frames 4100] [--a 1500] [--b 4100]
                                       [--input CSV] [--out DIR]
Runs the rom under the level-1 input script with the fork's 68K write
hook (`--count-writes lo:hi:name`, added 2026-09-07) on every rebased
hardware region of OUR memory map, plus the thunk code blocks as
instruction-fetch proxies are NOT available on ares (fetches are not
writes), so thunk entries are counted from their dirty-bitmap writes:
    tile thunks  ori.w -> 0xFFB9FE     pal thunks ori/bset -> 0xFFB9FC/0xFFBA00..
Reports per-frame means over [a, b) per region and the top writer PCs
(arcade addresses = pc - 0x900000 for the rebased high copy).
Ranges (tools/patch_game.py remap; SHIP_COMMON has FBTEXT, no FBSPR):
    tileram_fb   0x850000-0x85DFFF   FB staging (32X framebuffer window)
    textram_fb   0x85F000-0x85FFFF   FB (FBTEXT)
    textram_wram 0xFF8000-0xFF8FFF   WRAM (scroll regs, page selects)
    spriteram    0xFF7000-0xFF77FF   WRAM mirror
    palette      0xFF9000-0xFF9FFF   WRAM mirror
    io_bank      0xFFB000-0xFFB04F   mailboxes + tile bank shadow
    tile_dirty   0xFFB9FE-0xFFB9FF   tile thunk bitmap word (1 write = 1 thunk entry)
    pal_dirty    0xFFB9FC-0xFFB9FD   pal thunk bitmap word (PAL32: 8 bytes at 0xFFBA00)
    pal_dirty32  0xFFBA00-0xFFBA07
    fbwin_any    0x840000-0x85FFFF   every framebuffer-window write
    adapter      0xA15100-0xA1518F   32X adapter registers (FM, COMM)
"""
import argparse, csv, os, subprocess, sys, tempfile
ARES = os.path.expanduser('~/src/ares-debug/build_macos/headless-ui/Release/ares-headless')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RANGES = [('tileram_fb', 0x850000, 0x85DFFF), ('textram_fb', 0x85F000, 0x85FFFF),
          ('textram_wram', 0xFF8000, 0xFF8FFF), ('spriteram', 0xFF7000, 0xFF77FF),
          ('palette', 0xFF9000, 0xFF9FFF), ('io_bank', 0xFFB000, 0xFFB04F),
          ('tile_dirty', 0xFFB9FE, 0xFFB9FF), ('pal_dirty', 0xFFB9FC, 0xFFB9FD),
          ('pal_dirty32', 0xFFBA00, 0xFFBA07), ('fbwin_any', 0x840000, 0x85FFFF),
          ('adapter', 0xA15100, 0xA1518F)]
# READ ranges (spin census, question 2): the FM gate thunks poll 0xA15100
# (tst.w), the vsync wait polls the frame flag 0xFFF01C, the MCU handshake
# reads the mailboxes and text RAM; the thunk code blocks are counted as
# instruction fetches (reads), so a gate thunk's spin shows up as fetches
# of its own words per entry.
READS = [('fm_reg', 0xA15100, 0xA15101), ('frameflag', 0xFFF01C, 0xFFF01F),
         ('mailbox', 0xFFF0C0, 0xFFF0C7), ('textram_fb', 0x85F000, 0x85FFFF),
         ('tile_thunk_code', 0xFFB820, 0xFFB9FF), ('pal_fm_thunk_code', 0xFFBA00, 0xFFBFEF),
         ('fbwin_any', 0x840000, 0x85FFFF), ('cart_hi', 0x900000, 0x93FFFF)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('--frames', type=int, default=4100)
    ap.add_argument('--a', type=int, default=1500)
    ap.add_argument('--b', type=int, default=4100)
    ap.add_argument('--input', default=os.path.join(ROOT, 'discover/inputs/play_level1.csv'))
    ap.add_argument('--out', default=None)
    ap.add_argument('--reads', action='store_true', help='also count READS on the spin-census ranges (needs the read hook build)')
    a = ap.parse_args()
    out = a.out or tempfile.mkdtemp(prefix='write_census_')
    os.makedirs(out, exist_ok=True)
    csvp = os.path.join(out, 'write_census.csv')
    cmd = [ARES, '--frames', str(a.frames), '--input', a.input, '--count-writes-out', csvp]
    for name, lo, hi in RANGES:
        cmd += ['--count-writes', f'{lo:#x}:{hi:#x}:{name}']
    if a.reads:
        for name, lo, hi in READS:
            cmd += ['--count-reads', f'{lo:#x}:{hi:#x}:{name}']
    cmd.append(a.rom)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    if r.returncode:
        sys.exit(f'ares failed:\n{r.stdout}\n{r.stderr}')
    rows = list(csv.DictReader(open(csvp)))
    names = [n for n, _, _ in RANGES] + (['R_' + n for n, _, _ in READS] if a.reads else [])
    print(f'{os.path.basename(a.rom)}: frames {a.frames}, window {a.a}-{a.b}')
    print(f'{"region":14s} {"per-frame":>10s} {"peak":>8s} {"total":>9s} {"clk/write":>10s} {"lines/frame":>12s}')
    LINE = 487.0                         # 68K clocks per scanline at 7.67 MHz / 15.7 kHz
    for n in names:
        vals = [int(x[n]) for x in rows]
        clks = [int(x.get(n + '_clk', 0)) for x in rows]
        win = vals[a.a:a.b]; cw = clks[a.a:a.b]
        per = sum(win) / max(1, len(win)); cpw = sum(cw) / max(1, sum(win))
        print(f'{n:14s} {per:10.1f} {max(win) if win else 0:8d} {sum(vals):9d} {cpw:10.1f} {sum(cw) / max(1, len(cw)) / LINE:12.2f}')
    sites = list(csv.DictReader(open(csvp + '.sites')))
    print('top PCs per range (arcade address = pc - 0x900000 when in the high copy):')
    for n in names:
        top = [s for s in sites if s['name'] == n][:5]
        if top:
            print(f'  {n:14s} ' + ' '.join(f"{int(s['pc'], 16):#08x}:{s['count']}" for s in top))
    print('csv in', out)


if __name__ == '__main__':
    main()
