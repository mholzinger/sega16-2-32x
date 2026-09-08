#!/usr/bin/env python3
"""Build the arcade command map + per-command sound data (docs/sound/SOUND.md P4).

Input: a cmd_sweep tap log — every command byte 0x01-0xFF injected into
the sound latch in isolation, 10s captured each, IJ/IX marker lines.

Per command segment this tool:
- counts YM key-ons + uPD7759 (PD) bytes and finds the activity span,
- classifies: MUSIC (keys on and still active at window end — loops),
  YMSFX (keys on, finishes early), SPEECH (PD stream dominates),
  NONE (silent / control),
- transcodes MUSIC/YMSFX segments through tools/opm2opn.py's
  Transcoder (music cuts loop by source-wrap; sfx cuts get the 0xFF
  end opcode so the player reports MSTAT=2 and the 68K auto-stops),
- decodes SPEECH segments through tools/upd7759_decode.py's
  SlaveDecoder, hashes the PCM, matches against sndtest/speech's
  manifest (extending the bank with any sample the attract capture
  never played),
- emits sndtest/md/sndmap_data.h (track arrays + 256-entry dispatch
  table) and updates sndtest/speech/ + manifest for new samples.

Usage: soundmap_build.py SWEEP_LOG [--speech-dir sndtest/speech]
                         [--out sndtest/md/sndmap_data.h]
"""
import argparse
import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from opm2opn import Transcoder, Stream                     # noqa: E402
from upd7759_decode import SlaveDecoder, runs_to_pcm, write_wav  # noqa: E402

RATE = 16000


def parse_log(path):
    """-> list of segments: {cmd, t0, t1, ym:[(t,reg,val)], pc:[(t,v)],
    pd:[(t,v)]}, plus the running OPM shadow value at each segment start."""
    segs = []
    cur = None
    shadow = [0] * 256
    addr = 0
    for line in open(path):
        p = line.split()
        if len(p) != 3:
            continue
        t, kind, val = float(p[0]), p[1], int(p[2], 16)
        if kind == "IJ":
            if cur:
                segs.append(cur)
            cur = {"cmd": val, "t0": t, "t1": None, "ym": [], "pc": [],
                   "pd": [], "shadow": list(shadow)}
        elif kind == "IX":
            if cur and cur["t1"] is None:
                cur["t1"] = t
        elif kind == "Y0":
            addr = val
        elif kind == "Y1":
            if cur and (cur["t1"] is None):
                cur["ym"].append((t, addr, val))
            if addr == 0x19:
                shadow[0x19 + (val >> 7)] = val
            else:
                shadow[addr] = val
        elif kind == "PC":
            if cur and cur["t1"] is None:
                cur["pc"].append((t, val))
        elif kind == "PD":
            if cur and cur["t1"] is None:
                cur["pd"].append((t, val))
    if cur:
        segs.append(cur)
    return segs


def classify(seg):
    """Isolated (per-reset) segments classify sharply. Returns
    (ym_kind or None, has_pcm): ym_kind 'music' if key-ons persist to
    the window end (looping BGM), 'ymsfx' for a finite jingle; has_pcm
    marks a uPD7759 stream (speech or PCM sfx component)."""
    keyons = [t for t, r, v in seg["ym"] if r == 0x08 and (v >> 3) & 0xF]
    t_end = seg["t1"] or (seg["t0"] + 10.0)
    has_pcm = len(seg["pd"]) > 300
    if not keyons:
        return None, has_pcm
    if len(keyons) >= 8 and (t_end - keyons[-1]) < 2.0:
        return "music", has_pcm
    return "ymsfx", has_pcm


def transcode(seg, loop, secs=14.0):
    """Segment -> player stream bytes (opm2opn mapping, fixed FM order:
    per-command isolation means channel contention is the driver's own;
    map OPM ch0-5 -> FM, ch6-7 -> PSG for consistency across cuts)."""
    fm_map = {c: c for c in range(6)}
    psg_map = {6: 0, 7: 1}
    # size caps (the 68K image must stay inside the cart's first 512KB
    # window): loops keep 14s; one-shots end 2.5s after their last
    # key-on (kills 15s housekeeping tails the dedup can't fully eat)
    keyons = [t for t, r, v in seg["ym"] if r == 0x08 and (v >> 3) & 0xF]
    if loop:
        t_cut = seg["t0"] + secs
    else:
        t_cut = (keyons[-1] + 2.5) if keyons else seg["t0"] + 1.0
    tc = Transcoder(fm_map, psg_map)
    tc.s.t = seg["t0"]
    tc.snapshot(seg["shadow"])
    shadow = list(seg["shadow"])
    for t, reg, val in seg["ym"]:
        if t > t_cut:
            break
        tc.s.wait_to(t)
        tc.event(shadow, reg, val)
        if reg == 0x19:
            shadow[0x19 + (val >> 7)] = val
        else:
            shadow[reg] = val
    for opn in range(6):
        tc.s.ym(0, 0x28, tc.keych(opn))
    for v in (0x9F, 0xBF, 0xDF, 0xFF):
        tc.s.psg(v)
    if loop:
        tc.s.out.append(120)          # breath before the wrap
    else:
        tc.s.out.append(0xFF)         # end: player reports MSTAT=2
    return tc.s.out


def decode_speech(seg):
    """PD stream -> PCM hash via the slave state machine (same md/reset
    edge rules as upd7759_decode.main)."""
    md, reset = 1, 0
    dec = None
    pcms = []
    events = sorted([(t, "PC", v) for t, v in seg["pc"]] +
                    [(t, "PD", v) for t, v in seg["pd"]])
    for t, kind, val in events:
        if kind == "PC":
            new_md = 0 if (val & 0x80) else 1
            new_reset = 1 if (val & 0x40) else 0
            if reset and not new_reset and dec:
                pcms.append(dec)
                dec = None
            if md and not new_md and new_reset:
                if dec:
                    pcms.append(dec)
                dec = SlaveDecoder()
            md, reset = new_md, new_reset
        elif dec is not None:
            dec.feed(val)
            if dec.done:
                pcms.append(dec)
                dec = None
    if dec:
        pcms.append(dec)
    best = None
    for d in pcms:
        pcm = runs_to_pcm(d.runs, RATE)
        if len(pcm) < RATE // 100:
            continue
        if best is None or len(pcm) > len(best):
            best = pcm
    if best is None:
        return None, None
    sha = hashlib.sha1(
        b"".join(struct.pack("<i", v) for v in best)).hexdigest()[:12]
    return sha, best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--speech-dir", default="sndtest/speech")
    ap.add_argument("--out", default="sndtest/md/sndmap_data.h")
    args = ap.parse_args()

    manifest = json.load(open(os.path.join(args.speech_dir, "manifest.json")))
    # bank order = unique entries in manifest order (speech_bake.py rule)
    bank = []       # (sha, wav)
    for e in manifest:
        if "wav" in e:
            bank.append([e["sha"], e["wav"]])
    sha_to_idx = {s: i for i, (s, _) in enumerate(bank)}

    segs = parse_log(args.log)
    table = {}      # cmd -> {ym: kind or None, trk: idx, sp: idx or None}
    tracks = []     # (name, bytes, loop)
    trk_dedup = {}  # sha -> track idx (aliased commands share cuts)
    new_speech = 0

    for seg in segs:
        cmd = seg["cmd"]
        ym_kind, has_pcm = classify(seg)
        ent = {"ym": None, "trk": 0, "sp": None}
        if ym_kind == "music":
            # music PD streams are 7759 DRUM percussion, not utterances
            # — many short starts, not bank material. Dropped for now;
            # the future fix is a stream opcode scheduling PCM hits.
            has_pcm = False
        if has_pcm:
            sha, pcm = decode_speech(seg)
            if sha is not None:
                if sha not in sha_to_idx:
                    wav = "cmd_%02X.wav" % cmd
                    write_wav(os.path.join(args.speech_dir, wav), pcm, RATE)
                    manifest.append({"index": len(manifest), "cmds_before":
                                     ["0x%02X" % cmd], "ms":
                                     int(1000 * len(pcm) / RATE), "sha": sha,
                                     "dup_of": None, "wav": wav,
                                     "repeat_blocks": False, "t0": 0})
                    sha_to_idx[sha] = len(bank)
                    bank.append([sha, wav])
                    new_speech += 1
                ent["sp"] = sha_to_idx[sha]
        if ym_kind:
            loop = ym_kind == "music"
            data = transcode(seg, loop)
            dsha = hashlib.sha1(bytes(data)).hexdigest()[:12]
            if dsha in trk_dedup:
                ent["trk"] = trk_dedup[dsha]
            else:
                trk_dedup[dsha] = len(tracks)
                ent["trk"] = len(tracks)
                tracks.append(("trk_%02X" % cmd, data, loop))
            ent["ym"] = ym_kind
        if ent["ym"] or ent["sp"] is not None:
            table[cmd] = ent

    with open(os.path.join(args.speech_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)

    total = 0
    with open(args.out, "w") as f:
        f.write("/* AUTO-GENERATED by tools/soundmap_build.py — the arcade\n"
                " * command map + per-command sound data. Do not edit.\n"
                " * type: YM lane action. sp: PCM lane speech-bank index\n"
                " * (0xFF = none) — MIXED sfx fire both lanes. */\n"
                "#include <stdint.h>\n\n"
                "#define SND_NONE 0\n#define SND_MUSIC 1\n"
                "#define SND_YMSFX 2\n\n")
        for name, data, loop in tracks:
            total += len(data)
            f.write("static const uint8_t %s[%d] = {\n" % (name, len(data)))
            for off in range(0, len(data), 16):
                f.write("\t" + ",".join("0x%02X" % b
                                        for b in data[off:off + 16]) + ",\n")
            f.write("};\n")
        f.write("\nstruct snd_trk { const uint8_t *p; uint32_t len;"
                " uint8_t loop; };\n")
        f.write("static const struct snd_trk snd_trks[%d] = {\n"
                % max(1, len(tracks)))
        for name, data, loop in tracks:
            f.write("\t{ %s, %d, %d },\n" % (name, len(data), 1 if loop else 0))
        if not tracks:
            f.write("\t{ 0, 0, 0 },\n")
        f.write("};\n\nstruct snd_ent { uint8_t type; uint8_t idx;"
                " uint8_t sp; };\n")
        f.write("static const struct snd_ent sndmap[256] = {\n")
        for c in range(256):
            e = table.get(c)
            tv = {"music": 1, "ymsfx": 2}.get(e["ym"] if e else None, 0)
            idx = e["trk"] if e else 0
            sp = e["sp"] if e and e["sp"] is not None else 0xFF
            note = ""
            if e:
                note = "  /* 0x%02X %s%s */" % (
                    c, e["ym"] or "", " +speech" if e["sp"] is not None else "")
            f.write("\t{ %d, %d, 0x%02X },%s\n" % (tv, idx, sp, note))
        f.write("};\n")

    counts = {}
    for e in table.values():
        k = (e["ym"] or "") + ("+pcm" if e["sp"] is not None else "")
        counts[k] = counts.get(k, 0) + 1
    print("map:", counts)
    print("tracks: %d unique (%d bytes)  new speech: %d"
          % (len(tracks), total, new_speech))
    if total > 300 * 1024:
        print("WARNING: track data %dKB — watch the 68K 512KB window"
              % (total // 1024), file=sys.stderr)


if __name__ == "__main__":
    main()
