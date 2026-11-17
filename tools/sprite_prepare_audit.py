#!/usr/bin/env python3
"""Check a settled SH-2 prepared pose against a fresh sprite-ROM decode.

Checks packet bytes/headers and zero-preserving palette indices only. It does not
prove framebuffer output, cadence, gameplay parity or hardware speed.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from bake_art import unkeyword
from s16art import load_rom, decode, pack_frame

FIELDS = ('data','capacity','key','bank','epoch','height','valid','uses',
          'candidate_key','candidate_bank','candidate_count','candidate_height',
          'lookups','hits','prepares','declined','words','bank_mask')


def audit(sdram, listing, rom):
    if len(sdram) != 0x40000:
        raise ValueError('expected full 256 KiB SDRAM')
    symbols = {p[2]: int(p[0],16) & 0x3ffff for line in listing.splitlines()
               if len(p := line.split()) == 3}
    state = dict(zip(FIELDS, struct.unpack_from('>18I', sdram, symbols['_r60_sp_cache'])))
    if state['valid'] != 1:
        raise ValueError('cache empty or being prepared; capture a settled packet')
    if state['data'] != 0x0603d800 or state['capacity'] != 1024:
        raise ValueError('unexpected packet arena')
    if state['key'] & 0xe8000000 or state['bank'] > 15 or not 0 < state['height'] <= 224:
        raise ValueError('unsupported prepared pose descriptor')
    key = unkeyword(state['key'])
    rows, _, rownw = decode(load_rom(rom), *key, state['height'])
    raw = bytearray(pack_frame(rows, rownw)[4:])
    at = 0
    for _ in rows:
        groups = raw[at+1]
        at += 4
        for p in range(at, at+groups*4):
            if raw[p]:
                raw[p] |= state['bank'] * 16
        at += groups*4
    if len(raw) > state['capacity'] * 4:
        raise ValueError('descriptor exceeds its arena')
    actual = sdram[0x3d800:0x3d800+len(raw)]
    result = dict(scope='stored prepared packet only, not framebuffer or speed',
                  state=state, checked_bytes=len(raw),
                  wrong_bytes=sum(a!=b for a,b in zip(actual,raw)))
    if '_r60_bk_bad' in symbols:
        result['row_check'] = {n: struct.unpack_from('>I',sdram,symbols['_'+n])[0]
                              for n in ('r60_bk_ckrows','r60_bk_bad','r60_bk_badx','r60_bk_badn')}
        result['prepared_copy_checks'] = struct.unpack_from('>8I',sdram,0x3eb20)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('sdram',type=Path)
    ap.add_argument('listing',type=Path)
    ap.add_argument('rom',type=Path)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()
    r=audit(args.sdram.read_bytes(),args.listing.read_text(),args.rom)
    r['inputs']={str(p):hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in (args.sdram,args.listing,args.rom)}
    report=json.dumps(r,indent=2)+'\n'
    args.out.write_text(report);print(report,end='')
    checks=r.get('row_check',{})
    return int(bool(r['wrong_bytes'] or any(checks.get(n,0) for n in ('r60_bk_bad','r60_bk_badx','r60_bk_badn'))))


if __name__=='__main__':
    raise SystemExit(main())
