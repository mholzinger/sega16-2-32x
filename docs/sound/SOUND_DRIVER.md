# S16B Z80 sound driver — reverse-engineering map (2026-09-01)

The Sega System 16B sound board runs a Z80 driver in `epr-11671`
(32 KB). **The same ROM ships in English AND Japanese Altered Beast and
across much of the S16B library** (MAME `segas16b.cpp:39-45`), so this
map is the reusable spec for decoding any S16B title's music straight
from the bytes — no playback tap.

Everything here is DERIVED from the ROM by disassembly
(`tools/z80dis.py`); nothing is copied. Addresses are Z80 space.
Consumed by `tools/snd_seq_decode.py`.

## Why this exists

The playback-tap pipeline (`cmd_sweep.lua` → `soundmap_build.py`)
captured each command in ISOLATION (soft-reset + read-gag). That severs
the driver's per-note envelope/TL re-application, so regenerated music
fades after a few bars (measured: TL/SL-RR registers written 1-2× over a
14 s track vs 43-158× in a continuous capture). Decoding the sequence
data statically sidesteps the capture entirely. See `docs/sound/HANDOFF-SOUND.md`.

## Hardware I/O (Z80 io space)

| Port | Device | Access |
|---|---|---|
| $00 | YM2151 address | `out ($00),a` |
| $01 | YM2151 data / status | `out ($01),a` / `in a,($01)` bit7=busy |
| $40 | uPD7759 control (bank/reset/start) | `out ($40),a` |
| $80 | uPD7759 data | `out ($80),a` |
| $C0 | sound-command latch from 68K | `in a,($C0)` |

Ports mirror across their 0x40 blocks (partial decode). uPD7759 sample
ROMs are `opr-11672`/`opr-11673` — decode direct via `tools/pcm_from_rom.py`.

## Boot / IRQ

- Reset `$0000`: DI, IM1, `LD SP,0000`, clear RAM `$F800-$FFFF`, set
  `($F814)=$40`, `JP $00C2` (init).
- IRQ `$0038` → `CALL $0089` (the per-tick engine), EI, RET.

## Command intake — `$0089`

```
in a,($C0)          ; read latch
bit7 set  -> $0094  ; CALL $0212 (dispatch now)
a >= 0x41 -> $0099  ; enqueue in the 8-slot queue at $F808
else      -> $0094  ; CALL $0212
```
So $41-$7F queue (sfx/speech/jingles), ≥$80 and <$41 dispatch immediately.

## Master song/handler table — `$03B4`  (THE decoder entry point)

Command handler `$02D6`: for a bit7 command, `idx = cmd & $7F`
(`$02E3: and $7F`), then

```
$02EE: ld hl,$03B4 ; add hl,bc ; add hl,bc   ; hl = $03B4 + 2*idx
       ld c,(hl); inc hl; ld b,(hl)          ; BC = SONG-DATA pointer
       ld de,$00AD; add hl,de                ; parallel HANDLER table +$AD
       ld a,(hl); inc hl; ld h,(hl); ld l,a; jp hl   ; run start-handler
```

`word[$03B4 + 2*(cmd & $7F)]` = song-data pointer. Resolved:

| cmd | idx | song@ | note |
|---|---|---|---|
| 0x90-0x97 | 0x10-0x17 | 2E9A,35C6,3F6C,44C8,4721,5099,58D3,5DC1 | the 8 music tracks |
| 0x80-0x8F | 0x00-0x0F | 0F6C-103F region | control handlers (fade/stop/etc.) |
| 0xB1,0xB2,0xD3 | | 18C3,18D9,1DF4 | jingle/sfx songs |

0x94 (round-1 BGM) = song data at `$4721`.

## Song-data header

Byte 0 = channel count N. Then N × 9-byte records:

```
[flags, chan_id, b2, seq_lo, seq_hi, patch, 00, 00, 00]
```

- `flags` bit7 = active; `$84` seen on the last 1-2 channels (PCM/PSG).
- `seq_lo/hi` = pointer to this channel's sequence stream (sits right
  after the header).
- `patch` = initial instrument ($F1/$FD common; $00 on PCM channels).
- trailing 3 bytes = runtime state, zero in ROM.

0x94 → 8 channels, streams at 476A/48E1/4A2F/4776/4B9F/4D11/4EA7/4F27
(6 FM ids 81-86, then 87/80 flagged $84). `snd_seq_decode.py --song 0x94`
prints this from the ROM.

## Sequence interpreter — `$0D03` (STAGE 2, partly mapped)

The interpreter walks a stream via HL. Confirmed:

- YM write primitive `$0CF4`: wait `in a,($01)` bit7, `out ($00),c`
  (reg), `out ($01),a` (val). Callers pass reg in C, val in A.
- Patch loader `$0D03`: reads (reg, val) pairs, writes each via
  `$0CEF`→`$0CF4`, until byte `0x02` (end). `0x03` = jump/loop (`$0D84`).
- `$0D31`: operator-envelope regs `$60/$68/$70/$78` are CACHED into the
  channel struct (ix+28/30/29/..) so the driver RE-APPLIES them per
  note — this is the mechanism the isolation tap lost.
- Channel structs: 20 voice slots at `$F818`, stride `$28` (40 B);
  `ix+32` = active-command byte, `ix+27..30` = cached envelope state.

### Sequence format — FULLY MAPPED (2026-09-01)

- Tick = YM2151 Timer A overflow (`$0CE1` polls status bit0; period set
  by regs $10/$11, control $14). Main loop `$0200` runs once per tick.
- Per tick, `$0582` walks 20 channel slots (`$F840`, stride $28); active
  ones (`ix+0` bit7) advance in `$0599`: bump tick counter (ix+12/13),
  and on reaching the note duration (ix+10/11) call `$07D3` for the next
  event, then apply pitch (KC=$28+ch, KF=$30+ch), key on/off ($08), and
  run the per-tick effects.
- Event fetch `$07D3` reads the stream via DE=(ix+3,4):
  - byte **<$E0 = NOTE**: 0 → rest (ix+18=$FF); else KC = `kc_table[$0D8C
    + (note-1) + transpose(ix+5)]`. NEXT byte = duration, ticks =
    `dur * (ix+2)` (tempo base) via the shift-add multiply `$0CA9`.
  - byte **>=$E0 = OPCODE**: index `op & $1F` into the 32-entry jump
    table at `$08AB`. Confirmed opcodes:
    | op | operands | effect |
    |---|---|---|
    | E0/E3 | 0 | nop / marker |
    | E1 | 1 | set tempo base ix+2 |
    | E6 | 2 | set effect-enable ix+22, ix+8 |
    | E7 | 1 | set gate/effect ix+6 |
    | E8 | 2 (addr) | call (push return on iy+9 stack) |
    | E9 | 0 | return from call |
    | EA | 2 (addr) | **jump — song loop** |
    | EB | 1 | transpose += (ix+5) |
    | EC | slot,count,addr | **repeat loop N times** |
    | ED-F0,F4-F6,FC | 0 | set channel MODE flag bits (ix+0) — select FM/PCM/PSG note handling |
    | F1 | 1 | **patch/instrument** → table `$109F`, load via `$0D03` |
    | F2 | 0 | key-off / noise ctrl |
    | F3 | 2 | song sync/end ($F808/$F804) |
    | F7 | 2 (op,val) | **operator TL/volume** → YM $60+ch, cached ix+28-31 |
    | F8/F9 | 0 | modify $F812 (LFO/pan global) |
    | FA | 0 | all-notes-off |
    | FD | 1 | tempo base += |
    | FE | 1 | set global $F818 |
- Instrument table `$109F`: patch# (1-based) → `word` ptr (`$07C8`
  indexes) → reg/val pairs streamed by `$0D03` until byte $02.
- Per-tick effects: pitch-LFO `$063C` (table `$1045`), amp-LFO `$06AE`
  (table `$104D`), portamento `$06F5`. Control bytes FC/FD/FE inside the
  effect tables. These modulate every tick — the reason a static
  reimplementation is fidelity-risky.

### Decode strategy — EXECUTE the driver (decided 2026-09-01)

Rather than reimplement the interpreter + LFO/portamento in Python (the
approximation that causes fidelity drift), RUN the real driver ROM in a
small Z80 core with the S16B sound I/O harness and capture its YM2151 /
uPD7759 writes. Faithful by construction; works on ANY S16B sound ROM
(Japanese AB uses this exact ROM). Tools:
- `tools/z80cpu.py` — Z80 CPU interpreter (validated standalone).
- `tools/snd_render.py` — S16B harness: ROM + RAM + banking, YM2151
  port capture, Timer A, latch injection; boots, posts a music command,
  runs, emits the tap-format YM2151 log that `opm2opn.py` consumes.
The disassembly map above drives it precisely (where to inject the
command, Timer A period, song-end detection) and validates the output.

### Validation — RENDERER WORKING (2026-09-01)

`tools/z80cpu.py` (10/10 test cases) + `tools/snd_render.py` boot the
real driver, dispatch cmd 0x94, and render the song. Census of the
rendered YM2151 stream (1800 ticks ~ 15s, the song's ~14s loop — tempo
lands right at TA=500 / 8.38 ms tick):
- KC note-pitch 520/s, KF 520/s, key-ons 10/s — rich, musical.
- **TL carrier (reg $78) 717/s** — the per-tick envelope re-application
  that the isolation tap DROPPED (it had TL at ~2/s). The fade is fixed
  at the source, by construction.
- patch/DT/AR/etc registers ~0.5/s — one patch load per channel plus
  occasional changes. Correct.
Piped through `opm2opn.py`: 31953 YM2151 events → 17700 bytes of YM2612
Z80-player opcodes, 152 key-ons preserved, 3 DT2 drops. The full
faithful chain works: ROM → z80cpu (run driver) → snd_render (capture
YM2151) → opm2opn (transcode) → Z80 player stream.

**THE REAL SOURCE = VGM RIPS (2026-09-01, Mike found them).** Capturing
the driver (even gagged) fights attract contamination AND can't pin the
loop boundary — the songs are LONG (Rise From Your Grave = 127s with a
loop at 22.6s). The authoritative answer is the VGM/VGZ soundtrack rips
(`srcref/audio/Altered_Beast_(Sega_System_16B)/*.vgz`): a hardware log
of the exact YM2151 register writes WITH an explicit loop point. Pipeline
`vgm2ym.py` (parse) -> split at the loop point -> `build_vgm_music.py`
(transcode INTRO + LOOP separately via opm2opn, LZSS each) ->
render_music.h {cmd, intro, loop}. The player (md_main.c) plays the intro
ONCE then loops the loop section — complete + seamless, the authentic
arcade behaviour. 8 songs -> cmds 0x90-0x97 (mapped by per-channel melody
match; 0x94=Rise From Your Grave confirmed). Verified byte-exact. Use the
VGM, not a capture, whenever a rip exists. The capture path below
(`music_sweep.lua` gag) remains the fallback for titles with no rip.

**THE ATTRACT-CONTAMINATION TRAP (2026-09-01): injecting a music
command into altbeast sitting in ATTRACT and capturing a timed window
gives ~14s of the real song, then the game's attract logic STOPS it and
plays its own thing — identical garbage for every command (measured:
0x90/0x94/0x95 share the exact keyon tail past 14s). "The level capture
comes through" because gameplay does not interrupt the music; attract
does. FIX: after injecting, GAG the sound-command latch to 0x80 (a
no-op) so no attract stop/scene-change reaches the driver — the song
then plays and loops uninterrupted (equivalent to the arcade's sound
test). `music_sweep.lua` does this. A timed capture WITHOUT the gag is
contaminated garbage past ~14s.**

**THE FADE LAW (2026-09-01, cost the most): sound-command bytes
0x01-0x40 are MASTER-VOLUME commands** — driver `$0256` does
`ld ($F814),a`, and `$0B27` applies `0x40 - F814` as carrier
attenuation. So 0x40 = full volume, 0x01 = near-silent. The command
latch is re-read every IRQ, so the value it returns WHEN IDLE is set as
the master volume every tick. `snd_render.py` idling the latch at 0x01
(cmd_sweep's silence sentinel) cranked volume to 1 → the music faded to
near-silence within a bar. Idle value MUST be a driver no-op: **0x80**
(bit7 set, `$02FF` masks it to 0 → ignored; not volume, not stop, not
music). This is also why the isolation-sweep tracks and any cold-inject
tap can fade — whatever the latch reads back after the music command
sets the volume. Capture must hold the latch at 0x80 (or 0x40) after
posting the music byte.

Three harness facts that each cost a debug cycle (all in snd_render.py):
1. Command intake is IRQ-driven — the YM Timer A overflow fires RST 38
   ($0038→$0089); the harness must assert the maskable IRQ, not just the
   status flag, or commands never queue.
2. The command latch is READ-ONCE and idle-reads the driver's silent
   no-op sentinel **0x01** (from cmd_sweep's proven gag value). Reading
   0x00 when idle makes the driver queue a stop every IRQ and thrash the
   channels ($0B8B clears F840 each tick).
3. Song-start handler is at word[$048A] (NOT +$AD off the +2 read — a
   one-byte offset error); for 0x94 it is $0528, which LDIRs each 9-byte
   header record into the F840 channel slots (stride $28).

Remaining: by-ear validation (needs a YM2151→PCM synth, or play the
transcoded stream on the 32X via the existing Z80 player) against
`mame altbeast` and the ear-validated `music_track.h` (git 780a65b~1);
then wire snd_render into soundmap_build so SND_MUSIC tracks come from
the renderer instead of the isolation sweep.

## Reuse

Same driver = same map for Japanese Altered Beast (`altbeasj`, uses
`epr-11671` + `epr-11672/73` verbatim) and other S16B titles. Per-title
differences, when they occur, are relocated table addresses — put them
in `games/<title>.toml` and keep the code fixed.
