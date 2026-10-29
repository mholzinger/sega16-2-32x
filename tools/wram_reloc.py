#!/usr/bin/env python3
"""WRAM-resident game routines (2026-09-25, LESSONS "the FPGA's cart-fetch
penalty is the core's ROM path").

The MiSTer core holds the 68K's DTACK for a full SDRAM access on every MD
ROM read, so fetch-bound game code runs 18-32% slower there than from
WRAM (RIGSPAN=15). This tool re-lays hot routines of the patched game
image into a WRAM image and turns each routine's ROM entry into
`jmp (abs.l)` to the copy, so pointer-called handlers need no call-site
edits.

Relocation rules (68000 only):
  - internal branches (Bcc/BRA/BSR/DBcc whose target is inside the block)
    are re-encoded against the new addresses (.s or .w as needed);
  - external BSR -> JSR abs.l, BRA -> JMP abs.l, Bcc -> inverted Bcc.s +6
    then JMP abs.l; external DBcc -> the block is rejected;
  - (d16,PC) operands -> absolute long (ea 111/010 -> 111/001, +2 bytes);
    (d8,PC,Xn) -> rejected;
  - everything else is copied verbatim (absolute and register forms do
    not depend on the PC).
Every emitted block is re-disassembled and compared instruction by
instruction with the original (mnemonic and resolved target), and the
entry's first four ROM bytes must not be a branch target from anywhere
in the image.

Usage (from patch_game.py, or standalone):
    wram_reloc.py <image.bin> <objdump> <base> <start-end>[,start-end...] <out.h>
"""
import re, struct, subprocess, sys, tempfile, os

INS_RE = re.compile(r'^\s+([0-9a-f]+):\t((?:[0-9a-f]{4} ?)+)\s*\t?(\S+)?\s*(.*)$')
BR_RE = re.compile(r'^(b(ra|sr|cc|cs|eq|ne|pl|mi|hi|ls|ge|lt|gt|le|vc|vs)[sw]?)$')
DB_RE = re.compile(r'^db(ra|f|t|cc|cs|eq|ne|pl|mi|hi|ls|ge|lt|gt|le|vc|vs)$')
COND = {'ra': 0x0, 'sr': 0x1, 'hi': 0x2, 'ls': 0x3, 'cc': 0x4, 'cs': 0x5, 'ne': 0x6, 'eq': 0x7,
        'vc': 0x8, 'vs': 0x9, 'pl': 0xA, 'mi': 0xB, 'ge': 0xC, 'lt': 0xD, 'gt': 0xE, 'le': 0xF}
INV = {0x2: 0x3, 0x3: 0x2, 0x4: 0x5, 0x5: 0x4, 0x6: 0x7, 0x7: 0x6, 0x8: 0x9, 0x9: 0x8,
       0xA: 0xB, 0xB: 0xA, 0xC: 0xD, 0xD: 0xC, 0xE: 0xF, 0xF: 0xE}
ROM_HIGH = 0x900000          # the game image as the 68K sees it


def disasm(objdump, image_path, start, stop, vma=0):
    out = subprocess.run([objdump, '-b', 'binary', '-m', '68000', f'--adjust-vma={vma}',
                          f'--start-address={start}', f'--stop-address={stop}', '-D', image_path],
                         capture_output=True, text=True).stdout
    ins = []
    for l in out.splitlines():
        m = INS_RE.match(l)
        if m:
            ins.append((int(m.group(1), 16), [int(w, 16) for w in m.group(2).split()],
                        m.group(3) or '', m.group(4)))
    return ins


def target_of(ops):
    m = re.search(r'0x([0-9a-f]+)', ops)
    return int(m.group(1), 16) if m else None


class Reject(Exception):
    pass


def relocate_block(ins, start, end, new_base):
    """ins: instructions of [start,end) at their ROM offsets. Returns (words, mapping old->new)."""
    body = [i for i in ins if start <= i[0] < end]
    assert body and body[0][0] == start and sum(len(w) * 2 for _, w, _, _ in body) == end - start, \
        f'block {start:#x}-{end:#x} is not instruction-aligned'
    # classify
    items = []   # dicts: kind, words, old, target, cond, size (mutable)
    for a, words, mn, ops in body:
        op = words[0]
        it = {'old': a, 'words': list(words), 'mn': mn, 'ops': ops, 'kind': 'copy'}
        if BR_RE.match(mn):
            t = target_of(ops)
            assert t is not None, (hex(a), mn, ops)
            it['target'] = t
            it['cond'] = (op >> 8) & 0xF
            it['kind'] = 'br_int' if start <= t < end else 'br_ext'
            if it['kind'] == 'br_ext' and it['cond'] == 0x1 and False:
                pass
        elif DB_RE.match(mn):
            t = target_of(ops)
            assert t is not None
            if not (start <= t < end):
                raise Reject(f'{a:#x}: external DBcc')
            it['target'] = t
            it['kind'] = 'db_int'
        elif '%pc@' in ops:
            if ':' in ops.split('%pc@', 1)[1].split(')', 1)[0]:
                raise Reject(f'{a:#x}: indexed PC-relative operand')
            if (op & 0x3F) != 0x3A:
                raise Reject(f'{a:#x}: PC-relative operand not in the source ea field ({mn} {ops})')
            # extension words that precede the ea displacement: the bit number
            # of btst/bchg/bclr/bset #n, the register mask of movem
            lead = 1 if ((op & 0xFFC0) in (0x0800, 0x0840, 0x0880, 0x08C0) or (op & 0xFB80) == 0x4880) else 0
            dw = words[1 + lead]
            disp = dw if dw < 0x8000 else dw - 0x10000
            t = a + 2 + lead * 2 + disp          # the PC is the address of the displacement word
            told = target_of(ops.split('%pc@', 1)[1])
            assert told == t, f'{a:#x}: PC-relative target {t:#x} != objdump {told:#x} ({mn} {ops})'
            it['target'] = t
            it['lead'] = lead
            it['kind'] = 'pcrel'
        items.append(it)
    # sizes: iterate to a fixpoint (internal branches may promote .s -> .w)
    for it in items:
        if it['kind'] == 'copy':
            it['size'] = len(it['words']) * 2
        elif it['kind'] == 'br_int':
            it['size'] = 2                     # optimistic; promoted below
        elif it['kind'] == 'br_ext':
            it['size'] = 6 if it['cond'] in (0x0, 0x1) else 8
        elif it['kind'] == 'db_int':
            it['size'] = 4
        elif it['kind'] == 'pcrel':
            it['size'] = len(it['words']) * 2 + 2
    while True:
        pos = new_base
        newaddr = {}
        for it in items:
            newaddr[it['old']] = pos
            it['new'] = pos
            pos += it['size']
        changed = False
        for it in items:
            if it['kind'] == 'br_int':
                disp = newaddr[it['target']] - (it['new'] + 2)
                need = 2 if (-128 <= disp <= 127 and disp != 0) else 4
                if need > it['size']:
                    it['size'] = need
                    changed = True
        if not changed:
            break
    # encode
    out = []
    for it in items:
        k = it['kind']
        w = it['words']
        if k == 'copy':
            out += w
        elif k == 'br_int':
            disp = newaddr[it['target']] - (it['new'] + 2)
            if it['size'] == 2:
                out += [0x6000 | (it['cond'] << 8) | (disp & 0xFF)]
            else:
                out += [0x6000 | (it['cond'] << 8), disp & 0xFFFF]
        elif k == 'br_ext':
            t = ROM_HIGH + it['target']
            if it['cond'] == 0x0:            # BRA -> JMP abs.l
                out += [0x4EF9, t >> 16, t & 0xFFFF]
            elif it['cond'] == 0x1:          # BSR -> JSR abs.l
                out += [0x4EB9, t >> 16, t & 0xFFFF]
            else:                            # Bcc -> B!cc.s +6 ; JMP abs.l
                out += [0x6000 | (INV[it['cond']] << 8) | 0x06, 0x4EF9, t >> 16, t & 0xFFFF]
        elif k == 'db_int':
            disp = newaddr[it['target']] - (it['new'] + 2)
            out += [w[0], disp & 0xFFFF]
        elif k == 'pcrel':
            t = ROM_HIGH + it['target']
            ld = it['lead']
            out += [(w[0] & ~0x3F) | 0x39] + w[1:1 + ld] + [t >> 16, t & 0xFFFF] + w[2 + ld:]
    assert len(out) * 2 == pos - new_base
    return out, newaddr, items


def verify_block(objdump, words, new_base, items, start, end):
    """Re-disassemble the emitted words and check each instruction against its source."""
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(b''.join(struct.pack('>H', x) for x in words))
        tmp = f.name
    try:
        got = disasm(objdump, tmp, new_base, new_base + len(words) * 2, vma=new_base)
    finally:
        os.unlink(tmp)
    gi = 0
    assert got, f'block {start:#x}: empty re-disassembly'
    for it in items:
        assert gi < len(got), f'block {start:#x}: re-disassembly short at {it["old"]:#x}'
        a, w, mn, ops = got[gi]
        assert a == it['new'], f'layout drift at {it["old"]:#x}: emitted {a:#x} expected {it["new"]:#x}'
        k = it['kind']
        if k == 'copy':
            assert w == it['words'], (hex(it['old']), mn, ops)
            gi += 1
        elif k in ('br_int', 'db_int'):
            t = target_of(ops)
            want = it['new_target']
            assert mn[:2] == it['mn'][:2] and t == want, (hex(it['old']), it['mn'], mn, hex(t or 0), hex(want))
            gi += 1
        elif k == 'br_ext':
            t = ROM_HIGH + it['target']
            if it['cond'] == 0x0:
                assert mn == 'jmp' and target_of(ops) == t, (hex(it['old']), mn, ops)
                gi += 1
            elif it['cond'] == 0x1:
                assert mn == 'jsr' and target_of(ops) == t, (hex(it['old']), mn, ops)
                gi += 1
            else:
                a2, w2, mn2, ops2 = got[gi + 1]
                assert mn.startswith('b') and target_of(ops) == a2 + 6 and mn2 == 'jmp' and target_of(ops2) == t, \
                    (hex(it['old']), mn, ops, mn2, ops2)
                gi += 2
        elif k == 'pcrel':
            # the re-disassembled operand must name the same absolute target
            # objdump resolved for the ORIGINAL PC-relative operand
            want = ROM_HIGH + target_of(it['ops'].split('%pc@', 1)[1])
            assert mn == it['mn'] and '%pc@' not in ops and (ROM_HIGH + target_of(ops) if target_of(ops) < ROM_HIGH else target_of(ops)) == want, \
                (hex(it['old']), it['mn'], it['ops'], mn, ops)
            gi += 1
    assert gi == len(got), 'trailing instructions in the emitted block'


def all_branch_targets(ins):
    """{target: set(source addresses)} over every branch/jsr/jmp in the image."""
    ts = {}
    for a, w, mn, ops in ins:
        if BR_RE.match(mn) or DB_RE.match(mn) or mn in ('jsr', 'jmp'):
            t = target_of(ops)
            if t is not None:
                for k in (t & 0x3FFFF, t):
                    ts.setdefault(k, set()).add(a)
    return ts


def relocate_regions(image, objdump, regions, entries=None, log=print):
    """regions: [(base, blocks, limit)] -- several WRAM regions laid out in one two-pass run
    (2026-09-24: region 2 at 0xFF4600, the gap above .bss and below the boot stack's keep).
    Returns [(base, words, table)] in region order. Patches the entries and the abs.l call
    sites in place."""
    scratch = bytearray(image)
    alias = {}
    for base, blocks, limit in regions:
        _w, _t, a = _relocate(scratch, objdump, base, blocks, entries, log=lambda *a: None)
        alias.update(a)
    _rewrite_sites(image, objdump, alias, log)
    out = []
    for base, blocks, limit in regions:
        words, table, _ = _relocate(image, objdump, base, blocks, entries, log)
        assert base + len(words) * 2 <= limit, f'WRAMCODE: region {base:06X} needs {len(words)*2} bytes, limit {limit:06X}'
        out.append((base, words, table))
    return out


def _rewrite_sites(image, objdump, alias, log):
    if not alias:
        return
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(bytes(image))
        tmp = f.name
    try:
        full = disasm(objdump, tmp, 0, len(image))
    finally:
        os.unlink(tmp)
    n = 0
    for a, w, mn, ops in full:
        if mn in ('jsr', 'jmp') and w[0] in (0x4EB9, 0x4EF9) and len(w) == 3:
            t = (w[1] << 16) | w[2]
            e = t - ROM_HIGH
            if e in alias:
                wa = alias[e]
                struct.pack_into('>HH', image, a + 2, wa >> 16, wa & 0xFFFF)
                n += 1
    log(f'WRAMCODE: {n} abs.l call sites re-pointed at WRAM copies')


def relocate(image, objdump, base, blocks, entries=None, log=print):
    """image: bytearray of the patched game program. Patches the entries in place.
    Returns (words, table) where table = [(rom_entry, wram_addr)].

    Two passes. The first lays the blocks out on a scratch copy to learn every
    entry's WRAM address; every `jsr`/`jmp (abs.l)` in the image whose target
    is such an entry is then re-pointed at the WRAM copy (same 6-byte
    instruction, new target: the call no longer fetches the ROM stub's three
    words -- the profile of 2026-09-24 put ~11% of the residual cart samples
    ON the stubs, 0x3F04's alone 22 of 594). The second pass relocates the
    rewritten image, so copies inside blocks carry the direct targets too."""
    scratch = bytearray(image)
    _w, _t, alias = _relocate(scratch, objdump, base, blocks, entries, log=lambda *a: None)
    if alias:
        with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
            f.write(bytes(image))
            tmp = f.name
        try:
            full = disasm(objdump, tmp, 0, len(image))
        finally:
            os.unlink(tmp)
        n = 0
        for a, w, mn, ops in full:
            if mn in ('jsr', 'jmp') and w[0] in (0x4EB9, 0x4EF9) and len(w) == 3:
                t = (w[1] << 16) | w[2]
                e = t - ROM_HIGH
                if e in alias:
                    wa = alias[e]
                    struct.pack_into('>HH', image, a + 2, wa >> 16, wa & 0xFFFF)
                    n += 1
        log(f'WRAMCODE: {n} abs.l call sites re-pointed at WRAM copies')
    words, table, _ = _relocate(image, objdump, base, blocks, entries, log)
    return words, table


def _relocate(image, objdump, base, blocks, entries=None, log=print):
    """one relocation pass; returns (words, table, alias) with alias = {rom_entry: wram_addr}
    for EVERY entry, planted or ROM-only."""
    alias = {}
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(bytes(image))
        tmp = f.name
    try:
        full = disasm(objdump, tmp, 0, len(image))
    finally:
        os.unlink(tmp)
    targets = all_branch_targets(full)
    words = []
    table = []
    pos = base
    for start, end in blocks:
        try:
            blk, newaddr, items = relocate_block(full, start, end, pos)
        except Reject as e:
            log(f'WRAMCODE: block {start:05X}-{end:05X} rejected: {e}')
            continue
        for it in items:
            if 'target' in it and it['kind'] in ('br_int', 'db_int'):
                it['new_target'] = newaddr[it['target']]
        verify_block(objdump, blk, pos, items, start, end)
        # entries: the block start plus any address in the block that outside code jumps to
        # entries: the block start, every address inside the block that code
        # OUTSIDE the block jumps to, and the known pointer-called entries
        ents = {start} | {t for t, srcs in targets.items()
                          if start < t < end and t in newaddr and any(not (start <= s0 < end) for s0 in srcs)}
        if entries:
            ents |= {e for e in entries if start <= e < end}
        skipped = []
        for e in sorted(ents):
            # the 6-byte jmp (abs.l) overwrites [e, e+6): nothing may branch
            # into e+2 or e+4. (abs.w would sign-extend only from 0x8000 up;
            # 0xFF5000 as .w is low ROM -- the tile thunks sit at 0xFFB820 for
            # that reason.) An unplantable entry keeps its ROM code (correct,
            # just not accelerated) -- unless it is the block's own start.
            if (e + 2) in targets or (e + 4) in targets or e + 6 > end:
                if e == start:
                    log(f'WRAMCODE: block {start:05X}-{end:05X} rejected: its entry cannot take a jump')
                    ents = None
                    break
                skipped.append(e)
                alias[e] = newaddr[e]        # no stub, but abs.l callers can still go direct
                continue
            wa = newaddr[e]
            alias[e] = wa
            assert 0xFF0000 <= wa <= 0xFFFFFF
            struct.pack_into('>HHH', image, e, 0x4EF9, wa >> 16, wa & 0xFFFF)   # jmp (abs.l)
            table.append((e, wa))
        if ents is None:
            continue
        log(f'WRAMCODE: {start:05X}-{end:05X} {end-start:4d}B -> {pos:06X} ({len(blk)*2:4d}B) entries {" ".join(f"{e:05X}" for e in sorted(ents) if e not in skipped)}'
            + (f'  (ROM-only entries {" ".join(f"{e:05X}" for e in skipped)})' if skipped else ''))
        words += blk
        pos += len(blk) * 2
    return words, table, alias


def write_header(path, base, words, table, region2=None):
    """region2: (base, words, table) of the second WRAM region, or None."""
    with open(path, 'w') as h:
        h.write('/* generated by tools/wram_reloc.py -- WRAM-resident game routines */\n')
        h.write(f'#define WRAM_CODE_BASE 0x{base:06X}\n#define WRAM_CODE_WORDS {len(words)}\n')
        h.write(f'#define WRAM_CODE_ENTRIES {len(table)}\n')
        h.write('static const unsigned short wram_code[] = {\n')
        for i in range(0, len(words), 8):
            h.write('    ' + ', '.join(f'0x{w:04X}' for w in words[i:i+8]) + ',\n')
        h.write('};\n')
        if region2 and region2[1]:
            b2, w2, t2 = region2
            h.write(f'#define WRAM_CODE2_BASE 0x{b2:06X}\n#define WRAM_CODE2_WORDS {len(w2)}\n')
            h.write(f'#define WRAM_CODE2_ENTRIES {len(t2)}\n')
            h.write('static const unsigned short wram_code2[] = {\n')
            for i in range(0, len(w2), 8):
                h.write('    ' + ', '.join(f'0x{w:04X}' for w in w2[i:i+8]) + ',\n')
            h.write('};\n')
        else:
            h.write('#define WRAM_CODE2_WORDS 0\n')


if __name__ == '__main__':
    img_path, objdump, base, blocks, out = sys.argv[1:6]
    base = int(base, 16)
    blocks = [tuple(int(x, 16) for x in b.split('-')) for b in blocks.split(',')]
    image = bytearray(open(img_path, 'rb').read())
    words, table = relocate(image, objdump, base, blocks)
    write_header(out, base, words, table)
    print(f'{len(words)*2} bytes of WRAM code, {len(table)} entries')
