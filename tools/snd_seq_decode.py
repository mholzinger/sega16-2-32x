#!/usr/bin/env python3
"""Sega System 16B sound-driver sequence decoder — KIT (2026-09-01).

Reads a S16B Z80 sound ROM and decodes its music/sfx SEQUENCE DATA
directly, so the 32X sound pipeline gets faithful tracks from the bytes
Sega shipped — NOT from tapping playback (the isolation-sweep tap dropped
the driver's per-note envelope re-application; docs/sound/SOUND.md / HANDOFF-SOUND).

Reverse-engineered from epr-11671 (Altered Beast, English + Japanese
share this exact ROM — MAME segas16b.cpp lines 39-45). Every address
here is DERIVED from the ROM by disassembly (tools/z80dis.py), never
copied. See docs/sound/SOUND_DRIVER.md for the full driver map + citations.

STATUS: stage 1 COMPLETE (song table + song headers + channel stream
pointers, all validated against the ROM). Stage 2 (per-channel note/
opcode interpreter -> YM/PSG/PCM event stream) is IN PROGRESS — the
interpreter lives at driver $0D03 (patch loader) + the note loop; the
opcode table below is filled in as each is confirmed.

  snd_seq_decode.py SOUND_ROM [--config games/<title>.toml] [--song 0x94]
Without --song, lists every song and its channel streams.
"""
import argparse

# ---- Per-title driver constants (Altered Beast / S16B default) --------
# All confirmed by disassembly of epr-11671; a per-title TOML overrides
# these for other boards (same driver family, possibly relocated tables).
CFG = {
    "song_table": 0x03B4,   # word[song_table + 2*(cmd & 0x7F)] = song ptr
    "cmd_mask":   0x7F,     # driver $02E3: `and $7F`
    "music_cmds": range(0x90, 0x98),   # 0x90-0x97 -> real songs (verified)
    # song header: byte0 = channel count N, then N * 9-byte records:
    #   [flags, chan_id, b2, ptr_lo, ptr_hi, patch, 0, 0, 0]
    "hdr_rec_len": 9,
    "hdr_ptr_ofs": 3,       # ptr_lo/ptr_hi at record offset 3-4
}


def u16(d, i):
    return d[i] | (d[i + 1] << 8)


def song_ptr(rom, cmd, cfg):
    idx = cmd & cfg["cmd_mask"]
    return u16(rom, cfg["song_table"] + 2 * idx)


def parse_header(rom, ptr, cfg):
    """-> list of channel dicts. The header is N 9-byte records; each
    points at a sequence stream that the driver's interpreter walks."""
    n = rom[ptr]
    chans = []
    off = ptr + 1
    for c in range(n):
        rec = rom[off:off + cfg["hdr_rec_len"]]
        if len(rec) < cfg["hdr_rec_len"]:
            break
        seq = rec[cfg["hdr_ptr_ofs"]] | (rec[cfg["hdr_ptr_ofs"] + 1] << 8)
        chans.append({
            "index": c, "flags": rec[0], "chan_id": rec[1], "b2": rec[2],
            "seq_ptr": seq, "patch": rec[5],
        })
        off += cfg["hdr_rec_len"]
    return n, chans


# ---- Stage 2 (IN PROGRESS): sequence opcode interpreter ---------------
# The driver interpreter is at $0D03 (patch loader, reg/val pairs ending
# 0x02) plus the note loop. Confirmed opcode facts so far (SOUND_DRIVER):
#   0x02          = end of stream (interpreter RETs)
#   0x03          = jump/loop (handler at $0D84)
#   0x60/68/70/78 = cache an operator envelope reg into channel state
#                   (ix+28/30/29/..) so it re-applies per note
# The note/duration encoding and the instrument-patch table address are
# the remaining unknowns; decode_stream stays a stub until confirmed so
# it can never emit a silently-wrong stream.
def decode_stream(rom, seq_ptr, cfg):
    raise NotImplementedError(
        "stage 2: note/opcode interpreter not yet confirmed — see "
        "docs/sound/SOUND_DRIVER.md 'remaining work'")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--song", type=lambda x: int(x, 0), default=None)
    args = ap.parse_args()
    rom = open(args.rom, "rb").read()
    cfg = CFG
    cmds = [args.song] if args.song is not None else list(cfg["music_cmds"])
    for cmd in cmds:
        sp = song_ptr(rom, cmd, cfg)
        n, chans = parse_header(rom, sp, cfg)
        print(f"cmd 0x{cmd:02X}  song@{sp:04X}  {n} channels:")
        for ch in chans:
            print(f"    ch{ch['index']}: id={ch['chan_id']:02X} "
                  f"flags={ch['flags']:02X} patch={ch['patch']:02X} "
                  f"seq@{ch['seq_ptr']:04X}")


if __name__ == "__main__":
    main()
