# docs/design/INTEGRATION.md — the resource contract between the pipeline and
# the sound engine (2026-09-01, written from the native1 ledger)

The audio thread (docs/sound/SOUND.md, sndtest/) and the pipeline thread
(docs/design/PALSTATIC.md, docs/design/BOSSFIGHT.md) are about to share one machine. This
file is the single map of who owns what, written so neither thread
re-derives or collides. Update it WHENEVER either side claims a
resource.

## COMM registers (the hot contract)

  COMM4   flip-hold tail handshake (0xF1FF family)      [pipeline]
  COMM6   r60 window announce (0xB101) — the ISR arms
          on it. sndtest's lab choreography used COMM6
          for acks; the INTEGRATED engine must not.      [pipeline]
  COMM8   transient post channel, 68K-cleared,
          SH-2-posted, strict per-vint ordering:
          0xBAD1 torn-packet re-mark, 0xBAD2 heal
          (full force-raw), 0xBAD3/4 glow mask grant,
          0xBA50|s per-scene art upload. Codes
          0xBA00-0xBAFF are RESERVED. New SH-2->68K
          posts extend this family via the pending-post
          discipline (see glow_post / mdspr_post).       [pipeline]
  COMM10  68K->SH2 live tile-dirty word (low 13 bits).   [pipeline]
  COMM12  MD V-counter heartbeat (0xD0xx | v_entry).     [pipeline]
  COMM14  boot handshake (0xB007/0x600D), then the
          sound-command log (0x5000|cmd) — the natural
          audio channel. The engine takes ownership
          post-boot; the pipeline's logger yields.       [AUDIO]

## 68K WRAM diag block (0xFFA000-)

  ...through 0xFFA0D4: pre-existing (state_health map).
  0xFFA0D6 heal count | D8 glow grants | DA art uploads
  0xFFA0DC/DE/E0 art-upload chunk state                  [pipeline]
  FREE from 0xFFA0E2. Audio claims start there and get
  recorded HERE.

## SH-2 SDRAM

  .bss region guard: 0x19000, headroom 0x70 BYTES — the
  SH-2 .bss is FULL. Audio SH-2 state goes in fixed
  scratch with boot-init (the cram_key precedent), never
  plain statics.
  Fixed scratch claimed: 0x28900 cram_mirror, 0x28B00
  shadow_lut, 0x28C00-7F cram_key/keygen, 0x28C80-0x28CB3
  slave-busy census/miss_n/text_grp/shadow_cur (BOSSFIGHT M7;
  the older "0x28C80-CF free" line here was stale), 0x28E28-37
  mdspr+glow state.
  AUDITED FREE 2026-09-01 (tools/sdram_audit.py on 41d6bfc5:
  quiet in 7 end-of-run dumps AND no #define in ANY #ifdef
  branch AND outside every declared array's extent — the
  record is docs/design/BOSSFIGHT.md "MAP AUDIT"):
    0x398E0-0x3993F   96B  (md_dbg_hs end .. snap)
    0x28E38-0x28E7F   72B  (glow_streak end .. 0x28E80 prev_n)
    0x28F60-0x28F7F   32B  (psw end .. DRQR)
  That is ALL of it below the stack zone. Audio SH-2 state
  claims come out of these spans and get recorded here.
  .ramtext: the 41d6bfc5 link ends _end=0x18FD0 = 48B of
  region-guard headroom (not 0x70 — that was the v2.1 link).
  2026-09-01 later: spr_pair [2][64] moved from .bss to
  0x3F800-0x3F87F (slave stack floor; slave depth ~330B).
  2026-09-01 night: PAL_SETGEN [192] u16 moved 0x28C00 ->
  0x3F880-0x3F9FF (collision #15 with cram_key/keygen and
  MDSPR_SAT = the blue gravestone). Slave sentinel paint now
  starts at 0x3FA00. 0x28C00-0x28C7F = cram_key/keygen ONLY.
  CAT1MD (C1, shipping candidate): CAT1_PEND [28] at
  0x28F60-0x28F7B (master walk writes, slave cat-1 pass reads).
  PHASECENSUS (probe) claims 0x28E38-0x28E7F and 0x39900-0x3993F;
  flag-off builds leave those free.
  PWM ring buffers: find them SDRAM homes OUTSIDE
  0x19000-0x29000 (the region map) — the audited free
  span list is in m_main.c:54-66 comments.

## Cart (4MB ceiling, no mapper; guard at 0x400000)

  image(text+data+ramtext) -> ends ~0x2F79xx
  0x2F8000 .palscenes (32KB slot)
  0x2F9100 .mdsprart 18.8KB
  0x300000 .gamehigh 256KB
  0x340000 .sprbake 616KB -> ends ~0x3DA100
  FREE: ~0x3DA100-0x400000 = ~155KB
  ** THE ONE ALLOCATION DECISION: this 155KB is claimed
  by BOTH SCALEBAKE (the boss zoom ladder, est 15-40KB)
  and the audio sample banks. Split it deliberately —
  budget numbers from both threads first. **

## CPU budget (measured, 2026-09-01)

  68K: game share ~80%, worst-vint margin 11 lines. The
  command router must be near-free; Z80 BUSREQ steals
  68K bus — battery-gate the integrated build (target:
  ships ~975+, hdlr <60, wall <1.3v).
  Slave SH-2: idle polls 2-3/cycle in play — thin. PWM
  mixing there contends with compose exactly in the
  heavy scenes (the boss). Measure, don't assume.
  PWM ownership law (docs/sound/SOUND.md): SH-2 only, 68K never
  touches PWM regs. The pipeline agrees — it never has.

## Gates for every integrated build

  battery (tools/nat_score.py) + attract ledger
  (psw2=heal2=grants2, uploads 0) + parity statics A/B +
  the engine's own sndtest gates + Mike's pass.
