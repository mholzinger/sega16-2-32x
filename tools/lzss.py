#!/usr/bin/env python3
"""LZSS codec for streamable 68K decompression. KIT (2026-09-01).

The music opcode streams compress ~3.6x with plain LZSS — enough to hold
every full-length track inside the 32X's 512KB 68K ROM window without
banking (SOUND_DRIVER.md). This is the reference compressor + a Python
decoder for round-trip verification; the 68K streaming decoder
(sndtest/z80 or md side) implements the exact same format.

FORMAT (byte stream):
  Repeat: one FLAG byte, then 8 items, LSB of flag first.
    flag bit 0 -> LITERAL: copy the next 1 input byte to output.
    flag bit 1 -> MATCH: next 2 input bytes = (hi<<8)|lo =
                  (offset-1)<<4 | (length-MINMATCH).
                  offset in 1..WIN (distance back into output),
                  length in MINMATCH..MINMATCH+15.
  The last flag byte may describe fewer than 8 items (input ends).
Decoder keeps a WIN-byte sliding window of recent output; a match copies
byte-by-byte (overlap allowed, like classic LZSS/LZ77).
"""
import argparse

WIN = 4096
MINMATCH = 3
MAXMATCH = MINMATCH + 15


def compress(data):
    out = bytearray()
    i = 0
    n = len(data)
    # hash chains for speed: 3-byte prefix -> list of positions
    from collections import defaultdict
    chains = defaultdict(list)
    flags_pos = None
    flags = 0
    bit = 8
    pend = bytearray()

    def flush():
        nonlocal flags, bit, pend
        out.append(flags)
        out.extend(pend)
        flags = 0
        bit = 0
        pend.clear()

    bit = 0
    while i < n:
        if bit == 8:
            flush()
        best_len = 0
        best_off = 0
        if i + MINMATCH <= n:
            key = data[i:i + 3]
            for j in reversed(chains.get(bytes(key), ())):
                if i - j > WIN:
                    break
                L = 0
                m = min(MAXMATCH, n - i)
                while L < m and data[j + L] == data[i + L]:
                    L += 1
                if L > best_len:
                    best_len = L
                    best_off = i - j
                    if L == MAXMATCH:
                        break
        if best_len >= MINMATCH:
            tok = ((best_off - 1) << 4) | (best_len - MINMATCH)
            pend.append((tok >> 8) & 0xFF)
            pend.append(tok & 0xFF)
            flags |= (1 << bit)
            # register hashed positions we pass over
            for k in range(i, i + best_len):
                if k + 3 <= n:
                    chains[bytes(data[k:k + 3])].append(k)
            i += best_len
        else:
            pend.append(data[i])
            if i + 3 <= n:
                chains[bytes(data[i:i + 3])].append(i)
            i += 1
        bit += 1
    if bit:
        flush()
    return bytes(out)


def decompress(data):
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        flags = data[i]
        i += 1
        for b in range(8):
            if i >= n:
                break
            if flags & (1 << b):
                tok = (data[i] << 8) | data[i + 1]
                i += 2
                off = (tok >> 4) + 1
                length = (tok & 0xF) + MINMATCH
                start = len(out) - off
                for k in range(length):
                    out.append(out[start + k])
            else:
                out.append(data[i])
                i += 1
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("infile")
    ap.add_argument("-o", "--out")
    ap.add_argument("-d", "--decompress", action="store_true")
    args = ap.parse_args()
    data = open(args.infile, "rb").read()
    res = decompress(data) if args.decompress else compress(data)
    if args.out:
        open(args.out, "wb").write(res)
    print(f"{len(data)} -> {len(res)} ({len(data)/max(1,len(res)):.2f}x)")


if __name__ == "__main__":
    main()
