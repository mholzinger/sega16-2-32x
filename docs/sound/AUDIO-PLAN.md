# Audio plan: which Jenovi patterns we adopt (2026-10-20)

Inputs: `srcref/32X-Live-Mixer-Vol1/MAP.md` (static disassembly), HANDOFF-SOUND.md top
section (design and comparison), LESSONS.md "A shipped 32X cart clocks its FM off the PWM
DMA, in stereo". Standing constraint: no music lane until the game plays (Mike, 2026-10-19).
Speech and SFX on the SH-2 stay. Nothing in this plan is on the port until Mike approves a phase.

Cost unit: one build + ares-headless runs. The rig is not required for any phase A item.

## Phase A: the speech/SFX lane we ship now (one build, instruments + one belt)

| # | Pattern | Cost | Status |
|---|---|---|---|
| A1 | Hold the last sample on underrun, count it | Re-land ec9164f3 (+ c9fd87c2, flag off). 1 build, 2 ares runs | Measured speech only: 0 holds / 6000 frames, probe 266 holds and recovers |
| A2 | Load meter: FRT permille per fill, average + peak | ~10 lines in `speech_pump`, same build | Not built |
| A3 | ISR lateness: FRT gap between DMA end and ISR entry, worst case | ~6 lines in `amb_dma_handler`, same build | Not built |

- **A1 must be re-measured with a real `sfx.bin`.** The 2026-10-19 worktree baked an empty
  bank, so the four-voice SFX mix never ran. Gate: census[2] = 0 on attract and on the
  R60SNDTEST probe; soak, parity and artaudit equal to the same build without it.
- **A2 gives the master's audio cost per vint.** The master-band speed work needs that number,
  and any later SH-2 synth starts from it.
- **A3 decides the DMAC priority with a measurement.** Theirs is 15, ours is 4
  (`r60_speech.inc:233`). Raise it only if the worst gap approaches one buffer. Risk if raised:
  the audio ISR sits above the V-ISR that must land FS before FM_DOWN (LESSONS, fs-write-at-fm0).

## Phase B: only on a play-pass complaint

| # | Pattern | Cost | Trigger |
|---|---|---|---|
| B1 | 4 short buffers (one vint each), 3 filled ahead | Buffer table + fill loop, 1 build | Mike hears late effects. Ours can lag ~128 ms, theirs ~50 ms |

B1 needs A1 first: with one-vint buffers a single missed fill is an underrun, so the hold
belt is what makes it safe.

## Phase C: when music resumes (after the game plays)

These belong to the software YM2151 card (HANDOFF-SOUND "NEXT: software YM2151"), not to
the current Z80 stream.

| # | Pattern | Why it fits there |
|---|---|---|
| C1 | One DMA buffer = one sequencer tick; the DMA-end ISR is the music clock | The synth and the audible clock cannot drift; tempo changes samples per tick, never pitch |
| C2 | ~22 kHz PWM | The synth's planned rate (HANDOFF-SOUND, step 2) |
| C3 | Transport commands `(cmd << 8) \| arg`, acked `0x8000 \| seq` | Catches dropped or repeated commands; needed for the unhandled 0x01-0x40 volume and 0x80-0x8F fade/stop bytes |
| C4 | Stereo longword DMA to L/R | Only if the arcade output is stereo. UNVERIFIED, ask before building |

Open decision for C: which SH-2 runs the synth. Jenovi mixes on the slave. Our slave chain is
the speed pole (1.34 vints, compose-echo-is-the-pole), so copying their placement is not free.

## Not adopted

- **24 voices, 8-bit PCM.** We need 4, and IMA ADPCM keeps the bank small in cart.
- **Run-length precompute and the mono fast path.** They pay off at 24 voices, not at 4.
- **Boot ramp.** We already ramp to centre before the first DMA (`r60_speech.inc:224-229`).

## Phase A results: sound lab only (2026-10-20, branch `soundlab`)

All of this lives in the sound lab (`sndtest/sh/sound.c`, `make sndtest`). Nothing touches the
Altered Beast port until the audio plan is settled (Mike, 2026-10-20). The lab's slave fills one
voice (speech) per side; it carries no effects bank.

New lab screen rows: UNDERRUN now counts holds, PCM LATE (row 24), FILL PK AV (row 25, FRT ticks
at /128 = 5.56 us; 11506 = one 64 ms side).

ares-headless, 6000 frames, speech command fired every 90 frames from 0x44 upward:

| rom | holds | late | fills | fill avg | fill peak |
|---|---|---|---|---|---|
| lab1 (belt + meters) | 0 | 0 | 1513 | 945 ticks (8.2% of a side) | 1115 (9.7%) |
| lab1u (`SNDEXTRA=... -DUNDERPROBE`, 200 ms stall every 32 fills) | 970 | 0 | 1453 | 946 | 1117 |

- A 100 ms stall forced NO hold: the double buffer covers 128 ms. The probe needs 200 ms.
- 970 holds over ~45 stalls = ~86 ms of held sample per stall (200 - ~114), and speech posts kept
  playing after each stall.
- ares charges SH-2 instructions only (no SDRAM waits), so the 8.2% fill cost is a floor, not the
  hardware figure. One voice only.

Open in the lab: a four-voice load reading needs the effects bank in the lab; a by-ear check that
a hold is quieter than the old stale replay.
