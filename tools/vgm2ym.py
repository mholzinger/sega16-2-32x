#!/usr/bin/env python3
"""VGZ/VGM -> YM2151 event segment for the transcode pipeline. KIT.

VGM logs are the authoritative, COMPLETE song: the exact YM2151 register
writes from the real S16B hardware WITH an explicit loop point. This
parses a .vgz (gzipped VGM), returns the seg dict tools/soundmap_build
.transcode consumes, plus the loop point in seconds — so tracks are
complete and loop seamlessly (SOUND_DRIVER.md), no capture/attract
guesswork.

Reference: VGM spec 1.50. Commands used by S16B logs: 0x54 aa dd
(YM2151 reg=val), 0x50 dd (SN76489/PSG), waits (0x61 nnnn / 0x62 /
0x63 / 0x7n), 0x66 (end), 0x67 (data block, skipped — PCM handled by
pcm_from_rom).
"""
import gzip
import struct


def load(path):
    """-> (seg, loop_seconds or None, total_seconds). seg has YM2151
    events as (t, reg, val) in seconds, ready for transcode()."""
    d = gzip.decompress(open(path, "rb").read())
    if d[:4] != b"Vgm ":
        raise ValueError("not a VGM: " + path)
    u32 = lambda o: struct.unpack("<I", d[o:o + 4])[0]
    ver = u32(0x08)
    total = u32(0x18)
    loop_off = u32(0x1C)                      # relative to 0x1C
    loop_samp = u32(0x20)
    data_off = (u32(0x34) + 0x34) if ver >= 0x150 and u32(0x34) else 0x40
    loop_abs = (loop_off + 0x1C) if loop_off else None

    RATE = 44100.0
    seg = {"cmd": None, "t0": 0.0, "t1": None,
           "ym": [], "pc": [], "pd": [], "shadow": [0] * 256}
    i = data_off
    samples = 0
    loop_t = None
    while i < len(d):
        if loop_abs is not None and loop_t is None and i >= loop_abs:
            loop_t = samples / RATE
        c = d[i]
        if c == 0x54:                          # YM2151 write
            seg["ym"].append((samples / RATE, d[i + 1], d[i + 2]))
            i += 3
        elif c == 0x50:                        # PSG (SN76489)
            i += 2
        elif c == 0x61:
            samples += d[i + 1] | (d[i + 2] << 8)
            i += 3
        elif c == 0x62:
            samples += 735
            i += 1
        elif c == 0x63:
            samples += 882
            i += 1
        elif 0x70 <= c <= 0x7F:
            samples += (c & 0x0F) + 1
            i += 1
        elif 0x80 <= c <= 0x8F:                # YM2612 DAC + wait (n/a here)
            samples += c & 0x0F
            i += 1
        elif c == 0x66:
            break
        elif c == 0x67:                        # data block: skip
            blk = struct.unpack("<I", d[i + 3:i + 7])[0]
            i += 7 + blk
        elif c in (0x4F, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57, 0x58, 0x59,
                   0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F):
            i += 3                             # other 2-operand chip writes
        elif c == 0x31:
            i += 2
        elif c in (0x90, 0x91, 0x95):
            i += 5
        elif c in (0x92,):
            i += 6
        elif c == 0x93:
            i += 11
        elif c == 0x94:
            i += 2
        else:
            i += 1
    return seg, loop_t, samples / RATE


if __name__ == "__main__":
    import sys
    seg, loop_t, total = load(sys.argv[1])
    print(f"{len(seg['ym'])} YM2151 writes, total {total:.1f}s, "
          f"loop at {loop_t:.1f}s" if loop_t else f"no loop ({total:.1f}s)")
