#!/usr/bin/env python3
"""Rebuild uPD7759 speech as WAV from a tools/snd_tap.lua log (SOUND.md P1).

The arcade board runs the 7759 in SLAVE mode: the Z80 feeds the byte
stream by hand (port 0x80 = PD lines in the log), so the sample ROMs
carry no usable index — the tap log IS the ground truth for both the
bytes and the utterance boundaries. This decoder replays MAME's slave
state machine over the log and writes one WAV per utterance plus a
manifest tagging each with the sound-command byte(s) that preceded it.

Decode spec derived from upstream MAME src/devices/sound/upd7759.cpp
(BSD-3-Clause; fetched into ~/src/mame-ref 2026-09-01) — the step/state
tables, the block grammar (00 end / 00dddddd silence / 01ffffff 256
nibbles / 10ffffff nn n+1 nibbles / 11---rrr repeat), the 5-byte slave
prelude (last_sample, dummy1, addr_msb, addr_lsb, dummy2), and the
timing: one nibble lasts (f+1)*4 clocks of 640 kHz, silence lasts
1024*(d+1) clocks. We synthesize on the 160 kHz quarter-clock grid and
box-decimate by 10 to 16 kHz — the PWM mixer's native rate.

Control port (PC, port 0x40) semantics per segas16b.cpp:1151-1219:
bit7=1 -> /MD low (slave mode start edge), bit6 -> /RESET (1 = running,
1->0 edge hard-resets the chip), low bits = sample ROM bank (unused
here — the byte stream already went through the Z80).

Usage: upd7759_decode.py TAP_LOG OUTDIR [--rate 16000]
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import wave

# upd7759.cpp:327-345 (upd775x_step), :347 (upd775x_state_table)
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
STATE_ADJ = [-1, -1, 0, 0, 1, 2, 2, 3, -1, -1, 0, 0, 1, 2, 2, 3]

TICK_HZ = 160000  # 640 kHz / 4: one tick per nibble-clock unit


class SlaveDecoder:
    """Replays upd7759.cpp advance_state() for MODE_SLAVE, fed one PD
    byte at a time. Produces (value, ticks) runs on the 160 kHz grid."""

    # fifo-consuming states
    LAST_SAMPLE, DUMMY1, ADDR_MSB, ADDR_LSB, DUMMY2 = range(5)
    HEADER, NIB_COUNT, NIB_MSN = 5, 6, 7
    IDLE = 8

    def __init__(self):
        self.state = self.LAST_SAMPLE
        self.adpcm_state = 0
        self.sample = 0
        self.first_valid = False
        self.rate = 0          # (f+1)
        self.nibbles_left = 0
        self.runs = []         # (sample_value, ticks)
        self.done = False
        self.saw_repeat = False

    def _nib(self, n):
        self.sample += STEP[self.adpcm_state][n]
        self.adpcm_state = min(15, max(0, self.adpcm_state + STATE_ADJ[n]))
        self.runs.append((self.sample, self.rate))

    def feed(self, b):
        s = self.state
        if s == self.LAST_SAMPLE:
            # slave req_sample is 0x10; a smaller byte aborts (upd7759.cpp:426)
            self.state = self.DUMMY1 if b >= 0x10 else self.IDLE
            self.done = b < 0x10
        elif s == self.DUMMY1:
            self.state = self.ADDR_MSB
        elif s == self.ADDR_MSB:
            self.state = self.ADDR_LSB
        elif s == self.ADDR_LSB:
            self.state = self.DUMMY2
        elif s == self.DUMMY2:
            self.first_valid = False
            self.state = self.HEADER
        elif s == self.HEADER:
            kind = b & 0xC0
            if kind == 0x00:
                if b == 0 and self.first_valid:
                    self.state = self.IDLE
                    self.done = True
                else:
                    # silence: 1024*(d+1) clocks = 256*(d+1) ticks
                    self.sample = 0
                    self.adpcm_state = 0
                    self.runs.append((0, 256 * ((b & 0x3F) + 1)))
            elif kind == 0x40:
                self.rate = (b & 0x3F) + 1
                self.nibbles_left = 256
                self.state = self.NIB_MSN
            elif kind == 0x80:
                self.rate = (b & 0x3F) + 1
                self.state = self.NIB_COUNT
            else:  # 0xC0 repeat: meaningless in slave (rewinds a ROM
                # offset the slave never uses); flag it if a driver emits it
                self.saw_repeat = True
            if b != 0:
                self.first_valid = True
        elif s == self.NIB_COUNT:
            self.nibbles_left = b + 1
            self.state = self.NIB_MSN
        elif s == self.NIB_MSN:
            self._nib(b >> 4)
            self.nibbles_left -= 1
            if self.nibbles_left == 0:
                self.state = self.HEADER
            else:
                self._nib(b & 15)
                self.nibbles_left -= 1
                self.state = self.HEADER if self.nibbles_left == 0 else self.NIB_MSN
        # IDLE: ignore


def runs_to_pcm(runs, out_rate):
    """Expand (value, ticks) runs at 160 kHz and box-decimate to out_rate."""
    dec = TICK_HZ // out_rate
    assert TICK_HZ % out_rate == 0
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


def write_wav(path, pcm, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        # chip output is ~9-bit signed; <<6 keeps 2 bits of headroom
        w.writeframes(b"".join(
            struct.pack("<h", max(-32768, min(32767, v << 6))) for v in pcm))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("outdir")
    ap.add_argument("--rate", type=int, default=16000)
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    utts = []          # dicts: t0, cmds, decoder
    cur = None
    recent_cmds = []   # (time, cmd) latch reads, pruned to 1s
    md = 1             # /MD line state (1 = high = standalone/idle)
    reset = 0

    with open(args.log) as f:
        for line in f:
            parts = line.split()
            if len(parts) != 3:
                continue
            t, kind, val = float(parts[0]), parts[1], int(parts[2], 16)
            if kind == "CM":
                if val != 0:
                    recent_cmds.append((t, val))
                    recent_cmds = [(tt, c) for tt, c in recent_cmds if t - tt < 1.0]
            elif kind == "PC":
                new_md = 0 if (val & 0x80) else 1
                new_reset = 1 if (val & 0x40) else 0
                if reset and not new_reset and cur is not None:
                    utts.append(cur)      # hard reset ends the utterance
                    cur = None
                if md and not new_md and new_reset:
                    if cur is not None:
                        utts.append(cur)
                    cur = {"t0": t,
                           "cmds": [c for _, c in recent_cmds[-3:]],
                           "dec": SlaveDecoder()}
                md, reset = new_md, new_reset
            elif kind == "PD" and cur is not None:
                cur["dec"].feed(val)
                if cur["dec"].done:
                    utts.append(cur)
                    cur = None
    if cur is not None:
        utts.append(cur)

    manifest = []
    seen = {}
    idx = 0
    for u in utts:
        pcm = runs_to_pcm(u["dec"].runs, args.rate)
        if len(pcm) < args.rate // 100:   # <10ms: control noise, skip
            continue
        digest = hashlib.sha1(
            b"".join(struct.pack("<i", v) for v in pcm)).hexdigest()[:12]
        entry = {
            "index": idx,
            "t0": round(u["t0"], 4),
            "cmds_before": [f"0x{c:02X}" for c in u["cmds"]],
            "ms": int(1000 * len(pcm) / args.rate),
            "sha": digest,
            "dup_of": seen.get(digest),
            "repeat_blocks": u["dec"].saw_repeat,
        }
        if digest not in seen:
            seen[digest] = idx
            entry["wav"] = f"utt_{idx:03d}.wav"
            write_wav(os.path.join(args.outdir, entry["wav"]), pcm, args.rate)
        manifest.append(entry)
        idx += 1

    with open(os.path.join(args.outdir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    uniq = len(seen)
    print(f"{len(manifest)} utterances, {uniq} unique -> {args.outdir}")
    if any(e["repeat_blocks"] for e in manifest):
        print("WARNING: 0xC0 repeat blocks seen in slave stream — decoder "
              "treats them as no-ops; verify those utterances by ear",
              file=sys.stderr)


if __name__ == "__main__":
    main()
