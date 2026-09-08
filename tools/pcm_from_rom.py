#!/usr/bin/env python3
"""Decode uPD7759 ADPCM samples straight out of a System 16B sample ROM to WAV.

This is the ROM-side twin of tools/upd7759_decode.py. That tool rebuilds
speech from a *playback tap* (the byte stream the Z80 hand-fed the chip in
slave mode); this one reads the same utterances from the *sample ROM* the
Z80 was streaming, with no board running. Both share one ADPCM engine and
produce comparable 16 kHz mono WAVs.

  pcm_from_rom.py SAMPLE_ROM [SAMPLE_ROM2 ...] --out DIR [--rate 16000]

Algorithm and ROM layout are DERIVED (never copied) from the GPL jt7759
FPGA core (srcref/jtcores/modules/jt7759/hdl/*.v) and cross-checked against
MAME's BSD-licensed upd7759.cpp. See the block comments below for the
file:line citations that establish each hardware fact.

------------------------------------------------------------------------
ROM LAYOUT (Sega System 16B, opr-11672.a11 / opr-11673.a12)
------------------------------------------------------------------------
These ROMs do NOT carry a NEC master-mode header. The canonical uPD7759
master header is "nn 5a a5 69 55" + a table of 2-byte big-endian pointers,
sample i's data at 2*ptr(i)+1  (jt7759_ctrl.v:83-88 hold the 5a a5 69 55
signature; jt7759_ctrl.v:179 forms the table address {0,din+2,1} = 2*din+5;
jt7759_ctrl.v:216-218 load the data pointer as {addr_latch,1} = 2*ptr+1;
upd7759.cpp:80-92 and :442/:453 document the same layout). Applying that
formula here yields garbage pointers, because on this board the uPD7759
runs in SLAVE mode: the ROM reader is disconnected and the Z80 hand-feeds
bytes (upd7759.cpp:94-105; segas16b.cpp:1151-1217 wires /MD, /RESET and the
0x4000-granular sample-ROM bank to the control port). So the sample index
lives in the Z80 program, not in a chip-readable header.

What IS in the ROM, verified empirically and by matching every utterance in
sndtest/speech to ncc >= 0.97, is a back-to-back run of samples, each:

    [ ff 00 00 00 00 ]  5-byte SLAVE PRELUDE
    [ block ][ block ]... ADPCM block stream
    [ 00 ]              terminator (a 0x00 header after >=1 valid block)
    [ ff / 00 ... ]     padding to the next sample

The 5 prelude bytes are exactly what the chip's slave state machine eats
before the first block: last_sample, dummy1, addr_msb, addr_lsb, dummy2
(upd7759.cpp:420-471 STATE_LAST_SAMPLE..STATE_DUMMY2; mirrored by
tools/upd7759_decode.py:64). Sega stores them as literal ff 00 00 00 00 so
the Z80 can DMA a sample as one contiguous span. We locate samples by that
prelude and parse contiguously, decoding each block stream to its 0x00
terminator, then skipping padding to the next prelude. Parsing forward past
each decoded extent (rather than pattern-matching everywhere) steps over the
prelude byte-pattern where it recurs by chance inside ADPCM data.

------------------------------------------------------------------------
BLOCK GRAMMAR  (upd7759.cpp:44-54; jt7759_ctrl.v:238-268)
------------------------------------------------------------------------
    00000000                 sample end (only after a valid block)
    00dddddd                 silence, 1024*(d+1) chip clocks; resets DAC
    01ffffff                 256 nibbles, sample-rate divisor f+1
    10ffffff nnnnnnnn        n+1 nibbles, divisor f+1
    11---rrr [hdr] ...       repeat the following block r+1 times
"ffffff" is the ADPCM clock divisor: each nibble lasts (f+1)*4 clocks of
the 640 kHz master (upd7759.cpp:537 m_clocks_left = m_sample_rate*4), i.e.
the DAC advances at 160000/(f+1) Hz (jt7759_div.v divides the 160 kHz
cen by divby). Silence spans 1024*(d+1) clocks = 256*(d+1) of our 160 kHz
ticks (upd7759.cpp:488). The repeat block (0xC0) is not used by these ROMs.

------------------------------------------------------------------------
ADPCM CONVERTER  (jt7759_adpcm.v:45-73 + LUT :77-140; upd7759.cpp:327-365)
------------------------------------------------------------------------
Per nibble: sample += STEP[state][nibble]; state += STATE_ADJ[nibble],
state clamped to 0..15. STEP is the 16x16 signed step LUT (identical in
jt7759 doc/lut.c and upd7759.cpp:327-345); STATE_ADJ is upd7759.cpp:347 /
jt7759_adpcm.v st_lut:71-72. jt7759 additionally saturates the running DAC
to signed 9 bits (jt7759_adpcm.v:63-65); MAME lets the accumulator run as a
plain int, and so do we, to stay bit-comparable with upd7759_decode.py and
the sndtest/speech corpus that gates this port.
"""
import argparse
import os
import struct
import wave

# 16x16 signed ADPCM step LUT. Rows are the 4-bit adaptive state, columns
# the 4-bit nibble. DERIVED from jt7759 doc/lut.c (== jt7759_adpcm.v lut[]
# :77-140) and identical to upd7759.cpp:327-345 upd775x_step[][].
STEP = [
    [0, 0, 1, 2, 3, 5, 7, 10, 0, 0, -1, -2, -3, -5, -7, -10],
    [0, 1, 2, 3, 4, 6, 8, 13, 0, -1, -2, -3, -4, -6, -8, -13],
    [0, 1, 2, 4, 5, 7, 10, 15, 0, -1, -2, -4, -5, -7, -10, -15],
    [0, 1, 3, 4, 6, 9, 13, 19, 0, -1, -3, -4, -6, -9, -13, -19],
    [0, 2, 3, 5, 8, 11, 15, 23, 0, -2, -3, -5, -8, -11, -15, -23],
    [0, 2, 4, 7, 10, 14, 19, 29, 0, -2, -4, -7, -10, -14, -19, -29],
    [0, 3, 5, 8, 12, 16, 22, 33, 0, -3, -5, -8, -12, -16, -22, -33],
    [1, 4, 7, 10, 15, 20, 29, 43, -1, -4, -7, -10, -15, -20, -29, -43],
    [1, 4, 8, 13, 18, 25, 35, 53, -1, -4, -8, -13, -18, -25, -35, -53],
    [1, 6, 10, 16, 22, 31, 43, 64, -1, -6, -10, -16, -22, -31, -43, -64],
    [2, 7, 12, 19, 27, 37, 51, 76, -2, -7, -12, -19, -27, -37, -51, -76],
    [2, 9, 16, 24, 34, 46, 64, 96, -2, -9, -16, -24, -34, -46, -64, -96],
    [3, 11, 19, 29, 41, 57, 79, 117, -3, -11, -19, -29, -41, -57, -79, -117],
    [4, 13, 24, 36, 50, 69, 96, 143, -4, -13, -24, -36, -50, -69, -96, -143],
    [4, 16, 29, 44, 62, 85, 118, 175, -4, -16, -29, -44, -62, -85, -118, -175],
    [6, 20, 36, 54, 76, 104, 144, 214, -6, -20, -36, -54, -76, -104, -144, -214],
]
# State delta per nibble. upd7759.cpp:347 upd775x_state_table[];
# jt7759_adpcm.v st_lut[] :71-72 (mirrored for the high 8 nibbles).
STATE_ADJ = [-1, -1, 0, 0, 1, 2, 2, 3, -1, -1, 0, 0, 1, 2, 2, 3]

# 640 kHz master / 4: one tick per nibble-clock unit. A nibble of divisor
# (f+1) holds for (f+1) ticks; box-decimating to 16 kHz averages 10 ticks.
# (upd7759.cpp:537 nibble = sample_rate*4 clocks of the 640 kHz master.)
TICK_HZ = 160000

PRELUDE = b"\xff\x00\x00\x00\x00"   # slave prelude, see module docstring


def decode_stream(rom, start):
    """Decode one ADPCM block stream beginning at the 5-byte prelude at
    `start`. Returns (runs, byte_len) where runs is a list of
    (dac_value, ticks_at_160kHz), or None if `start` is not a prelude.

    Mirrors upd7759.cpp advance_state() STATE_BLOCK_HEADER..NIBBLE_LSN
    (:474-553) unrolled: we don't model chip timing, only DAC values and
    their durations, which is all the WAV needs."""
    if rom[start:start + 5] != PRELUDE:
        return None
    i = start + 5                       # step over the 5 prelude bytes
    n = len(rom)
    sample = 0
    state = 0
    first_valid = False                 # upd7759.cpp:465 m_first_valid_header
    runs = []

    def nib(v):
        nonlocal sample, state
        sample += STEP[state][v]
        state += STATE_ADJ[v]
        if state < 0:
            state = 0
        elif state > 15:
            state = 15
        runs.append((sample, rate))

    while i < n:
        b = rom[i]
        i += 1
        kind = b & 0xC0
        if kind == 0x00:                # end / silence  (upd7759.cpp:487)
            if b == 0 and first_valid:
                break                   # sample-end terminator
            sample = 0                  # silence resets the DAC + state
            state = 0
            runs.append((0, 256 * ((b & 0x3F) + 1)))    # 1024*(d+1) clocks
        elif kind == 0x40:              # 256 nibbles   (upd7759.cpp:494)
            rate = (b & 0x3F) + 1
            left = 256
            while left and i < n:
                d = rom[i]
                i += 1
                nib(d >> 4)
                left -= 1
                if left:
                    nib(d & 0x0F)
                    left -= 1
        elif kind == 0x80:              # n+1 nibbles   (upd7759.cpp:501)
            rate = (b & 0x3F) + 1
            if i >= n:
                break
            left = rom[i] + 1
            i += 1
            while left and i < n:
                d = rom[i]
                i += 1
                nib(d >> 4)
                left -= 1
                if left:
                    nib(d & 0x0F)
                    left -= 1
        else:                           # 0xC0 repeat: unused by these ROMs
            pass
        if b != 0:
            first_valid = True
    return runs, i - start


def runs_to_pcm(runs, out_rate):
    """Expand (value, ticks) runs on the 160 kHz grid and box-decimate to
    out_rate. Same resampler as tools/upd7759_decode.py:137."""
    dec = TICK_HZ // out_rate
    if TICK_HZ % out_rate:
        raise SystemExit("--rate must divide %d evenly" % TICK_HZ)
    pcm = []
    acc = 0
    acc_n = 0
    for val, ticks in runs:
        t = ticks
        while t:
            take = min(t, dec - acc_n)
            acc += val * take
            acc_n += take
            t -= take
            if acc_n == dec:
                pcm.append(acc // dec)
                acc = 0
                acc_n = 0
    return pcm


def find_samples(rom):
    """Greedy contiguous scan for prelude-delimited samples. Decoding each
    stream to its terminator and only THEN scanning padding for the next
    prelude steps over prelude byte-patterns that recur inside ADPCM data.
    Returns a list of (offset, byte_len, runs)."""
    out = []
    i = 0
    n = len(rom)
    while i < n - 5:
        if rom[i:i + 5] != PRELUDE:
            i += 1
            continue
        res = decode_stream(rom, i)
        if not res:
            i += 1
            continue
        runs, blen = res
        out.append((i, blen, runs))
        j = i + blen                    # skip this sample's decoded extent...
        while j < n - 5 and rom[j:j + 5] != PRELUDE:
            j += 1                       # ...then the padding, to next start
        i = j
    return out


def write_wav(path, pcm, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        # DAC is ~9-bit signed; <<6 matches upd7759_decode.py so these WAVs
        # are directly comparable to the sndtest/speech corpus.
        w.writeframes(b"".join(
            struct.pack("<h", max(-32768, min(32767, v << 6))) for v in pcm))


def peak(pcm):
    return max((abs(v) for v in pcm), default=0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("roms", nargs="+", metavar="SAMPLE_ROM")
    ap.add_argument("--out", required=True, metavar="DIR")
    ap.add_argument("--rate", type=int, default=16000)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    idx = 0
    print("idx  source        offset   bytes  samples    ms   peak  wav")
    for path in args.roms:
        rom = open(path, "rb").read()
        stem = os.path.splitext(os.path.basename(path))[0]
        samples = find_samples(rom)
        for off, blen, runs in samples:
            pcm = runs_to_pcm(runs, args.rate)
            if not pcm:
                continue                # e.g. an all-0xFF empty bank
            ms = round(1000 * len(pcm) / args.rate)
            name = "sample_%02d.wav" % idx
            write_wav(os.path.join(args.out, name), pcm, args.rate)
            print("%3d  %-12s  0x%05x  %6d  %7d  %5d  %5d  %s"
                  % (idx, stem, off, blen, len(pcm), ms, peak(pcm), name))
            idx += 1
    print("\n%d samples written to %s" % (idx, args.out))


if __name__ == "__main__":
    main()
