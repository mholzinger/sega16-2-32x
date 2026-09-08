# HANDOFF — sndtest input bug, 2026-09-01 ~4am

## THE VGZ RIPS ARE DUCKED — restore levels from the gag capture (2026-09-02)

The "uncanny valley / missing overarching theme" on the VGZ builds was
NOT the transcode: the rips carry the game's in-context BGM mix, with
harmony channels DUCKED vs the arcade driver's own levels. Rise From
Your Grave: ch4/ch5 carrier TL 72 in the VGZ vs 18 in the driver
(~40dB — inaudible), ch1 58 vs 30; and on ch4/ch7 the MODULATORS too
(+23..+57, which dulls the timbre — the "balance is off"). Same
instruments (all a2/fb7), same mapping; just buried. Every one of the 8
rips is ducked (20-58 TL). FIX: `build_vgm_music.py --gag-log
music3.log` measures per-(slot,ch) TL in the gag capture of the same
command and subtracts the delta over ALL 4 operators
(tl_delta_vs_gag) — the driver's full patch on the rip's complete
notes. Mike: "YES! 94 is the right sound" (gag+attenuation fix) ->
"I think I hear the notes!" (VGZ+carrier restore) -> "Better" (all 4
ops). Also fixed en route: PSG attenuation was TL>>3 (~3x too loud);
now (TL*3)>>3 to match 0.75dB vs 2dB steps (opm2opn.psg_key). Kept the
whole-stream loop (re-snapshot each loop) over the intro/loop split.

CHANNEL VU (visual aid for balancing): the 68K feeder parses the opcodes
it decompresses (key-on + carrier TL per FM ch, PSG volume latches),
snapshots 8 levels per 512B block, and publishes the snapshot of the
block the Z80 is CURRENTLY reading (COMM10 = ch0-3 nibbles, COMM2 =
ch4-7; pad-2 publish to COMM10 removed). SUB-BLOCK TIMING: 8 snapshots
per block (one per 64B), each tagged with the cumulative wait-ms at its
end; music_feed counts frames since rd changed (~16.7ms each) and picks
the window playing NOW -> note-level motion (~75ms windows; measured 4.3
level changes/sec, 40 distinct states over 24s of Rise vs 2-3 with
block granularity). SH-2: HORIZONTAL bars, one row per channel, rows
13-20, label col 24, 14-cell track cols 26-39 — the only region the HUD
leaves free (vertical bars at cols 25-39 collided with the row 10-11
text). The boot font has NO solid block (unmapped chars = tile-1 dot),
so lit = '|' on a '.' track. vu_draw: instant rise, decay 1 level per 4
frames, redraws only cells between old and new bar ends.

ENVELOPE MODEL (Mike: "drain when not actively emitting, not full while
merely keyed"): the tracker also parses the carrier's D1R (0x6C+ch),
D2R (0x7C+ch), D1L|RR (0x8C+ch) and runs a per-channel attenuation
vu_ea (0..127, TL units) over each window's elapsed ms: key-on -> 0
(phase decay1), decay1 at D1R to the D1L sustain (D1L*4, 15 = 127),
then decay2 at D2R, key-off -> release at RR*2+1. Rate -> att/ms via
eg_inc[32] (full-scale ~60000ms / 2^(r/2); att += dt*inc >> 8 — a
visual approximation, not the chip's EG). Level = 15 - ((TL+ea)>>3),
invisible past att 120; a strike shows the TL peak for its window.
Result on Rise: sustained pads (ch1/3/5, D1L=0) hold full while keyed —
correct, they ARE sounding — while ch0/ch4 show intermediate levels as
they drain. PSG channels latch until key-off (no envelope is emitted
for them). Not modelled: KS rate scaling, AR (attack treated instant),
the other carriers of multi-carrier algorithms (slot 3 only).

FLAT-WHEN-QUIET (Mike, 2026-09-02, screenshot after B with bars still
full): two fixes. (1) music_feed used to `return` when !mus_on, so
COMM10 froze at the last levels and the SH-2 held the bars up forever —
it now publishes SILENCE (alternating halves) while stopped; verified:
first all-zero word 10 frames after B, bars flat by +200. (2) The
level mapping had its floor at -90dB (att 120), a long lit-but-
inaudible tail. Now CEILING = the channel's own peak (15 - TL>>3,
absolute, so bar length still reads as balance between channels) and
FLOOR = 48 att units (-36dB) below that peak: lvl = peak -
peak*min(ea,48)/48 -> flat once audibly silent. SH-2 decay raised to
1 level/frame (was 1 per 4). Measured on Rise: intermediate levels
14-27% of samples on the melodic channels (was 0-8%).

## LIVE MIXER in the sound test (2026-09-03) — adjust/mute while playing

Mike: "any interaction to adjust the instrument levels while the songs
are playing... or at least enable/disable instruments." Built:
- CONTROLS (6-button pad): MODE toggles mixer mode (title becomes
  "MIX U-D C=MUTE"). In it: LEFT/RIGHT select a channel ('>' marker on
  its VU row), UP = louder / DOWN = quieter by 3 TL (~2.25dB, clamp
  +-60), C = mute/unmute. Each row shows its offset ("+03" louder,
  "-06" quieter, "MUT"). Offsets persist across songs. Outside mixer
  mode the pad is the command navigator as before.
- PATH: SH-2 HwMdMixCmd(ch, off, mute) -> COMM2 = {mute<<8 | int8 off},
  COMM0 = 0x2200|ch -> 68K case 34 -> mix_off[]/mix_mute[]. The feeder
  applies them in vu_byte to every CARRIER TL write as bytes stream
  into the ring (carriers per YM2612 algorithm: CARRIER_MASK over
  register slot 0=S1 1=S3 2=S2 3=S4; alg 0-3 S4, 4 S2+S4, 5/6 S2+S3+S4,
  7 all) and to PSG volume latches (TL*3/8 steps; mute = 15). No Z80
  change.
- RE-EMIT: the rips set most TLs once at song start, so a mid-song
  change is injected: vu_byte keeps each channel's raw pre-mixer TL per
  slot (vu_tlbase) and PSG att (vu_psgbase); case 34 marks the channel
  pending; fill_block queues fresh writes (mix_q) at the head of the
  next block, routed through vu_byte so the offset applies and the VU
  tracks it. The Z80 treats the ring as one opcode stream, so extra
  opcodes are legal.
- LATENCY: ring lead cut from 8 blocks (~5s) to prime 3 + top-up to 2
  ahead (~1.2s max); measured wr-rd = 2 steady, no stall (a block lasts
  ~37 frames, refill runs every frame). Verified: two DOWN taps on ch1
  -> the next block starts with F0 4D 14 = base 8 + 6.
- Use: play 0x94, MODE, pick a bar, tap UP/DOWN or C, hear it within a
  second, watch the bar. Report the offsets that sound right per
  channel; bake them with build_vgm_music (per-channel gain is a
  one-line addition if needed).

## LEVEL MATCHING vs THE ARCADE (2026-09-02/03) — the 32X level is SET

Mike: "validate and set the 32x level to match the overall output of
MAME." Method (all KIT, reusable per song):
- ARCADE: `tools/arcade_wav.lua` injects one command into altbeast and
  gags the latch; `mame altbeast ... -sound none -wavwrite arc.wav
  -seconds_to_run N`. (-sound none still feeds -wavwrite.)
- ROM: `tools/rom_wav.lua` presses A on the sound test; `mame 32x -cart
  rom/sndtest.32x ... -wavwrite rom.wav -seconds_to_run N`. MAME's 32x
  session never finalizes the RIFF header (a scripted mac:exit() made it
  worse); `tools/wav_ab.py` patches it in memory.
- COMPARE: `tools/wav_ab.py arc.wav rom.wav --skip 2.5 --secs 25`:
  DC removal (the 32X PWM idles at ~-9000 DC), onset alignment, per-
  second RMS, per-band RMS (bass/lowmid/mid/high) in dB, TL equivalents.
- SET: `build_vgm_music.py --gain N` adds N TL units to every CARRIER
  operator (volume only; carriers per algorithm in CARRIERS, register
  slot order M1,M2,C1,C2 -> alg4 = (2,3), alg5/6 = (1,2,3), alg7 = all).
  `--mod-gain` (modulator TL) and `--psg-att` exist but are NOT used —
  see below.

FINDINGS on Rise (0x94):
- Before: ROM +7..+9 dB hot, steady, across the whole song.
- MAME's 32X output has a SIGNAL-INDEPENDENT FLOOR ~ -36 dBFS overall
  (4-20kHz at 38/34/28 dB vs the arcade's 9/-1/7): at carrier +40
  (-30dB) the ROM still measures -36.0 vs the arcade's music at -35.7.
  Muting the PSG changed nothing (so it's not the squares). Most likely
  ymfm's YM2612 LADDER (crossover) distortion model, which is a real
  model-1 Mega Drive trait, plus PWM noise. Consequence: lowmid/mid/high
  and the overall figure stop responding to TL; ONLY THE BASS BAND is a
  clean level metric in MAME's 32X. (ares has no headless audio.)
- Levers that do NOT work: --mod-gain (lower modulation index pushes
  energy INTO the fundamentals: lowmid +5 -> +7, overall up); --psg-att
  (no effect on the bands; the PSG carries ch6's low rhythm only).
- FINAL: --gain 13, psg-att 0, mod-gain 0 -> bass +0.4 dB vs arcade
  (the arcade bass band also carries the uPD7759 kick we don't play, so
  our FM bass is if anything a hair hot); overall reads +3.6 but that is
  the floor. Shipped in rom/sndtest.32x and render_music.h. A/B clips
  sent to Mike (AB_arcade_94 / AB_32x_94, 25s, unnormalised) for the ear
  test; his ear is the final gate.
- To redo for another song: capture both with AW_CMD/RW_UP+RW_RT, run
  wav_ab, adjust --gain by the BASS-band TL figure.
- CORRECTION 2026-09-03: --gain 13 (matching MAME altbeast's ABSOLUTE
  level) made the intro "super silent" for Mike on his playback. MAME's
  altbeast output is simply a low line level (~-35 dBFS RMS); matching
  it absolutely is not the goal — the CHANNEL BALANCE is (done via the
  gag TL restore), and the master level should be comfortable. From the
  first note MAME renders our gain-13 intro at -29..-34 dBFS vs the
  arcade's -34..-36, i.e. the notes are there; the absolute level is
  just quiet. SHIPPED: --gain 6 (bass +4.3 dB over MAME-arcade
  absolute, ~-4.5 dB from the pre-measurement level Mike liked). The
  master gain is Mike's ear call from here; the tools give the number.
- REAL-BOARD REFERENCE (2026-09-04): Mike supplied the 16-Bit Audiophile
  Project FLACs (srcref/audio/Altered Beast (The 16-Bit Audiophile
  Project)/) — actual PCB recordings, the proper reference for balance
  AND spectral shape. Decode: `ffmpeg -i X.flac -ac 1 -ar 48000 -sample_fmt
  s16 out.wav`; compare with wav_ab --skip 0 (no boot blip in the FLAC;
  trim ours with ffmpeg -ss 2.5 first). Rise: relative to bass the real
  board is lowmid -0.6 / mid -5.9 dB; our ROM -0.3 / -4.4 -> BALANCE
  MATCHES within ~1.5 dB (the high band is unmeasurable in MAME's 32X,
  floor). MAME altbeast is 16 dB QUIETER than the real-board master
  (tonally close: all bands within ~5 dB), so "match MAME absolute" was
  the wrong target; our gain-0 level sits ~7 dB under the mastered FLAC
  = comfortable and closer. SHIPPED: --gain 0 (the level Mike liked).
  Absolute level from here is his ear / the live mixer.

COMM2 IS THE COMMAND PARAMETER REGISTER — NEVER PUBLISH FROM THE 68K.
The first VU build put ch4-7 in COMM2. The SH-2 writes COMM2 (offset /
char) then COMM0 (cmd 6/7); the 68K's music_feed overwrote COMM2 in
between, so do_commands read VU nibbles as the VRAM offset: stray chars
in the corners ("94"/"4"), and when bit 13 was set, case 7's
`0x6000+ofs` crossed 0x8000 = a VDP REGISTER write — plane size
(every-other-row display, screenshots/53.png) and auto-increment (a
column of '4's, 57.png). FIX: the VU lives ONLY in COMM10 — 4 channels
per frame, halves alternate, bit 15 = half, 3-bit levels (l4to3 LUT on
the 68K, l3to4 on the SH-2); each half refreshes at 30Hz, the other
keeps decaying. HARDENED: case 6 masks the offset to 0x1FFF so a bad
parameter can never become a register write. Verified: 70s soak, 0
68K COMM2 writes, halves 1655/1655, HUD intact at frame 4100.
Free COMMs for future 68K->SH-2 data: none — COMM0 cmd, 2 param, 4
speech bell, 6 status, 8 pad1, 10 VU, 12 tick, 14 feed blocks. Time-
multiplex (as the VU does) or fold into COMM14's spare bits.

## NEXT: software YM2151 on the SH-2 (the 8-channel fix, 2026-09-02)

Decided with Mike: the VGM rips play COMPLETE, but our YM2612 has only 6
FM channels and the songs use all 8, so opm2opn squashes 2 to PSG
squares — a real, audible tonal loss (Mike: "sounds wrong... missing an
entire set of channels and instruments"). Least-used-to-PSG mapping
(build_vgm_music.best_maps) helped but is not enough. THE FIX: a software
YM2151 FM synthesizer on the 32X SH-2, output via PWM — all 8 channels
as true FM, exact fidelity, fed the RAW VGZ register stream (no opm2opn,
no squash, no DT2 drops).

Foundation in hand: jt51's `doc/opm.c` (Nuked-OPM, cycle-accurate — too
heavy to run as-is, but the AUTHORITATIVE tables + algorithm) extracted
to scratch. Plan:
1. Per-SAMPLE YM2151 synth (ymfm-style, NOT cycle-accurate) using the
   Nuked tables (logsinrom, exprom, eg rates, pg_detune, pg_freqtable,
   fm_algorithm). Build+validate OFFLINE first: render a VGZ -> WAV, all
   8 channels, confirm it's the target sound.
2. Port to SH-2 C; output PWM at ~22kHz (the speech lane already drives
   PWM). Budget: ~8 voices at 22kHz ~ half of one SH-2 (idle in sndtest;
   tighter in the shipping game where the SH-2s render video).
3. Feed the raw VGZ register stream (compressed) with sample-accurate
   timing; retire the opm2opn/YM2612 path for music.
This replaces render_music.h's transcoded YM2612 opcodes with raw VGZ
register data + the SH-2 synth. jt51 verilog + opm.c live only in git
objects (submodule not checked out); extract via `git --git-dir` cat-file.

PROGRESS 2026-09-02:
- REFERENCE built: compiled jt51 `opm.c` (Nuked-OPM) + a VGM driver in
  scratch/jt51/ (`cc -O2 drive.c opm.c`); rendered "Rise From Your
  Grave" from its VGZ -> the TARGET WAV (all 8 channels, exact). Sent to
  Mike. Use this as the oracle to validate the synth against.
- Tables extracted to `tools/ymtables.json` (logsin, exp, freqtable,
  detune) from opm.c.
- `tools/ym_synth.py` = first-draft per-sample OPM synth (integer,
  SH-2-portable shape). It PLAYS the song at the right level (RMS 1090
  vs ref 1331) but is TOO DARK: spectral centroid 1194Hz vs ref 2666Hz.
  Diagnosed NOT the envelope (dark even with env forced full, 1443Hz) —
  it's the FM MODULATION DEPTH and the first-draft PHASE-INCREMENT
  scaling (`_recalc`: `fnum*2^oct*mul>>4` is a guess; Nuked's
  OPM_PhaseCalcIncrement is the real formula) plus the modulation index
  in `_connect`. NEXT: fix phase increment + modulation scaling
  operator-by-operator against the Nuked reference until the timbre
  matches, verify all 8 channels + envelope shapes, THEN port ym_synth
  to SH-2 C and drive PWM at ~22kHz, fed the raw VGZ stream. The synth
  math (logsin/exp operator, 8 algorithms, feedback) is in place; the
  tuning (pitch table + mod depth + EG rates) is the remaining work.
- V2 FIX (2026-09-02): two bugs found + fixed in ym_synth.py against the
  Nuked reference: (1) PHASE INCREMENT was ~5x too low (`_recalc` now
  uses the exact OPM_KCToFNum + `(fnum<<block)>>2` formula from opm.c,
  incl. dt1 detune) — notes were an octave+ down = dark; (2) MODULATION
  was halved (`>>1`) — Nuked adds the operator output to the 10-bit
  phase DIRECTLY (& 1023). Result: spectral centroid 1194Hz -> 2996Hz
  (ref 2666Hz), RMS matched. Now in the right range, maybe a touch
  bright. REMAINING TUNING: LFO (AM/PM, reg 0x18/0x19/0x1B — not yet
  implemented), EG rate table exactness, feedback averaging. Then SH-2
  port + PWM. Operator/phase/atten now match Nuked's structure; validate
  a sustained single note sample-close before the port.

---

## RESOLVED 2026-09-01 (next session): `Defocus: Pause`

Root cause: **ares was pausing emulation every time its window lost
focus.** Mike's live `settings.bml` is a regenerated default profile
(uniform default keymaps across all systems, his gamepad GUID
`03008fe54c05…` bound 18x in the Aug 13 backup and 0x live) whose
`Input → Defocus` is the ares default **Pause**. His Aug 13 original
(`settings.bml.bak-tick-hunt`) had **Allow** — run and accept input in
the background. His test workflow alternates ares and the terminal
constantly, so on the final soundboard night:

- Press play focused, switch to the terminal to report → emulation
  pauses → **audio cut within ~100ms**. That is the play-then-cut, for
  every button, and it is why MUS FEED froze at 0800/0901 — the
  headless trace shows those are the first 2-3 frames after
  music_start.
- Presses made while ares was unfocused were ignored outright — the
  3:34am screenshot (COUNT 00, **PAD 1000** = six-button detected,
  no buttons down) is a frozen, unfocused emulator, not dead input.
- Earlier builds "worked" because short speech finished before any
  window switch and Mike sat still through the BGM listen.

A first analysis this session wrongly blamed the profile's keyboard
layout shift; Mike corrected it — the defaults have been his working
layout since Aug 13 and his ear passes that night used them.

The ROM is fully cleared by rig evidence, not assertion: ares-headless
`--trace-comm` on (a) a human-shaped 30-frame-hold A press and (b) the
full interactive choreography (jingle auto-stop → 0x94 music → browse
while playing → restart while playing) shows exactly one router post
per press, monotonically counting acks, and a healthy stream to end of
run. v148 (Mike's GUI) and the fork differ by zero MD-core commits, so
no emulator divergence either. All three ranked hypotheses are dead;
the A+B same-frame guard defends against a condition that does not
exist (revert at leisure; BTN HIST is worth keeping as a lab fixture).

Fix, one line (with ares closed), or Settings → Input → When focus is
lost → Allow in the GUI:

    sed -i '' 's/Defocus: Pause/Defocus: Allow/' \
        ~/Library/Application\ Support/ares/settings.bml

Note for the books: the regenerated profile dates to Aug 13 (a
tick-hunt backup that was never restored). If Mike ever wonders where
his gamepad bindings went, they are in the backup file. The profile
also moved audio SDL→OpenAL that day; if the P3 boot-tick is ever
hunted again, that driver change is a suspect with a matching date.

**Addendum, same day (after Mike reported sputter-then-silence on both
ares GUI and OpenEmu):** the ROM was re-proven end to end, deeper:

- The regenerated `sndmap_data.h` (780a65b deleted the ear-validated
  `music_track.h` and rebuilt everything — nobody's ears had heard the
  new bytes) was simulated offline: trk_94 = 14.1s, keyons throughout,
  carrier TLs loud (0x12-0x30), zero misaligned opcodes, and its
  1.18KB/s rate matches the fork's measured consumption. Sonically and
  temporally valid by construction.
- `tools/snd_autopsy.lua` in mame 32x: A press → COMM6 ack 0194,
  MSTAT=01, ring 8 blocks in flight, Z80 looping in the player at the
  right tempo for 20s+. `-wavwrite` captured sustained moving music to
  end of run. **Two independent cores (ares v148-core fork + MAME) play
  this build's 0x94 uncut, machine-level and audio-level.**
- TOOL LESSON (cost two false results): MAME lua notifier/tap handles
  held in autoboot-chunk locals are GC'd after the chunk returns — the
  callback dies silently mid-run. An earlier "zero YM writes" tap and a
  silent wav were THIS bug, not the machine. Anchor handles in _G
  (see snd_autopsy.lua header).
- Menu fix in the same build: boots at CMD 94 now — it used to boot at
  CMD 00, which is the arcade stop-all byte, so a first A press was
  silence by design (Mike hit exactly this; BTN HIST 0001 / COUNT 01 /
  LAST 00 in his capture was a working input chain playing a silent
  command).

What is NOT yet explained by anything in the ROM: Mike hears the start
sputter and die on his desktop apps. The rigs cannot reproduce it at
any layer. The one discriminating experiment left: press A and keep
the emulator window focused, hands off, for 30s. Facts on record, no
interpretation: his ares profile still carries `Defocus: Pause`
(emulation halts when the window loses focus), and OpenEmu ships with
pause-in-background on by default.

**Closed 2026-09-01 pm: "a works now" (Mike).** The boot-at-94 build
plays on his setup.

**DIAGNOSED 2026-09-01 pm — regenerated music tracks fade after a few
bars (Mike's ear catch).** Cause: the per-note VOLUME/ENVELOPE refresh
writes are missing, so each voice plays its attack and decays to
near-silence with nothing re-arming it. Measured on trk_94, whole
14s track, per part-0 register:
  - TL (0x40-0x4E): ear-validated 43-158 writes each -> regen 1-2 each
  - SL/RR (0x80-0x8E): 56-158 each -> regen 1-2 each
  - key-ons (0x28) and pitch (0xA0) are still dense in the regen
    (notes fire; only the volume/envelope refresh is gone).
ROOT CAUSE IS CAPTURE METHOD, not the Z80 player and not the ym()
value-dedup (opm2opn.py:64 — that only drops immediate repeats; the
arcade's refreshes are value-VARYING and survive dedup in a continuous
tap). `cmd_sweep.lua` captures each command in ISOLATION (soft-reset +
read-gag), which severs the arcade driver's continuously-running
mix/envelope engine, so the per-note TL refreshes never get emitted.
The ear-validated `music_track.h` came from a CONTINUOUS gameplay tap
(driver running normally) — hence its 100+ refreshes/register and its
"sounds incredible" verdict.

FIX PATH — DECIDED 2026-09-01 (Mike): build a REAL sequence-format
decoder, not a re-capture. The music is sequence data in the Z80 sound
ROM (`epr-11671`); decode it straight from the bytes so it is faithful
by construction and reusable across the whole S16B library (the exact
same ROM ships in Japanese Altered Beast — MAME segas16b.cpp:39-45 —
so this pays off immediately on the next title). PCM/speech decode
direct from the sample ROM (`opr-11672/73`) via the uPD7759 algorithm,
deriving from jtcores `jt7759` (Mike: "we have a fully working sound
decoder in the FPGA source").

PROGRESS 2026-09-01 (this session), all in `SOUND_DRIVER.md`:
- `tools/z80dis.py` — full Z80 disassembler, no deps (KIT). Built to
  reverse the driver; reusable.
- Driver mapped from the ROM: I/O ports, IRQ tick ($0089), command
  intake, YM write primitive ($0CF4), and the MASTER SONG TABLE at
  $03B4 (`word[$03B4 + 2*(cmd&$7F)]` = song ptr). All 8 music tracks
  resolved (0x90-0x97 → $2E9A..$5DC1; 0x94 round-1 BGM = $4721).
- Song-header format decoded (N channels × 9-byte records with stream
  pointers). `tools/snd_seq_decode.py` STAGE 1 runs: reads the ROM and
  lists every song's channels + sequence streams.
- Confirmed the fade mechanism at the source: interpreter $0D03/$0D31
  CACHES operator-envelope regs per channel and re-applies them per
  note (ix+27..30) — exactly what the isolation tap dropped.
- REMAINING: the per-channel note/opcode encoding, the instrument
  patch table address, the tempo model — then reimplement and validate
  0x94 against the ear-validated music_track.h. See SOUND_DRIVER.md
  "remaining work".
- MUSIC lane: the decode-by-execution renderer WORKS (SOUND_DRIVER.md
  "Validation"). `tools/z80cpu.py` (Z80 core, 10/10 tests) +
  `tools/snd_render.py` boot the real driver and render 0x94: rich note
  content AND the per-tick TL envelope refresh (reg $78 at 717/s) that
  the isolation tap dropped — the fade fixed at the source. Piped
  through opm2opn: 17700 bytes of YM2612 player opcodes, 152 keyons, 3
  DT2 drops. IN THE ROM NOW: `tools/render_track.py` bakes 0x94 to
  `sndtest/md/render_track.h` (15008B loop); md_main.c case 32 plays it
  for cmd 0x94 (boot default). rom/sndtest.32x built + verified
  streaming headless. Press A to hear it — awaiting Mike's ear pass.
  SUPERSEDED by the FULL BUILD below (oracle-sourced, all tracks).
- **FULL BUILD DONE (2026-09-01): every music command plays the COMPLETE
  song, full volume, compressed.** Root cause of the fade was THE FADE
  LAW (idle sound-latch 0x01 = master-volume-1 command); the oracle
  capture is full-volume throughout. Pipeline: `music_sweep.lua` (arcade
  oracle, all 11 cmds x 60s, one MAME run) -> `build_music.py`
  (transcode + LZSS ~4x) -> `render_music.h` (620KB raw -> 153KB, fits
  the 512KB 68K window) -> router stream-decompresses into the Z80 ring
  (md_main.c `lz_next`, 4KB window). VERIFIED byte-exact: the Z80 ring
  blocks are exact contiguous chunks of the expected decompressed
  stream. Compression, not banking, beat the 512KB limit (Mike's call).
  REFINEMENT LEFT: tracks are 60s captures (~1.5-2 loops) looped whole,
  so the 60s wrap replays the intro (minor seam) — trim each to one
  natural loop period for a seamless loop. Music is COMPLETE now; this
  is polish. Also the offline EMULATOR (snd_render) still under-runs
  pitch-LFO/portamento ($063C/$06F5 never fire — bit-rotation flag
  divergence); ORACLE is the source of truth until that's fixed for the
  Japanese-AB/offline path.
  CLEAN PCM is decoded but NOT yet baked (ROM still plays tap-decoded
  speech); "Rise from your grave" = sample_00 (first in opr-11672,
  2.24s). Exact cmd->sample map needs the uPD7759 slave NMI protocol
  modeled in snd_render (feeds only 1 byte now).
- PCM lane: `tools/pcm_from_rom.py` DONE — direct uPD7759 decode of
  opr-11672/73, derived from jt7759 (cited) + cross-checked vs MAME.
  Validated: all 17 real utterances match sndtest/speech/*.wav at
  ncc >= 0.90 (13 at >= 0.99), one-to-one, no collisions. KEY FACT:
  these boards run the uPD7759 in SLAVE mode (Z80 hand-feeds bytes),
  so the sample ROM has NO chip-readable index — samples are found by
  their 5-byte slave prelude `ff 00 00 00 00`; the authoritative
  sample-boundary table lives in the Z80 ROM (epr-11671), not needed
  to hit 17/17 but it's where exact boundaries would come from. 6
  spurious partial detections are flagged by low match quality.

Original stuck-state analysis below, kept for the record.

---

For the next session. Written stuck. Read SOUND.md first for the
architecture and phase log; this file is only about where we are
jammed and what is proven vs. assumed.

## Where things stand

P0-P4 of the sound engine are BUILT and COMMITTED (`SOUND.md` has the
full log). `make sndtest` -> `rom/sndtest.32x`: the complete Altered
Beast soundboard behind a menu — 10 music tracks (cmds 0x94-0x9D), 63
YM jingles/sfx, 22 speech commands, real router dispatch on both lanes
(Z80/YM2612+PSG music, SH-2 PWM speech). Every piece of it was
ear-validated by Mike DURING the session as it was built: speech bank
("all pcm sounds up to 0B play!!"), streaming music at arcade tempo
("This sounds incredible").

## The jam

On Mike's interactive ares GUI, the FINAL soundboard build does not
play usably: **sounds start and are cut off immediately** (his words),
across A, B, C. The same rom file, driven on ares-headless
(`~/src/ares-debug`, the accuracy fork) with scripted input, works
completely: navigate to 0x94, press A -> LAST POST 94, music streams
indefinitely; cmd 0x41 -> speech plays to completion.

Evidence chain from Mike's screenshots (both in the session log):

- COUNT climbed ~2 per physical press while LAST POST pinned at 00 —
  i.e. every press delivered a play AND a stop (0x00) to the router.
- MUS FEED froze at 0800/0901 — music_start ran, then was stopped
  before/just after the Z80 consumed anything.
- "PCM starting and being immediately cut off" — the play half DOES
  fire; the stop half kills it within the frame.

My working theory: **the input layer delivers one physical press as A
and B edging in the same frame** (B = post 0x00 = stop-all in the
menu). I shipped two rom-side countermeasures, both in the current
build and commit history:

1. **BTN HIST row** — a flight recorder: last 4 button edges the ROM
   actually received (nibbles, newest right, 1=A 2=B 3=C 4=START).
2. **A+B same-frame guard** — when both edge together, B is dropped
   (play wins; B alone still stops). Verified headless with a
   deliberately simultaneous A+B: one post, speech survives.

Mike called stuck before confirming whether the guard build fixed his
playback. **Nobody has yet read BTN HIST off his screen — that single
hex value is the ground truth this whole argument needs.**

## Ranked hypotheses for the next session

1. **Input double-fire (theory above).** Next datum: BTN HIST after
   ONE press on Mike's machine. `0001` = clean A (theory dead, go to
   #2). `0012`/`0021` = confirmed double-fire (guard build should
   already fix playback; if not, extend the guard — maybe C co-fires
   too, or the pair spans two frames).
2. **GUI-ares vs headless-fork divergence.** His desktop ares is not
   the same binary as the headless fork (nightly f23eb39ad+). Two
   independent oddities tonight point at his GUI build behaving
   differently: the from-boot ticking heard on GUI ares but NOT in
   OpenEmu (SOUND.md P3 open observation), and now this. Cheap test:
   load the same rom in **OpenEmu** (which played earlier builds
   perfectly tonight) and browse the map there. If OpenEmu is clean
   and GUI-ares is broken, the lab's interactive rig should become
   OpenEmu until real hardware arbitrates.
3. **ares input-profile corruption across relaunch.** His first
   screenshot (pre-relaunch) showed input fully dead (COUNT 00);
   post-relaunch it showed the double-fire pattern. Two different
   broken states across one relaunch with zero rom changes smells
   like the emulator's controller-profile state, not the cart.

## What is PROVEN (do not re-litigate)

- The shipped rom's full dispatch chain works under clean input:
  headless run navigated the menu, posted 0x94, streamed music,
  played speech. Screenshots + Z80-RAM dumps in the session log.
- The engine itself is sound on Mike's own setup: he heard speech,
  the YM drone, the test arpeggio, and the transcoded round-1 BGM on
  ares GUI earlier THE SAME NIGHT on earlier builds.
- The earlier builds' A-press did LOCAL sfx_play on the SH-2;
  the final build routes A through the 68K router. Both paths work
  headless. The difference under his input stack is the open question.

## Menu reference (current build)

- UP/DOWN ±1, LEFT/RIGHT ±0x10 on the command byte; A = play via the
  arcade command map; B = stop-all; C = YM smoke test; START = round-1
  BGM shortcut.
- Rows: LAST POST/COUNT = router ack {last byte, posts}; PCM ACT =
  PWM voice mask; MUS FEED = {produced, consumed} ring blocks (moving
  = Z80 eating the stream); BTN HIST = input flight recorder.
- Landmarks: 0x41 speech, 0x94 stage music, 0x60-0x7F dense jingles,
  0x01-0x0C genuinely silent no-ops, 0x8C would halt the arcade's
  sound CPU (our router just plays what the map recorded for it).

## Deferred engineering (SOUND.md carries these too)

- Music loop cuts are 14s with unpolished loop points.
- DT2 dropped in transcode; no music-over-sfx layering (player is
  exclusive per track; arcade driver mixes).
- P5 (game integration): link the proven engine sources into the
  shipping rom; router consumes the live MCU mailbox (0xFFF0C4
  capture already works there); provision the 68K vint budget for the
  feeder; parity statics must not move.
- Boot-tick open observation (P3): ares GUI only; NOPWM isolation
  probe documented in SOUND.md.

## Key artifacts

- `rom/sndtest.32x` — current diagnostic+guard build (868KB).
- `sndtest/` — the lab (68K router/feeder, SH-2 menu+PWM pool, Z80
  player asm). `tools/`: snd_tap.lua, cmd_sweep.lua (5 MAME laws in
  comments), upd7759_decode.py, speech_bake.py, opm2opn.py,
  soundmap_build.py, z80_pack.py, mus_testgen.py.
- Sweep/tap logs were scratchpad-transient; regenerate via the tools
  (~40 min for a full command sweep).
