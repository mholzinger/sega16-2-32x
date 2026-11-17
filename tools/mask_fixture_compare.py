#!/usr/bin/env python3
"""Exact pen comparison of two fixed sprite+plane renderer fixtures in ares.

Prefixes name -sd.bin, -dram.bin, -flips.csv and .lst files. The last actual
flip selects the shown physical bank; a hidden bank may still be drawing. Read
the shown bank through its line tables with the fixed published dot shift. This
fixture check does not establish cadence or normal gameplay equivalence. Native
sprite checks include FG/text occlusion; --verify-native-image also validates the
reference against the MAME screen. Priorities0/1 require a separate BG compositor.
"""
import argparse
import csv
import hashlib
import json
import struct
import io
from pathlib import Path
from PIL import Image

import numpy as np
from sprite_pixel_audit import framebuffer, reference, native_image_check
from tile_block_census import tile_at, physical_code
from sprite_scene_audit import compare_scene
from sprite_penid_check import decode as decode_penid_image


def symbols(path):
    return {p[2]: int(p[0], 16) & 0x3ffff
            for line in path.read_text().splitlines() if len(p := line.split()) == 3}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('control', type=Path)
    parser.add_argument('experiment', type=Path)
    parser.add_argument('sprites', type=Path)
    parser.add_argument('planes', type=Path, help='native snapshot prefix')
    parser.add_argument('--bank', type=int, required=True, help='expected adapter tile bank')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--sprite-rom', type=Path, required=True)
    parser.add_argument('--tiles', type=Path, required=True)
    parser.add_argument('--verify-native-image', action='store_true',
                        help='also validate reference against native-prefix PNG and palette')
    parser.add_argument('--scaled-reference', action='store_true',
                        help='use independently native-validated scaled sprite reference')
    parser.add_argument('--verify-port-images', action='store_true',
                        help='check visible sprite pens in each calibrated PENID PNG')
    parser.add_argument('--verify-full-port-images', action='store_true',
                        help='require complete port screenshots to match, including MD planes/text')
    args = parser.parse_args()
    inputs = {}
    def read(path):
        raw = path.read_bytes()
        inputs[str(path)] = hashlib.sha256(raw).hexdigest()
        return raw
    sprite_data = read(args.sprites)
    if len(sprite_data) not in (1024, 2048):
        raise ValueError('expected native 1/2 KiB sprite RAM')
    table = sprite_data[:1024]
    if not any(struct.unpack_from('>H', table, slot*16+4)[0] & 0x8000 for slot in range(64)):
        raise ValueError("native scene exceeds the port fixture's 64 records")
    native_tiles = read(Path(str(args.planes) + '_tilemap.bin'))
    text = read(Path(str(args.planes) + '_text.bin'))
    selects = struct.unpack_from('>2H', text, 0x740 * 2)
    expected_tiles = bytearray(0xd000)
    for page in range(13):
        if any(page == ((select >> shift) & 15) for select in selects for shift in (0, 4, 8, 12)):
            expected_tiles[page*4096:(page+1)*4096] = native_tiles[page*4096:(page+1)*4096]
    expected_view = dict(prio_psel=selects[0],
                         prio_xs=(192-struct.unpack_from('>H', text, 0x74c*2)[0])&1023,
                         prio_ys=struct.unpack_from('>H', text, 0x748*2)[0]&511,
                         prio_bank1=args.bank)
    banks = []
    proof = []
    shown = []
    palette_maps = []
    for prefix in (args.control, args.experiment):
        sd = read(Path(str(prefix) + '-sd.bin'))
        dram = read(Path(str(prefix) + '-dram.bin'))
        listing = Path(str(prefix) + '.lst')
        read(listing)
        sym = symbols(listing)
        if len(sd) != 0x40000 or len(table) != 1024:
            raise ValueError('bad dump or fixture length')
        checks = dict(live_sprites=sd[0x28400:0x28800] == table,
                      planes=sd[0x19000:0x26000] == expected_tiles,
                      text=sd[0x26000:0x27000] == text)
        snap = sym['_r60_snap2_mem']
        checks['both_sprite_snapshots'] = all(sd[snap+k*1024:snap+(k+1)*1024] == table for k in (0, 1))
        view = {name: struct.unpack_from('>I', sd, sym['_'+name])[0] for name in expected_view}
        checks['priority_view'] = view == expected_view
        if not all(checks.values()):
            raise ValueError(f'{prefix}: incomparable inputs: {checks}, actual view={view}')
        events = list(csv.DictReader(io.StringIO(read(Path(str(prefix) + '-flips.csv')).decode())))
        applied = [event for event in events if event['event'] == 'flip']
        if not applied:
            raise ValueError('no actual flip evidence')
        # ares/md/m32x/vdp.cpp selectFramebuffer: fbram = dram +
        # 0x10000 words when select==0, otherwise the first physical bank.
        selected_bank = 1 - int(applied[-1]['select'])
        shown.append(selected_bank)
        palette_maps.append(list(sd[sym['_r60_bankpal_m']:sym['_r60_bankpal_m']+16]))
        shift = struct.unpack_from('>H', sd, 0x28ff8)[0]
        banks.append([framebuffer(dram, k, shift) for k in (0, 1)])
        proof.append(dict(prefix=str(prefix), checks=checks, priority_view=view,
                          published_shift=shift, shown_physical_bank=selected_bank,
                          last_applied_flip=applied[-1]))
    art = read(args.tiles)
    rom = read(args.sprite_rom)
    if args.scaled_reference:
        from sprite_scaled_audit import reference as scaled_reference
        native_pens, owners, records, _ = scaled_reference(table, struct.unpack('>%dH' % (len(rom)//2), rom))
    else:
        native_pens, owners, records = reference(table, rom)
    if any(record['priority'] < 2 for record in records):
        raise ValueError('sprite priorities0/1 require a full background compositor')
    words = struct.unpack('>26624H', expected_tiles)
    foreground = np.zeros((224, 320), dtype=bool)
    for y in range(208):  # bottom16 rows are calibration, not scene pixels
        for x in range(320):
            wx, wy = x + expected_view['prio_xs'], y + expected_view['prio_ys']
            word = tile_at(words, expected_view['prio_psel'], wx//8, wy//8)
            if word & 32768 and word & 8191:
                foreground[y, x] = bool(art[physical_code(word, args.bank)*64 + wy%8*8 + wx%8] & 7)
    text_words = struct.unpack('>2048H', text)
    text_mask = np.zeros((224, 320), dtype=bool)
    for y in range(208):
        for x in range(320):
            word = text_words[y//8*64 + 24 + x//8]
            if word & 0xfff:
                text_mask[y,x] = bool(art[(word & 511)*64 + y%8*8 + x%8] & 7)
    native_checks = [compare_scene(native_pens, owners, records, banks[i][shown[i]],
                                   palette_maps[i], foreground, text_mask)
                     for i in (0, 1)]
    port_screens = []
    if args.verify_port_images:
        visible = (owners >= 0) & ~foreground & ~text_mask
        visible[208:] = False
        for i, prefix in enumerate((args.control, args.experiment)):
            path = Path(str(prefix) + '.png')
            read(path)
            pixels, colors = decode_penid_image(path)
            errors = int(np.count_nonzero(visible & (pixels != banks[i][shown[i]])))
            port_screens.append(dict(prefix=str(prefix), checked_pixels=int(visible.sum()),
                                     wrong_visible_penids=errors, calibration_colors=len(colors)))
    full_screen = None
    if args.verify_full_port_images:
        screenshots = [np.asarray(Image.open(io.BytesIO(read(Path(str(p)+'.png')))).convert('RGB'))
                       for p in (args.control,args.experiment)]
        if screenshots[0].shape != screenshots[1].shape:
            raise ValueError('port screenshot dimensions differ')
        full_screen = dict(compared_pixels=int(screenshots[0].shape[0]*screenshots[0].shape[1]),
                           different_pixels=int(np.count_nonzero(np.any(screenshots[0]!=screenshots[1],axis=2))))
    a, b = banks[0][shown[0]], banks[1][shown[1]]
    report = dict(compared_pixels=int(a.size),
                  different_pens=int(np.count_nonzero(a != b)),
                  opaque_pixels=int(np.count_nonzero((a != 0) & (a != 255))),
                  covered_sprite_pixels=int(np.count_nonzero(foreground & (owners >= 0) & (a == 255))),
                  control_through_pen_pixels=int(np.count_nonzero(a == 255)),
                  experiment_through_pen_pixels=int(np.count_nonzero(b == 255)))
    native_screen = None
    if args.verify_native_image:
        native_owner = owners.copy()
        native_owner[foreground | text_mask] = -1
        native_owner[208:] = -1
        palette = read(Path(str(args.planes) + '_palette.bin'))
        image = Path(str(args.planes) + '.png')
        read(image)
        native_screen = native_image_check(native_pens, native_owner, records, palette, image)
    result = dict(scope='exact shown framebuffer pens selected by actual flip trace; not cadence or gameplay',
                  input_proof=proof, shown=report, native_sprite_checks=native_checks,
                  native_reference_screen=native_screen, port_screens=port_screens,
                  full_port_screen=full_screen, inputs=inputs)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    screen_ok = native_screen is None or all(r['visible_pixels'] == r['rgb_equal'] for r in native_screen)
    passed = (screen_ok and (full_screen is None or full_screen['different_pixels']==0) and report['different_pens'] == 0 and
              report['covered_sprite_pixels'] > 0 and report['opaque_pixels'] > 0 and
              all(c['passed'] for c in native_checks) and
              all(c['wrong_visible_penids'] == 0 for c in port_screens))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
