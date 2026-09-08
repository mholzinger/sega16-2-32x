# docs/sound/SOUND.md — the sega16 sound engine design (2026-08-31)

> **RESOLVED 2026-09-01 — the ROM and the emulator were both fine.**
> Root cause: `Defocus: Pause` in Mike's ares GUI settings — a
> regenerated default profile (his Aug 13 original, preserved at
> `settings.bml.bak-tick-hunt`, had `Defocus: Allow`). Every switch to
> the terminal to report a result PAUSED the emulation: sound cut ~3-6
> frames after starting (the MUS FEED 0800/0901 freeze), and presses
> made while unfocused were ignored (the "dead input" screenshot:
> COUNT 00, PAD 1000 = six-button detected, nothing pressed). Comm
> traces on the headless rig cleared the ROM: one post per press
> through the full interactive choreography. Fix: set Defocus to
> Allow. Full write-up in `docs/sound/HANDOFF-SOUND.md`.

Research pass over four sources: this repo, `32x-builder` (Mike's own,
copy freely), `32x-builder/srcref/d32xr` (derive architecture, don't
copy), `srcref/jtcores` + `~/src/mame-ref` (hardware facts, cite only).
This document is the design that fell out. Nothing here is implemented
yet; the repo is silent by construction (Z80 parked in reset,
`md_src/md_start.s:118-139`).

## Verdict up front

**Do not run the arcade Z80 program (`epr-11671.a10`) on the MD Z80.**
Three facts from the RTL/MAME surveys kill it:

1. The driver's inner loop is a uPD7759 slave-mode byte-pump: the 7759's
   DRQ fires a Z80 NMI per sample byte, and the Z80 feeds port 0x80 from
   a 16 KB banked ROM window (`jts16b_snd.v:227`, `segas16b.cpp:1241-1245`).
   The MD has no uPD7759 and nothing to pump into.
2. YM2151 -> YM2612 is not a register remap: 8 ch vs 6, KC/KF pitch vs
   F-num/block, DT2, noise channel, richer LFO, different operator
   register interleave (see "OPM->OPN2" below).
3. The MD Z80 runs at 3.58 MHz vs the arcade's 5.0 MHz (0.716x); the
   driver's cycle-counted loops and Timer-A-polled tempo would drag.

**Instead: a replacement three-lane engine consuming the same command
byte stream.** The arcade contract we must honour is tiny — a one-byte
command latch — and this repo already captures it, verified
(`md_src/md_main.c:2039-2044`, attract emits `0x00/0x41/0xB6/0x46`,
`NOTES.md:343-345`).

```
arcade 68K game code
  │ writes byte to work RAM 0xFFF0C4 (MCU sound mailbox — already live)
  ▼
68K COMMAND ROUTER (new, small)          table: cmd byte -> action
  ├── music cmds ──> Z80 FM PLAYER ──> YM2612 + PSG   (d32xr model)
  ├── sfx cmds ────> either lane per-table
  └── speech cmds ─> COMM relay ──> SH-2 PWM VOICE POOL (32x-builder engine)
```

- **FM lane**: Z80-resident streaming register player owning YM2612+PSG;
  68K feeds it pre-transcoded data 512 bytes at a time (d32xr protocol).
- **PCM lane**: SH-2 PWM mixer (16 kHz mono, DMA1 ping-pong, IMA ADPCM)
  lifted from 32x-builder; plays the uPD7759 speech/SFX, pre-decoded
  offline. Replaces the 7759 entirely — kills the NMI-pump problem.
- **Router**: extends the existing MCU-mailbox consume loop; the mapping
  table is derived from the (plaintext, 32 KB) `epr-11671` disassembly
  plus MAME observation.

The 8-channel YM2151 score fits because the MD gives us **6 FM + 4 PSG**
targets: the transcoder assigns arcade noise (YM2151 ch7 reg 0x0F) to
PSG noise, overflow melodic channels to PSG squares, the rest to YM2612.
d32xr's player already interleaves YM2612 and PSG writes in one stream.

## The arcade contract (facts, cited)

Sources: `srcref/jtcores/cores/s16b/hdl/jts16b_snd.v`,
`jts16b_mapper.v`; `~/src/mame-ref/src/mame/sega/segas16b.cpp`,
`315_5195.cpp`. jtcores and MAME agree on every fact below.

- **Command latch**: 315-5195 mapper reg 3. Writing it (68K at
  `$FE0007`, or the i8751 directly) asserts a **level-held Z80 INT**;
  the Z80's read of the latch (mem `E800` or I/O `C0`) clears it
  (`jts16b_mapper.v:367-373,391-392`; `315_5195.cpp:118-121,390-395`).
  No busy/ack readable by the 68K — fire and forget. For Altered Beast
  the game never touches the latch; the **i8751 forwards work-RAM
  mailbox `0xFFF0C4`** (`NOTES.md:66-68,93-94`) — which is exactly what
  our MCU shim already intercepts.
- **Z80 memory map**: `0000-7FFF` program ROM (32 KB, plaintext for
  set 8), `8000-DFFF` banked sample window, `E800` latch, `F800-FFFF`
  2 KB RAM (`jts16b_snd.v:93-110`; `segas16b.cpp:1813-1837`).
  RTL nuance: the bank latch *overwrites* A14, so the window is really
  16 KB at `8000-BFFF` with `C000-DFFF` aliasing (`jts16b_snd.v:70-91`).
- **Z80 I/O**: `00/01` YM2151 addr/data; `40` = 7759 control fused with
  bank bits (D7 -> /MD inverted, D6 -> /RESET, D3..D0 bank on the
  171-5521 board); `80` R = busy<<7, W = 7759 data byte; `C0` latch
  (`jts16b_snd.v:100-132`; `segas16b.cpp:1151-1228`). Write-order law:
  `md_w` before `reset_w` (`segas16b.cpp:1156-1157`).
- **Interrupt model**: INT = latch only. NMI = 7759 DRQ only.
  **YM2151 IRQ is deliberately unconnected** (`jts16b_snd.v:206` open
  port; `segas16b.cpp:4368` commented out) — **tempo comes from polling
  YM2151 Timer A status**. Our replacement driver's timebase can
  therefore be anything with equivalent resolution; d32xr polls YM2612
  Timer A the same way.
- **Clocks**: Z80 5.0 MHz, YM2151 4.0 MHz, uPD7759 640 kHz, i8751 8 MHz
  (`cores/s16/cfg/mem.yaml:10-32`; `segas16b.cpp:157-161,4089-4121`).
- **Sample ROMs**: `opr-11672.a11` + `opr-11673.a12`, 256 KB total,
  16 pages of 16 KB, region offset 0x10000 (`segas16b.cpp:4824-4830`).
  Already on disk: `roms/altbeast/`.
- **Mix balance**: FM `rsum 47k` vs PCM `rsum 10k, pre 0.56` + ~4 kHz
  LPF on the 7759 (`cores/s16b/cfg/mem.yaml:4-11` via s16) — speech is
  ~2.6x hotter than FM into the summing node and heavily low-passed.
  Reproduce this ratio between the PWM lane and the YM2612.

### OPM -> OPN2 transcoding facts (for the offline tool)

From ymfm headers (`~/src/ares-debug/thirdparty/ymfm/src/ymfm_opm.h`,
`ymfm_opn.h`):

| | YM2151 | YM2612 |
|---|---|---|
| Channels | 8 (+noise on ch7 op4) | 6, banked reg file (ch4-6 at +0x100) |
| Pitch | key code + fraction (regs 28-3F) | F-num + block (A0-A7) |
| Key on | reg 08, ch in bits 0-2 | reg 28, op mask bits 4-7, bank bit 2 |
| Timers | 10/11/12, mode 14 | 24/25/26, mode 27 |
| LFO | freq+waveform+AM/PM depth | 3-bit rate only |
| DT2 | yes (C0-DF bits 6-7) | none — fold into frequency |

TL/AR/DR/SR/RR/SL/MUL/DT copy 1:1, but the operator-to-register
interleave differs (OPN2 skips slot indices) — a linear voice-table copy
scrambles. All of this is an **offline tool problem**, never runtime.

## What we already own

**From 32x-builder (Mike's code — copy verbatim):**
- PWM engine core: `Mars_InitPWM` / `amb_dma_handler` / `amb_sound_init`
  (`sh_src/sound.c:123-187,726-787`) + IRQ asm
  (`sh_src/mars_start.s:530-551,637-682`). 16 kHz mono, [8..1430] duty,
  DMA1 ping-pong 2x1024 samples (64 ms), soft-clip 4:1. Fully generic.
- IMA ADPCM matched pair: decoder `sound.c:341-382` (~40 lines, ~15
  cyc/sample) + `tools/adpcm_bake.py`; `tools/wav_to_pwm.py` for raw.
- The cooperative-pump scheduling pattern (pump from idle **and** at
  checkpoints between render passes; underrun counter; buffer-size A/B)
  — the thing that makes PWM survive render load (`DEVLOG.md:1010-1075`).
- The **YM2612/Z80 arbitration laws**, hardware-derived
  (`md_src/md_main.c:104-117,380-457`):
  1. Steady state = reset HIGH + bus GRANTED AND HELD (Z80 frozen, the
     Yamaha lives). Never park in reset — Z80 reset resets the YM2612.
  2. Boot order is law: release reset FIRST, then busreq. (This is the
     answer to our `25e1cf9` bus-grant-poll boot hangs.)
  3. Frame-spaced YM writes sound; back-to-back bursts land silent on
     hardware even when the busy-poll passes (B00245/B00246). Short
     busy guards + fixed settle delays.
  4. PSG needs explicit attenuation-off latches; it is not reset by
     Z80 reset (we already do this, `md_start.s:129-139`).
- The idioms: split int+frac resample cursor (never 16.16), the
  `|0x20000000` cache-through alias, per-asset `_SAMPLE_RATE` headers.

**From d32xr (in srcref — derive the architecture, write our own):**
- The whole Z80-music shape: `src-md/z80_vgm.s80` (814 lines) — Z80
  owns YM2612+PSG, busy-polls Timer A for tempo, executes a
  VGM-command stream from a **4 KB ring in Z80 RAM, eight 512-byte
  banks**; the Z80 cannot touch cart or 32X regs (32X Tech Notes 15/22,
  quoted at `z80_vgm.s80:7-13`), so it raises a request byte and the
  **68K copies the next 512 decompressed bytes in** (`crt0.s:2534-2762`).
  Producer-blocking flow control: late 68K = music stalls, never
  glitches.
- 68K discipline: VBI only sets `need_bump_fm`; all FM work runs from
  the main loop at IPL 0. Busreq windows short and skippable.
- Loader: busreq, deassert reset, wait grant, clear Z80 RAM, byte-copy
  driver to `0xA00000`, pulse reset, run (`crt0.s:291-331`).
- Pitfall recorded there: PWM_CYCLE/CTRL must have **one owner** — d32xr
  lets the 68K DAC path reprogram PWM and it conflicts with the SH-2
  mixer by design (alternative drivers, never both). Ours: SH-2 owns
  PWM, full stop. The 68K never touches PWM registers.

**Already in this repo:**
- Command capture, live + verified: `MCU_SNDCMD` 0xFFF0C4 pump
  (`md_src/md_main.c:2039-2044`), currently logs to COMM14 and discards.
- Empty sockets shaped for the 32x-builder engine: PWM vectors installed
  (`sh_src/mars_start.s:192,249`), `slav_dma_irq` full save/restore
  calling the empty `amb_dma_handler` (`mars_start.s:805-849`,
  `s_main.c:3-6`).
- Sample source ROMs on disk (`roms/altbeast/opr-11672/11673`, and
  `epr-11671.a10` for disassembly).
- `megadriveref/Altered Beast (USA, Europe).md` — fallback source of a
  ready-made YM2612 arrangement (NOTES.md:1033-1050 plan). Kept as
  plan B / fidelity cross-check; the transcoder pipeline is preferred
  because it is toolkit-shaped (works for every S16B title, not just
  ones with MD ports).

## Asset pipeline (tools to build)

1. `tools/upd7759_rip.py` — walk the 7759 header/block table in
   opr-11672/11673, decode the variable-rate 4-bit ADPCM frames to WAV
   per sample index. Derive the decode from upstream MAME
   `src/devices/sound/upd775x.cpp` (**not in the local mame-ref
   checkout** — src/devices/sound is absent; fetch that one file from
   upstream MAME as reference). Then re-encode with the existing
   `adpcm_bake.py`.
2. `tools/ym_tap.lua` — MAME autoboot script on `mame altbeast` tapping
   soundcpu I/O writes to ports 0/1 (+ 0x40/0x80) with frame timestamps,
   driven per sound command. Output: raw OPM register stream per music
   track. House style — same shape as `tools/health_mame.lua`.
3. `tools/opm2opn.py` — offline transcoder: OPM stream -> YM2612+PSG
   command stream (our Z80 player's native format, VGM-like: reg writes
   + wait opcodes). Does channel assignment (6 FM + PSG overflow +
   noise->PSG), KC/KF->fnum/block, DT2 folding, LFO approximation.
   All fidelity decisions happen here, auditable, per-track.
4. Command map table — from `epr-11671` disassembly (plaintext):
   command dispatch -> {music N | sfx N | speech N | stop | fade}.

## The sound-test ROM (`rom/sndtest.32x`) — where the engine gets built

Decision (Mike, 2026-08-31): the engine is developed and proven in a
**dedicated sound-test ROM**, not inside the game. The main rom stays
untouched until the engine is done; integration is then a link step,
not a development phase.

Why this is the right shape:

1. **The interface guarantees transferability.** The engine's entire
   input is one command byte — the same latch contract the arcade uses.
   The test ROM drives that byte from a joypad menu (pick 0x00-0xFF,
   post it, hear it — exactly the arcade's own sound-test mode). If it
   works there, the game integration is "call the router from the
   mailbox pump" and nothing else.
2. **Isolation from the tuned pipeline.** The main rom's 68K vint and
   parity gates are the project's tightest, most instrumented
   resources; debugging audio inside them confounds both. A tiny ROM
   boots instantly, needs no game patching, and every audible glitch
   is the sound engine's fault by construction.
3. **This is the toolkit deliverable.** `sndtest` + the asset pipeline
   IS the "System 16 sound board model" for the kit: for the next
   title, swap the command map + baked assets and you have that game's
   sound test running before its video port exists. Altered Beast is
   just the first client.
4. Precedent: 32x-builder's `make maze-ares` standalone build was
   exactly this pattern for its Z80 work, and it paid off.

Construction rules:

- **Same repo, same sources, second link target.** The engine modules
  (`snd_*.c` on both CPU sides, the Z80 driver, baked assets) are
  compiled identically for `sndtest.32x` and `s16.32x`; only the main
  loops differ. No forked copies — divergence between "proven" and
  "shipped" code would void the proof.
- On-screen instrumentation: current command, Z80 ring fill, feeder
  starve count, PWM underruns, YM canary states, per-lane mute toggles.
  Failure legible from a photo, per 32x-builder's canary discipline.
- **A synthetic-load mode** (hold a button: SH-2 framebuffer-hammering
  busy loop + 68K vint-scale busywork) to approximate the game's bus
  contention. This narrows, but does not close, the gap below.
- Listening rig is **ares**; MAME 32x is for logic/regression only
  (its PWM/timing model is not authority — CLAUDE.md).

**The proof boundary, stated honestly:** the test ROM proves the engine
correct and stable, including under synthetic load. It cannot prove the
main rom's real contention profile (FB write stalls, DREQ traffic, the
actual vint tail). Integration therefore keeps one measurement phase of
its own — it is cheap because the engine is no longer a suspect.

## Build phases

All phases through P4 happen in `sndtest.32x`. The main-rom gates
(parity statics, region guard, ares play pass) apply only at P5.

- **P0 — sndtest scaffold** — **DONE 2026-09-01.** `make sndtest` ->
  `rom/sndtest.32x`. Skeleton is 32x-builder lineage in `sndtest/`
  (md_start.s/mars_start.s/ld scripts verbatim; trimmed 68K command
  server with the busreq-park boot, the proven YM write path as cases
  14/15, and the router front door as case 0x20; SH-2 menu on the MD
  text plane). Verified headless-MAME: menu draws, pad edges step the
  command byte, A posts it, and the 68K ack (COMM6 {count,last}) reads
  back correctly. C already fires the case-15 YM smoke patch — audible
  YM output is P2's proof, on ares (MAME is logic-gate only here).
- **P1 — PWM speech lane** — **DONE 2026-09-01, Mike's ares pass:
  "all pcm sounds up to 0B play pressing A!!"** The whole lane:
  - `tools/snd_tap.lua` taps the arcade oracle's sound-CPU io (YM/7759/
    latch) with timestamps; a 3-min attract capture yielded 159k speech
    bytes, 83k YM writes, and the live command stream.
  - `tools/upd7759_decode.py` replays the slave-mode 7759 state machine
    (upstream MAME upd7759.cpp, fetched into mame-ref) over the tap:
    **12 unique utterances** decoded to 16 kHz WAV (`sndtest/speech/`),
    manifest-tagged with preceding command bytes (speech triggers look
    like 0x41/0x43/0x46/0x4F/0x51...).
  - `tools/speech_bake.py` -> `speech_bank.c` (133 KB IMA ADPCM bank).
  - `sndtest/sh/sound.c`: the 32x-builder PWM core (Mars_InitPWM, DMA1
    ping-pong 2x1024 @16 kHz, soft-clip) with the per-game pump
    replaced by a generic 4-voice pool; banks baked at the mixer's own
    rate so decode IS mix. Trigger mailbox = cache-through-aliased
    shared statics (one linked image, both SH-2s).
  - Menu: CMD 00-0B plays that bank sample; PCM ACT + UNDERRUN rows.
  Proof (ares-headless, `~/src/ares-debug` fork): scripted A-press ->
  PCM ACT 01 during the 2.2 s utterance, ACT 00 after, UNDERRUN 0000.
  A/B vs the arcade oracle (`sndtest/speech/arcade_ref/`, MAME
  -wavwrite sliced at tap timestamps): Mike, 2026-09-01 — "sounds
  correct." Decoder rate reconstruction validated by ear.
  **MAME 32X SEGFAULTS on this rom** (exit 139, both -sound arms) — its
  PWM-DREQ DMA1 modeling is the crash site by elimination (P0 rom ran
  fine); the lab's machine gate is ares-headless from here on. Decoded
  WAVs handed to Mike for phrase identification (utt_000 = cmd 0x41
  suspect "Rise from your grave").
- **P2 — Z80 unpark + YM2612 write path** — **PROVEN ON ARES
  2026-09-01**, banked early: P0's scaffold shipped with the
  32x-builder park + YM path built in, and Mike heard the two-voice
  electric-buzz drone from sndtest's C button on ares ("sounds like an
  electric buzzer" — that is the patch's designed sound). The
  `25e1cf9` landmine is dead: busreq-park boots clean and the YM
  chain (COMM relay -> 68K -> bus arbitration -> paced writes) sounds
  on the hardware-truth proxy. Remaining P2 scope folds into P3 (the
  Z80 has still never RUN code here — only been parked around).
- **P3 — Z80 streaming player + 68K feeder** — **BUILT + STREAMING
  PROVEN ON ARES 2026-09-01; awaiting Mike's ear pass.**
  `sndtest/z80/player.asm` (wla-z80): our own driver on the d32xr
  architecture — Z80 owns YM2612+PSG, executes a 4KB 8x512B ring in
  Z80 RAM, Timer A (NA=917, 1.0043ms) polled for tempo, opcodes
  {wait n | F0 reg val YM-I | F1 YM-II | F2 psg | FF end}. **Stream
  law: never write reg $27 — it is the player's clock** (the P4
  transcoder must obey). 68K: case 33 boot dance (proven case-12
  order) + once-per-vblank feeder, <=2 blocks/frame, producer-blocking
  starve-stall, COMM14 = {produced,consumed}. Contract addresses
  parsed from the asm by tools/z80_pack.py (single source of truth).
  tools/mus_testgen.py emits a 6KB arpeggio+PSG-bass test loop (bigger
  than the ring, so wrap + refill are exercised). Proof (ares-headless
  --dump of Z80 RAM): MSTAT=1 after GO; after 22s consumed=2 blocks,
  produced=10, inflight pegged at 8 — Timer A ticking, ring wrapping,
  feeder pacing. Menu: START toggles; MUS FEED row shows the counters.
  Ear pass 2026-09-01: v1 clipped hard (alg-7 sting patch, four hot
  carriers + PSG att 4 — a voice 32x-builder abandoned unvalidated);
  v2 (alg-4 bell, PSG -18dB, settle nops in the Z80 YM writes) passed
  — "I think you resolved the z80 issue." **P3 DONE.**
  OPEN OBSERVATION: a persistent from-boot ticking heard on ares but
  NOT in OpenEmu (Mike calls it resolved). ares is the hardware-truth
  proxy, so this stays on the books until a real-hardware listen. The
  isolation probe is one command: `make sndtest SNDEXTRA='-O2
  -fomit-frame-pointer -DNOPWM'` disables the PWM lane wholesale —
  tick-gone means PWM seam/carrier, tick-still means MD side.
- **P4 — transcoder + command router** — **IN PROGRESS 2026-09-01.**
  `tools/opm2opn.py` exists and transcoded its first cut: attract
  t=9.0-35.5s (title theme + demo music) -> 29.8KB player stream,
  START on the menu plays it (cycle: off -> arcade cut -> arpeggio).
  Mapping facts baked in (from ymfm, cited in the tool): OPM/OPN
  operator slots align in wiring order (OPM 0x40+8s+ch -> OPN
  0x30+4s+(ch%3), bank ch/3; keyon mask passes through); pan bits
  SWAPPED; KC/KF at the arcade's 4.0MHz -> fnum/block at 7.67MHz;
  stream opens with a full state snapshot so the cut restarts clean.
  This cut: 4 OPM channels active (98 keyons), fits FM 1:1, no PSG
  overflow; 119 DT2 values dropped (warned — audibility unknown).
  Course corrections, all same-day: attract has NO BGM (arcade
  demo-sounds behavior — Mike's catch), so the music needed a
  coined-up gameplay capture (command 0x94 = round-1 BGM start, first
  command-map entry); value-dedup shadow shrank the stream 5.3x
  (566KB -> 106KB — the arcade driver rewrites unchanged values
  ~120/s); Timer A factor is 144 not 72 (score played at exactly half
  speed until fixed — player.asm carries the law). **EAR VERDICT,
  Mike on ares 2026-09-01: "This sounds incredible."** The full
  transcode pipeline — oracle tap -> opm2opn -> Z80 player on real
  YM2612+PSG — is validated end to end at arcade tempo.
  **P4 COMPLETE 2026-09-01 (commit 780a65b).** The whole soundboard is
  mapped by direct latch injection (`tools/cmd_sweep.lua`: per-command
  soft-reset isolation + read-gag — five measured MAME laws documented
  in the file) and generated by `tools/soundmap_build.py`: **10 music
  tracks, 63 YM jingles/sfx, 22 speech commands** (bank 29 samples).
  The router dispatches every arcade command byte for real: music and
  jingles to the Z80 player (one-shots auto-stop via MSTAT), speech to
  the PWM pool via the COMM4 doorbell; MIXED entries fire both lanes.
  ares-verified. Known facts for the game integration (P5): command
  0x00 = stop-all, 0x01-0x0C silent no-ops, 0x8C HALTS the sound CPU
  (guard it in the router), music lives at 0x94-0x9D. Deferred: track
  loops are 14s cuts (loop points unpolished); DT2 dropped; no
  music-over-sfx layering (arcade driver mixes, our player is
  exclusive per track).
- **P5 — main-rom integration** (~1-2 sessions). Link the proven
  modules into `s16.32x`; router consumes the live mailbox instead of
  the menu. Measure: 68K vint tail with feeder active, parity statics
  unmoved, ares play pass. Then balance polish: arcade mix ratio
  (PCM ~2.6x FM, LPF), fades, ducking, PAL PWM_CYCLE.

## Open questions / risks

- **68K vint budget** (`docs/audit/audit_sdkmodel.md:99`): the feeder
  and router are main-loop work, but the budget was never provisioned.
  Measure the feeder's worst 512-byte copy against the vint tail early
  in P3.
- upd775x decoder reference must come from upstream MAME (local
  checkout lacks src/devices/sound).
- OPM LFO/noise fidelity through opm2opn is lossy by nature; acceptance
  is Mike's ears against the `mame altbeast` oracle, per track.
- MC-8123 encrypted sound CPUs (altbeast2/4) are irrelevant — set 8's
  Z80 ROM is plaintext.
