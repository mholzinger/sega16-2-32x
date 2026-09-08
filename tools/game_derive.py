#!/usr/bin/env python3
"""Derive a title's patch tables from a reference title's hand-derived
tables, by disassembly alignment plus per-site byte verification.

    python3 tools/game_derive.py altbeast altbeastj [--map cached.json]
        -> tools/game_altbeastj.py  (+ a derivation report on stdout)

Every entry of tools/game_<ref>.py TABLES is translated to the target
program through the alignment (tools/game_align.py) and then CHECKED
against the target bytes the way the patcher will check them. Entries
that map but fail the check are dropped and reported; entries that do
not map are reported. The idiom lists that are scannable (the boot
region's pc-relative fixups, the jump-ins to the boot region) are
re-derived from the target listing directly, and the scanner is first
required to reproduce the reference's hand list exactly — derive, don't
guess.
"""
import sys, json, struct, re
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_align as GA
import importlib.util as ilu

ROOT = Path(__file__).resolve().parent.parent
NOTES = []      # scanner observations that are not derivation failures

def load_tables(game):
    gp = ROOT / 'tools' / f'game_{game}.py'
    spec = ilu.spec_from_file_location(f'game_{game}', gp)
    m = ilu.module_from_spec(spec); spec.loader.exec_module(m)
    return m.TABLES

def insn_index(insns):
    return {a: (w, t) for a, w, t in insns}

# ---- scanners (idiom re-derivation; verified against the reference) ----
def scan_boot_pcrel(insns, rom):
    """pc-relative instructions inside [0x400,0x808) -> absolute rewrites."""
    out = []
    for a, words, text in insns:
        if not (0x400 <= a < 0x808):
            continue
        mn = text.split()[0]
        old = list(rom[a:a+4])
        if mn in ('bsrw', 'braw'):
            tgt = a + 2 + struct.unpack('>h', rom[a+2:a+4])[0]
            if 0x400 <= tgt < 0x808:
                continue                # stays relative inside the copied region
            assert tgt < 0x8000, f"{a:#x}: {mn} target {tgt:#x} needs abs.l"
            op = 0x4EB8 if mn == 'bsrw' else 0x4EF8
            out.append((a, old, list(struct.pack('>HH', op, tgt))))
        elif mn == 'lea' and '%pc@' in text:
            w0 = struct.unpack('>H', rom[a:a+2])[0]
            assert (w0 & 0xF1FF) == 0x41FA, f"{a:#x}: lea pc form {w0:04x}"
            tgt = a + 2 + struct.unpack('>h', rom[a+2:a+4])[0]
            if 0x400 <= tgt < 0x808:
                continue
            assert tgt < 0x8000
            out.append((a, old, list(struct.pack('>HH', w0 & ~2, tgt))))
        elif mn.startswith('b') and mn.endswith('s') and len(words) == 1:
            # short branch: relative, fine if it stays inside the region
            d = struct.unpack('>b', rom[a+1:a+2])[0]
            tgt = a + 2 + d
            if not (0x400 <= tgt < 0x808):
                # The RAM copy executes only the exception stub at 0x408
                # (the game boots in the rebased high copy, md_main.c
                # "Enter the game's own boot IN PLACE"), so a short branch
                # leaving the copied window is inert. Recorded, not fatal.
                NOTES.append(f"boot region {a:#x}: short branch to {tgt:#x} leaves the copied window (inert: only the 0x408 stub executes from RAM)")
        elif '%pc@' in text:
            raise SystemExit(f"boot region {a:#x}: unhandled pc-relative {text}")
    return out

def scan_boot_jumpins(insns, rom):
    """control transfers from OUTSIDE into [0x400,0x808)."""
    out = []
    for a, words, text in insns:
        if 0x400 <= a < 0x808:
            continue
        mn = text.split()[0]
        n = len(words)
        if mn in ('bsrw', 'braw'):
            tgt = a + 2 + struct.unpack('>h', rom[a+2:a+4])[0]
            if 0x400 <= tgt < 0x808:
                out.append((a, mn, tgt))
        elif mn in ('bsrs', 'bras') or (mn.startswith('b') and mn.endswith('s') and n == 1):
            tgt = a + 2 + struct.unpack('>b', rom[a+1:a+2])[0]
            if 0x400 <= tgt < 0x808:
                NOTES.append(f"{a:#x}: short branch into the boot region ({text}) — inert, see scan_boot_pcrel")
        elif mn in ('jmp', 'jsr'):
            w0 = struct.unpack('>H', rom[a:a+2])[0]
            if w0 in (0x4EF9, 0x4EB9):
                tgt = struct.unpack('>I', rom[a+2:a+6])[0]
                if 0x400 <= tgt < 0x808:
                    out.append((a, 'jmpl' if w0 == 0x4EF9 else 'jsrl', tgt))
            elif w0 in (0x4EF8, 0x4EB8):
                tgt = struct.unpack('>h', rom[a+2:a+4])[0]
                if 0x400 <= tgt < 0x808:
                    out.append((a, 'jmpw' if w0 == 0x4EF8 else 'jsrw', tgt))
    return out

def main():
    argv = sys.argv[1:]
    if '--map' in argv:
        i = argv.index('--map'); argv = argv[:i] + argv[i+2:]
    ref_g, tgt_g = [a for a in argv if not a.startswith('--')]
    ref, tgt = GA.load(ref_g), GA.load(tgt_g)
    ref_rom = (ROOT / 'roms' / ref_g / 'prog68k.bin').read_bytes()
    tgt_rom = (ROOT / 'roms' / tgt_g / 'prog68k.bin').read_bytes()
    if '--map' in sys.argv:
        amap = {int(k, 16): int(v, 16) for k, v in
                json.loads(Path(sys.argv[sys.argv.index('--map') + 1]).read_text()).items()}
    else:
        amap, _, _ = GA.align(ref, tgt)
    keys = sorted(amap)
    import bisect
    def shift_map(off):
        """data/unaligned offset: neighbour-shift consistency."""
        i = bisect.bisect_right(keys, off)
        if i == 0 or i >= len(keys):
            return None
        p, q = keys[i - 1], keys[i]
        sp, sq = amap[p] - p, amap[q] - q
        return off + sp if sp == sq else None
    def mapc(off):
        return amap.get(off) or shift_map(off)
    ref_ix, tgt_ix = insn_index(ref), insn_index(tgt)
    def data_via_ref(addr):
        """Locate a data address in the target through the instructions
        that reference it in the reference listing (lea/pea/movel #imm):
        map each referencing instruction and read the same operand there.
        Returns the target address when every mapped reference agrees."""
        pat = re.compile(r'\b0x%x\b' % addr)
        cands = set()
        for a, w, t in ref:
            if not pat.search(t):
                continue
            m = amap.get(a)
            if m is None or m not in tgt_ix:
                continue
            tt = tgt_ix[m][1]
            hx = [int(h, 16) for h in re.findall(r'0x([0-9a-f]+)', tt)]
            rx = [int(h, 16) for h in re.findall(r'0x([0-9a-f]+)', t)]
            if len(hx) == len(rx) and addr in rx:
                cands.add(hx[rx.index(addr)])
        return cands.pop() if len(cands) == 1 else None
    def mapd(addr):
        return data_via_ref(addr) or shift_map(addr)
    def tgt_insn_len(a):
        return len(tgt_ix[a][0]) * 2 if a in tgt_ix else None

    R = load_tables(ref_g)
    out = {}
    report = []
    def ok(key, msg=''):
        report.append(f"OK   {key} {msg}")
    def miss(key, msg):
        report.append(f"MISS {key}: {msg}")

    # --- scannable idioms: verify the scanner on the reference first ---
    ref_pcrel = scan_boot_pcrel(ref, ref_rom)
    assert ref_pcrel == [tuple(x) if isinstance(x, tuple) else x for x in
                         [(a, list(o), list(n)) for a, o, n in R['BOOT_PCREL']]], \
        "boot pc-rel scanner does not reproduce the reference hand list"
    out['BOOT_PCREL'] = scan_boot_pcrel(tgt, tgt_rom); ok('BOOT_PCREL', f"{len(out['BOOT_PCREL'])} fixups (scanner reproduces ref)")
    ref_ji = scan_boot_jumpins(ref, ref_rom)
    assert ref_ji == [tuple(x) for x in R['BOOT_JUMPINS']], \
        f"jump-in scanner does not reproduce the reference: {ref_ji}"
    out['BOOT_JUMPINS'] = scan_boot_jumpins(tgt, tgt_rom); ok('BOOT_JUMPINS', f"{out['BOOT_JUMPINS']}")

    # --- simple code sites with byte checks ---
    def code_site(key, off, check, n=None):
        t = mapc(off)
        if t is None:
            miss(key, f"{off:#x} unmapped"); return None
        if not check(t):
            miss(key, f"{off:#x} -> {t:#x}: bytes {tgt_rom[t:t+8].hex()} fail check"); return None
        return t
    def words_at(rom, a, n): return struct.unpack_from(f'>{n}H', rom, a)

    t = code_site('RLE_EVEN_PASS', R['RLE_EVEN_PASS'],
                  lambda t: tgt_rom[t:t+6] == bytes([0x14,0x19,0x10,0x82,0x54,0x88]) and tgt_rom[t+0xA:t+0xE] == bytes([0x51,0xC8,0xFF,0xF6]))
    if t is not None: out['RLE_EVEN_PASS'] = t; ok('RLE_EVEN_PASS', f"{t:#x}")
    t = code_site('STRIP_BLITTER_MOVEW', R['STRIP_BLITTER_MOVEW'], lambda t: tgt_rom[t:t+2] == b'\x30\x18')
    if t is not None: out['STRIP_BLITTER_MOVEW'] = t; ok('STRIP_BLITTER_MOVEW', f"{t:#x}")
    t = code_site('STRIP_BLITTER_BASE', R['STRIP_BLITTER_BASE'], lambda t: words_at(tgt_rom, t, 3) == (0x203C, 0x0040, 0x0000))
    if t is not None: out['STRIP_BLITTER_BASE'] = t; ok('STRIP_BLITTER_BASE', f"{t:#x}")

    lst = []
    for off, old, new in R['TEXT_IDIOM']:
        t = code_site('TEXT_IDIOM', off, lambda t, old=old: words_at(tgt_rom, t, 1)[0] == old)
        if t is not None: lst.append((t, old, new))
    out['TEXT_IDIOM'] = lst; ok('TEXT_IDIOM', f"{len(lst)}/{len(R['TEXT_IDIOM'])}")

    lst = []
    for off, want, thunk in R['DISPATCHERS']:
        t = code_site('DISPATCHERS', off, lambda t, w=want: tgt_rom[t:t+6] == bytes(w))
        if t is not None: lst.append((t, list(want), thunk))
    out['DISPATCHERS'] = lst; ok('DISPATCHERS', f"{len(lst)}/{len(R['DISPATCHERS'])}")
    lst = []
    for off, want, thunk in R['DATA_PTR_NORM']:
        t = code_site('DATA_PTR_NORM', off, lambda t, w=want: tgt_rom[t:t+4] == bytes(w))
        if t is not None: lst.append((t, list(want), thunk))
    out['DATA_PTR_NORM'] = lst; ok('DATA_PTR_NORM', f"{len(lst)}/{len(R['DATA_PTR_NORM'])}")
    lst = []
    for off, want, thunk in R['TAS_SITES']:
        t = code_site('TAS_SITES', off, lambda t, w=want: tgt_rom[t:t+4] == bytes(w))
        if t is not None: lst.append((t, bytes(want), thunk))
    out['TAS_SITES'] = lst; ok('TAS_SITES', f"{len(lst)}/{len(R['TAS_SITES'])}")

    def remap_tile(a):
        return 0x852000 + (a & 0xFFFF) if 0x400000 <= a < 0x410000 else None
    lst = []
    for off, opw, tgt_addr, bits in R['TILE_DIRTY_SITES']:
        def chk(t, opw=opw, tgt_addr=tgt_addr):
            w = words_at(tgt_rom, t, 3)
            return w[0] == opw and remap_tile((w[1] << 16) | w[2]) == tgt_addr
        t = code_site('TILE_DIRTY_SITES', off, chk)
        if t is not None: lst.append((t, opw, tgt_addr, bits))
    out['TILE_DIRTY_SITES'] = lst; ok('TILE_DIRTY_SITES', f"{len(lst)}/{len(R['TILE_DIRTY_SITES'])}")

    lst = []
    for off, dlen, mask, note in R['PAL_DIRTY_SITES']:
        def chk(t, dlen=dlen, off=off):
            if tgt_rom[t:t+2] != ref_rom[off:off+2]: return False
            a = struct.unpack_from('>I', tgt_rom, t + dlen - 4)[0]
            return 0x840000 <= a < 0x841000
        t = code_site('PAL_DIRTY_SITES', off, chk)
        if t is not None: lst.append((t, dlen, mask, note))
    out['PAL_DIRTY_SITES'] = lst; ok('PAL_DIRTY_SITES', f"{len(lst)}/{len(R['PAL_DIRTY_SITES'])}")
    t = code_site('PAL_THUNK_A', R['PAL_THUNK_A'], lambda t: words_at(tgt_rom, t, 3) == (0x43F9, 0x0084, 0x0000))
    if t is not None: out['PAL_THUNK_A'] = t; ok('PAL_THUNK_A', f"{t:#x}")
    lst = []
    for off in R['PAL_THUNK_B']:
        t = code_site('PAL_THUNK_B', off, lambda t: tgt_rom[t:t+4] == b'\x22\x5a\x20\x5a')
        if t is not None: lst.append(t)
    out['PAL_THUNK_B'] = lst; ok('PAL_THUNK_B', f"{len(lst)}/{len(R['PAL_THUNK_B'])}")

    lst = []
    for off, dlen, w0, note in R['FMGATE_ENTRIES']:
        t = code_site('FMGATE_ENTRIES', off, lambda t, w0=w0: words_at(tgt_rom, t, 1)[0] == w0)
        if t is not None: lst.append((t, dlen, w0, note))
    out['FMGATE_ENTRIES'] = lst; ok('FMGATE_ENTRIES', f"{len(lst)}/{len(R['FMGATE_ENTRIES'])}")
    lst = []
    for s_, e_ in R['FMGATE_SPANS']:
        ts, te = mapc(s_), mapc(e_)
        if ts is None or te is None:
            miss('FMGATE_SPANS', f"({s_:#x},{e_:#x}) -> ({ts},{te})"); continue
        lst.append((ts, te))
    out['FMGATE_SPANS'] = lst; ok('FMGATE_SPANS', f"{len(lst)}/{len(R['FMGATE_SPANS'])}")

    lst = []
    for off, val in R['IMM_OVERRIDES']:
        t0 = mapc(off) or shift_map(off)
        if t0 is None: miss('IMM_OVERRIDES', f"{off:#x} unmapped"); continue
        # `off` is the OPERAND position (the patcher reads the long there);
        # the opcode word precedes it. Find the same opcode + a ROM-range
        # long near the mapped spot.
        opw = ref_rom[off-2:off]
        found = [a for a in range(max(0, t0 - 0x40), t0 + 0x40, 2)
                 if tgt_rom[a:a+2] == opw and 0x100 <= struct.unpack_from('>I', tgt_rom, a + 2)[0] < 0x40000]
        if len(found) != 1:
            miss('IMM_OVERRIDES', f"{off:#x} near {t0:#x}: {len(found)} candidates"); continue
        t = found[0] + 2
        v = struct.unpack_from('>I', tgt_rom, t)[0]
        mv = mapc(val)
        if mv != v: report.append(f"NOTE IMM_OVERRIDES {off:#x}: target imm {v:#x} vs mapped value {mv}")
        lst.append((t, v))
    out['IMM_OVERRIDES'] = lst; ok('IMM_OVERRIDES', f"{lst}")

    lst = []
    for off in R['LOW_VECTOR_READS']:
        t = mapc(off - 2)
        if t is None: miss('LOW_VECTOR_READS', f"{off:#x} unmapped"); continue
        if struct.unpack_from('>I', tgt_rom, t + 2)[0] != 0:
            miss('LOW_VECTOR_READS', f"{off:#x} -> {t+2:#x}: operand not 0"); continue
        lst.append(t + 2)
    out['LOW_VECTOR_READS'] = lst; ok('LOW_VECTOR_READS', f"{len(lst)}/{len(R['LOW_VECTOR_READS'])}")

    off, _ = R['ABSW_JMP']
    t = mapc(off)
    if t is None or words_at(tgt_rom, t, 1)[0] != 0x4EF8:
        miss('ABSW_JMP', f"{off:#x} -> {t}")
    else:
        out['ABSW_JMP'] = (t, words_at(tgt_rom, t + 2, 1)[0]); ok('ABSW_JMP', f"{out['ABSW_JMP']}")
        ji = [(a, k, g) for a, k, g in out['BOOT_JUMPINS'] if a == t]
        if not ji or ji[0][1] != 'jmpw':
            report.append(f"NOTE ABSW_JMP {t:#x} is not among the scanned jump-ins {out['BOOT_JUMPINS']}")

    # --- data ranges and values ---
    def data_range(key, lo, hi, must_equal=True):
        tl = mapd(lo)
        if tl is None:
            miss(key, f"({lo:#x},{hi:#x}): start unmapped"); return None
        th = tl + (hi - lo)
        if must_equal and tgt_rom[tl:th] != ref_rom[lo:hi]:
            report.append(f"NOTE {key} ({lo:#x},{hi:#x}) -> ({tl:#x},{th:#x}): bytes differ from reference")
        return (tl, th)
    for key in ('DATA_EXCLUDE', 'REBASE_EXCLUDE'):
        lst = []
        for lo, hi in R[key]:
            r = data_range(key, lo, hi)
            if r: lst.append(r)
        out[key] = lst; ok(key, f"{len(lst)}/{len(R[key])}")

    lst = []
    for start, n in R['REBASE_TABLES']:
        ts = mapd(start)
        if ts is None: miss('REBASE_TABLES', f"{start:#x} unmapped"); continue
        good = True
        for k in range(n):
            rv = struct.unpack_from('>I', ref_rom, start + 4 * k)[0]
            tv = struct.unpack_from('>I', tgt_rom, ts + 4 * k)[0]
            if mapc(rv) == tv:
                continue
            if rv == tv and rv not in ref_ix and tv not in tgt_ix:
                report.append(f"NOTE REBASE_TABLES {start:#x}[{k}]: {rv:#x} is not an instruction start in either program (table over-extent, harmless)")
                continue
            if mapc(rv) is None and 0x100 <= tv < 0x40000 and (tv - rv) == (ts - start) and not (tv & 1):
                kind = 'handler in an unaligned regional block' if tv in tgt_ix else 'sub-table pointer (data)'
                report.append(f"NOTE REBASE_TABLES {start:#x}[{k}]: {rv:#x} -> {tv:#x} by the table's own shift ({kind}; verify in MAME)")
                continue
            good = False; report.append(f"NOTE REBASE_TABLES {start:#x}[{k}]: ref {rv:#x}->{mapc(rv)} vs target {tv:#x}")
        if good: lst.append((ts, n))
        else: miss('REBASE_TABLES', f"{start:#x} -> {ts:#x}: entries do not map")
    out['REBASE_TABLES'] = lst; ok('REBASE_TABLES', f"{len(lst)}/{len(R['REBASE_TABLES'])}")

    lst = []
    for start, cnt, stride, poff in R['STRIDE_TABLES']:
        ts = mapd(start)
        if ts is None: miss('STRIDE_TABLES', f"{start:#x}"); continue
        def _nptr(rom, base):
            return sum(0x100 <= struct.unpack_from('>I', rom, base + k * stride + poff)[0] < 0x40000 for k in range(cnt))
        good = _nptr(tgt_rom, ts) == _nptr(ref_rom, start)
        if good: lst.append((ts, cnt, stride, poff))
        else: miss('STRIDE_TABLES', f"{start:#x} -> {ts:#x}: records not pointers")
    out['STRIDE_TABLES'] = lst; ok('STRIDE_TABLES', f"{lst}")

    sm, se = R['SPAWN_META']
    tsm = mapd(sm)
    tse = tsm + (se - sm) if tsm is not None else None
    _sh = (tsm - sm) if tsm is not None else None
    lo = R['SPAWN_LO'] + _sh if _sh is not None else None
    cap = R['SPAWN_CAP'] + _sh if _sh is not None else None
    if None in (tsm, tse, lo, cap):
        miss('SPAWN_META', f"{(tsm, tse, lo, cap)}")
    else:
        ents = [struct.unpack_from('>I', tgt_rom, a)[0] for a in range(tsm, tse - 3, 4)]
        if all(lo <= v < cap for v in ents):
            out['SPAWN_META'] = (tsm, tse); out['SPAWN_LO'] = lo; out['SPAWN_CAP'] = cap
            ok('SPAWN_META', f"({tsm:#x},{tse:#x}) lo {lo:#x} cap {cap:#x} entries {[hex(v) for v in ents]}")
        else:
            miss('SPAWN_META', f"entries {[hex(v) for v in ents]} outside ({lo:#x},{cap:#x})")

    offs, val = R['PAL_LAUNCH']
    tv = shift_map(val)
    toffs = [shift_map(o) for o in offs]
    if tv is None or None in toffs:
        miss('PAL_LAUNCH', f"{toffs} {tv}")
    else:
        got = [struct.unpack_from('>I', tgt_rom, o)[0] for o in toffs]
        if all(g == tv for g in got):
            out['PAL_LAUNCH'] = (toffs, tv); ok('PAL_LAUNCH', f"{[hex(o) for o in toffs]} -> {tv:#x}")
        else:
            miss('PAL_LAUNCH', f"target longs {[hex(g) for g in got]} != mapped {tv:#x}")

    hb = shift_map(R['HARVEST_BOUND'])
    out['HARVEST_BOUND'] = hb if hb else R['HARVEST_BOUND']
    ok('HARVEST_BOUND', f"{out['HARVEST_BOUND']:#x}" + ('' if hb else ' (unmapped; reference value kept)'))
    out['HARVEST_BLACKLIST'] = list(R['HARVEST_BLACKLIST'])
    hv, unm = [], []
    for v in R['HARVESTED_HANDLERS']:
        if v in R['HARVEST_BLACKLIST'] or v < 0x100 or v >= 0x40000:
            hv.append(v); continue
        m = mapc(v)
        if m is None: unm.append(v); continue
        hv.append(m)
    out['HARVESTED_HANDLERS'] = hv
    ok('HARVESTED_HANDLERS', f"{len(hv)} mapped, {len(unm)} unmapped {[hex(u) for u in unm]}")

    report += ['NOTE ' + n for n in NOTES]; NOTES.clear()
    # MCU mailboxes: follow each reference address's read/write sites to
    # the target instruction and take its work-RAM operand (the listing
    # prints them sign-extended: 0xfffff0c2). Majority over the sites.
    def wram_via_ref(addr):
        """The differing RAM operand is part of the instruction shape, so
        these sites never align. Instead: for each reference site, take the
        neighbour-shifted position and look within +-0x40 for a target
        instruction of the same shape with SOME work-RAM operand in the
        address's place; vote over the sites."""
        pat = re.compile(r'\b0xffff%04x\b' % (addr & 0xFFFF))
        votes = {}
        for a, w, t in ref:
            if not pat.search(t): continue
            c = mapc(a)
            if c is None: continue
            want = re.escape(pat.sub('@', t)).replace('@', r'0xffff([0-9a-f]{4})')
            best_m, best_d = None, 1 << 30
            for ta, tw, tt in tgt:
                if ta < c - 0x40 or ta > c + 0x40: continue
                m = re.fullmatch(want, tt)
                if m and abs(ta - c) < best_d:
                    best_m, best_d = m, abs(ta - c)
            if best_m:                      # nearest same-shape instruction per site
                v = 0xFF0000 | int(best_m.group(1), 16); votes[v] = votes.get(v, 0) + 1
        if not votes: return None, votes
        best = max(votes, key=votes.get)
        return (best if votes[best] * 2 > sum(votes.values()) else None), votes
    for key in ('MCU_BUSY', 'MCU_COINS', 'MCU_SND'):
        v, votes = wram_via_ref(R[key])
        if v is None: miss(key, f"votes {{{', '.join(f'{k:#x}:{n}' for k, n in votes.items())}}}")
        else: out[key] = v; ok(key, f"{R[key]:#x} -> {v:#x} (sites {votes[v]})")
    # --- emit ---
    def fmt(v, ind=8):
        if isinstance(v, bytes):
            return 'bytes([' + ', '.join(f'0x{b:02X}' for b in v) + '])'
        if isinstance(v, bool): return repr(v)
        if isinstance(v, int): return f'0x{v:X}' if v >= 0x100 else str(v)
        if isinstance(v, str): return repr(v)
        if isinstance(v, (list, tuple)):
            inner = ', '.join(fmt(x) for x in v)
            return ('(' + inner + (',' if len(v) == 1 else '') + ')') if isinstance(v, tuple) else '[' + inner + ']'
        raise TypeError(type(v))
    lines = [f'"""{tgt_g}: patch tables DERIVED from tools/game_{ref_g}.py by',
             'tools/game_derive.py (disassembly alignment + per-site byte checks).',
             'Regenerate with:  python3 tools/game_derive.py ' + ref_g + ' ' + tgt_g,
             'Hand corrections belong in this file AFTER the generated block, keyed',
             'the same way, with the evidence cited. Derivation report:', '']
    lines += ['    ' + r for r in report]
    lines += ['"""', '', 'TABLES = {']
    for k in R:
        if k in out:
            v = out[k]
            if isinstance(v, list) and v and isinstance(v[0], (tuple, list)):
                lines.append(f"    {k!r}: [")
                for e in v:
                    lines.append(f"        {fmt(e)},")
                lines.append("    ],")
            else:
                lines.append(f"    {k!r}: {fmt(v)},")
        else:
            lines.append(f"    # {k!r}: NOT DERIVED — see report")
    lines.append('}')
    (ROOT / 'tools' / f'game_{tgt_g}.py').write_text('\n'.join(lines) + '\n')
    report += ['NOTE ' + n for n in NOTES]
    print('\n'.join(report))
    missing = [k for k in R if k not in out]
    print(f"\nwrote tools/game_{tgt_g}.py: {len(out)}/{len(R)} keys; missing: {missing}")

if __name__ == '__main__':
    main()
