# LOOP 6 — Retire copy_pages via the write-log ring

> **STATUS: PREMISE FALSIFIED, ARC CLOSED (2026-08-04).** Do NOT build
> the ring. Measurement first, as the loop demands: copy_pages costs
> **0.005 ms/cycle** — 0.25% of the 2.0ms window, dirty bitmap 0000.
> LOOP 3c's dirty-page bitmap already retired it. The "window/ack ~88
> lines is dominated by copy_pages" claim below was an INFERENCE, never
> a measurement, and it is wrong.
>
> The ring was also unbuildable as written: the 0xFFB820 thunks patch
> ADDRESS-FORMATION sites (6-byte lea / move.l #imm), not the 2-byte
> store instructions, so a thunk there never sees the stored VALUE.
>
> The real k1 pre-ack floor was **apply_cram** (0.679 ms/cycle,
> unconditional ~2112-entry CRAM rewrite every k1). It is now gated and
> memoized — see LOOP.md "Iteration 6 LANDED" for the mechanism, the
> ordering law it cost, and the ares falsifier. The rest of this
> document is kept as the record of the falsified hypothesis.

Kickoff doc for a FRESH session. Self-contained: read this + LOOP.md, then
run. The prior session (LOOP 5) landed a PLAYABLE game; this arc takes it
from ~7Hz/57%-reject to the ~20Hz cadence and kills the strobing.

## Mission (one sentence)

Retire `copy_pages` from the k1 in-window FM-hold by shipping a tile
write-LOG over DREQ (the SH-2 applies it straight to the SDRAM tilemap
shadow), so the master never reads the FB staging in-window — cutting the
worst per-vint handler and collapsing the V-gate reject band.

## Why this, why now (the falsified-down chain)

LOOP 5 proved: the 64-67% reject band IS the per-vint 68K handler
overrunning the frame. Cut the handler, the band drops (65% -> 57% when
the text stream moved off COMM onto DREQ). The band is now gated by the
**heaviest accepted vint = k1**: window/ack ~88 lines (dominated by
`copy_pages`, the ~2ms FB-staging read) + tail 151 = ~239 of 262 lines.
k0/k2 vints (~155) already fit. So k1 is what overruns -> the next vint
fires late -> reject -> the strobing cadence. **Kill copy_pages on k1 and
the k1 handler drops to ~181 (fits) -> the k1-triggered rejects vanish.**

## Starting state

- HEAD: `4a09632` (branch `unpair-rebase`). Playable build: `5a04686e`.
- ares (state_health, build 5a04686e): vints/cycle 7.01, V-gate rejects
  57.2%, blit skips 0, dreq_incomplete 23, tail 151, worst-handler k1
  window 88.
- MAME scoreboard (rom rig): title 49 / scream 92 / eyehold 3.4 / demo 52
  / demo2 21. eyehold+demo2 at baseline; title/demo carry ~1-frame DMA
  text latency the frame-exact anchors penalize (invisible on hardware).

## The design (from LOOP.md "Iteration 1b" — never implemented)

`copy_pages` exists only because the game's tile writes land in FB
staging (0x852000 MD / 0x24012000 SH-2) and the master must copy dirty
pages into the SDRAM tilemap shadow — an FB read, so FM=1, so in-window,
so on the k1 critical path. Replace it:

1. **Thunk the game's tile-store instructions** to ALSO append
   `(offset, value)` to an MD-RAM ring (the store still lands in FB
   staging so game read-backs are unaffected). Store sites are
   enumerable — LOOP.md iteration 1b lists them (0xD84 fill helper,
   0x2AD4-cluster vint words, 0x6836 seam writers, 0x1BA1C/2C fills,
   etc.); patch_report.txt / patch_game.py has the address-formation
   sites. The dirty-page BITMAP thunks already exist (0xFFB820, tile_
   thunks.h) — this extends them from "mark page dirty" to "log the
   write".
2. **Ship the ring over DREQ**, appended to the existing packet (same
   pattern as the text chunk LOOP 5 added). The packet is already
   512 spr + bitmap + text base + 256 text + 2 pad = 772 words.
3. **SH-2 applies the log** directly to TILEMAP_U (SDRAM shadow) — no FB
   read. `copy_pages` retires; its ~2ms/88-line k1 FM-hold is gone. The
   FB becomes pure display double-buffer.

## HARD-WON LESSONS (do not re-learn these the hard way)

- **DMA packet MUST be a multiple of 4 words.** The DREQ FIFO drains in
  4-word bursts; a non-multiple leaves the tail un-drained -> TE never
  sets -> stale frame (ares dreq_incomplete). LOOP 5 hit this at 770;
  fixed at 772.
- **The DMA drains ONE transfer then stops.** Re-arm it every window
  (dreq_rearm), and push ONLY on gate-accepted vints (window_ok). An
  off-cycle push into an undrained FIFO BLOCKS the 68K on its
  unconditional group-write (`fifo[0]=s[3]`) — a hard mutual deadlock.
  This bit TWICE. If you grow the packet, keep this invariant.
- **The MAME debugger is THE tool.** Read the SH-2 DMAC registers live
  via lua — SAR0 0xFFFFFF80, DAR0 ..84, TCR0 ..88, CHCR0 ..8C (bit0 DE,
  bit1 TE), DMAOR 0xFFFFFFB0 (bit2 AE=address error) — plus the 68K FIFO
  view 0xA15107 (bit7 full) / 0xA15110 (length). Guessing DMA behavior
  cost two deadlocks; one register read (TE=1/TCR=0/FIFO-full) solved it.
- **MAME cannot see the band.** It rejects ~0% no matter what (3x faster
  SH-2). state_health on an ARES savestate is the only verdict.
- Tail probe lives in the shim: 0xFFB0FE = V at entry, 0xFFB0F4 =
  packed (max total handler span << 8 | window span). state_health
  decodes it. Keep it wired.

## Gates (every commit)

1. Boots + runs attract title->scream->eyehold->demo, NO hang
   (probe: watch 0xFFB0F0 ent + SH-2 nwin advance; a freeze = deadlock).
2. MAME scoreboard: no scene regresses (`tools/parity_run.sh`).
3. Region guard passes, rom stamped `normal`, `make PRESSURE=1` holds.
4. PLAYABILITY PRESERVED — do not break the LOOP 5 milestone.

## Falsifier

state_health on ares: **V-gate rejects toward 0, vints/cycle -> 3,
dreq_incomplete ~0.** copy_pages gone from the k1 window should show as
the worst-handler window span dropping from ~88 toward ~30.

## First moves for the new session

1. Read LOOP.md (iteration 1b store-site list + LOOP 5 sections) and this
   doc. `git log --oneline -10` for the DMA-packet commits (5f0ec0b,
   5a04686).
2. Enumerate the tile-store sites (patch_report.txt, patch_game.py, the
   existing 0xFFB820 dirty-bit thunks in tile_thunks.h) — confirm the
   set that write tilemap staging.
3. Design the ring format + packet extension (keep 4-word alignment!).
   Prototype the SH-2 apply first (cheap to validate the shadow updates),
   then the MD thunks.
4. Build a boot/no-hang probe BEFORE trusting any packet-size change.
5. Hand Mike an ares state_health each milestone — the band is the judge.

## Key files

- `md_src/md_main.c` — MD shim (vint handler, DREQ push, thunks setup)
- `sh_src/m_main.c` — SH-2 master (window handler, DMA snapshot/apply,
  copy_pages, dreq_rearm)
- `sh_src/s_main.c` — SH-2 slave (compose, stream service)
- `tools/patch_game.py` / `tools/patch_report.txt` — game patching + the
  store-site archaeology
- `tools/state_health.py` — the ares savestate verdict (reject %, tail)
- `sh_src/tile_thunks.h` — generated dirty-bit thunks (extend these)
