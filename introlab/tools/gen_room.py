#!/usr/bin/env python3
"""The launcher's room sounds: short FM effects that the launcher's 68K program fires at
random moments on three FM channels, as if from other machines across the arcade.

    gen_room.py titles/<title>.json ROOM_DIR OUT.bin BUDGET_BYTES

ROOM_DIR holds se_NN.csv register logs (frame, part, register, value) captured from the
owner's own cartridge; they are never tracked. The title's json names which ones to use
("room": {"effects": [...], "quieter": TL steps, "every_frames": mean gap}). Without the
logs the output is an empty table and the room is quiet.

Each effect was played by its game on FM channel 3, and there are two kinds. One that uses
the chip's channel-3 mode (a pitch of its own for each operator, registers 0xA8-0xAE, mode
bit 0x40 of register 0x27) can only ever play on channel 3: it is stored as it was written
(kind 1). The rest are stored for "a channel" (kind 0): register numbers with the channel
bits removed and the key-on with only its operator bits, and the player adds one of
channels 4-6. Either way every operator level is pushed down by `quieter` (duller as well
as softer, which is what distance does), and the player picks left, right or centre.

Output: count, mean gap in frames, the crowd murmur's level (the carriers' TL, 0 = no
murmur: "murmur" in the json), count offsets (16-bit, from the start of the table),
then the effects: the kind byte, then per frame one byte n and n (register, value) pairs;
0x80 | k skips k silent frames; 0xFF ends."""
import json, os, sys

def effect(path, quieter):
    rows = [tuple(map(int, l.split(','))) for l in open(path) if l[0] != '#']
    own = any(p == 0 and (0xA8 <= r <= 0xAE or (r == 0x27 and v & 0xC0)) for f, p, r, v in rows)
    frames = {}
    for f, part, reg, val in rows:
        if part != 0: continue
        if reg == 0x28:
            if (val & 7) != 2: continue
            w = (0x28, val if own else val & 0xF0)
        elif (0x30 <= reg <= 0xB6 and (reg & 3) == 2) or (own and 0xA8 <= reg <= 0xAE):
            if 0x40 <= reg < 0x50: val = min(127, (val & 0x7F) + quieter)
            w = (reg if own else reg - 2, val)
        else: continue
        frames.setdefault(f, []).append(w)
    if not frames: return b''
    out = bytearray([1 if own else 0]); last = min(frames) - 1; shadow = {}
    for f in sorted(frames):
        ws = []
        for reg, val in frames[f]:          # a repeated value is dropped, except where the write itself is the event
            if reg != 0x28 and not 0xA0 <= reg < 0xB0 and shadow.get(reg) == val: continue
            shadow[reg] = val; ws.append((reg, val))
        if not ws: continue
        gap = f - last - 1
        while gap > 0: k = min(gap, 127); out.append(0x80 | k); gap -= k
        for i in range(0, len(ws), 0x7E):
            part = ws[i:i + 0x7E]
            out.append(len(part)); out += bytes(b for w in part for b in w)
            if i + 0x7E < len(ws): pass     # more writes of the same frame follow in the next group: the player takes one group a frame
        last = f
    out.append(0xFF)
    return bytes(out)

def main():
    cfg = json.load(open(sys.argv[1])).get('room', {})
    src, out, budget = sys.argv[2], sys.argv[3], int(sys.argv[4])
    picks = [n for n in cfg.get('effects', []) if os.path.exists(os.path.join(src, 'se_%02d.csv' % n))]
    bodies, used = [], []
    for n in picks:
        b = effect(os.path.join(src, 'se_%02d.csv' % n), cfg.get('quieter', 12))
        if not b: continue
        size = 6 + 2 * (len(bodies) + 1) + sum(map(len, bodies)) + len(b)
        if size > budget: print('room: effect %02d left out, over the %d B budget' % (n, budget)); continue
        bodies.append(b); used.append(n)
    table = bytearray(len(bodies).to_bytes(2, 'big') + int(cfg.get('every_frames', 200)).to_bytes(2, 'big') + int(cfg.get('murmur', 0)).to_bytes(2, 'big'))
    at = 6 + 2 * len(bodies)
    for b in bodies: table += at.to_bytes(2, 'big'); at += len(b)
    for b in bodies: table += b
    if len(table) & 1: table.append(0)
    open(out, 'wb').write(table)
    print('room: %d effects %s (channel-3 kind: %s), %d B' % (len(bodies), used, [n for n, b in zip(used, bodies) if b[0]], len(table)))

if __name__ == '__main__':
    main()
