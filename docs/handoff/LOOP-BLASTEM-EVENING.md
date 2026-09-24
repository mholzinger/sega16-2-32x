# LOOP BRIEF — O-8 BlastEm evening queue (2026-09-23, unattended)

**STATUS, loop ended 2026-09-23 evening: all three items done.** Item 3
(BLASTEM.md 10): the RTL count reaches ~190 clk/group vs the rig's 336;
the FB store path is ~80 of it, so the 1.6x is a blit-rate figure whose
remainder is bracketed (instruction-fetch misses on a 5.4x oversubscribed
cache, DDR3 latency, the shared ddram channel) with a rig probe recipe.
Item 1 (BLASTEM.md 11, fork fa7550d): cart-ROM arbiter from IF.sv behind
BLASTEM_BUS_ARB; < 0.5% of either CPU's frame on the line rom; the
FIFO-era poll figure is SH-2-internal CPU/DMAC sharing, not the adapter.
Item 2 (BLASTEM.md 12, fork pushed): --trace-comm/flip/dreq with ares's
columns, checked on bprof3 for 120 frames. sega16 commits are local only.
The two mislabelled fork commits were rewritten afterwards on Mike's call (now 2febe53 fix / cd3a5f7 knob / 9f980a6 diagnostics; tree verified identical; force-pushed).

Mike is away for the evening. This loop works the three open O-8 items in
order, one at a time, and writes results into the record as it goes.
Re-read this file at every wake. `docs/design/BLASTEM.md` sections 7-9
hold the state of the instrument; STATE.md's INSTRUMENT STATUS block
names it.

## Rules (hard)

- sega16-2-32x: commit locally, NEVER push. Fork
  (`~/src/blastem-0c61d0d95463`, github.com/mholzinger/blastem): commit
  and `git push origin main` are allowed; NEVER force-push without Mike.
- No rig launches, no MAME windows, nothing interactive. BlastEm runs
  headless only: `-b N -m 32x` from `/tmp/blastem-eval` (BIOS files live
  there). ares: `~/src/ares-debug/build_macos/headless-ui/Release/ares-headless`.
- A BlastEm rebuild is ~5 min (31 MB generated `sh2.c` under LTO): run
  `make -j8` in the background writing to a log, arm a Monitor on
  `make rc=`, do reading work meanwhile. Never edit `32x.c` while a build
  is compiling it; kill the make first.
- Every timing number: prove same-picture first (group counts within a
  few %, game RAM changing, M68KSTAT pc not in the shim) before quoting.
- Do not touch the line rom or `.build_flags`. Probe roms:
  `rom/night/rigblit_bl.32x` (RIGBARCODE+BODYPROF+RIGBLIT, `_bprof`
  0x06003558, BLITPROF 0x06028FD0, `rigblit_lines` 0x06003541),
  `rom/night/bprof3.32x` (`_bprof` 0x06003544).
- Compare scripts and dumps: `/tmp/blastem-eval/` (`xbp_compare.py`,
  `verify_fm.sh <wait>`, `bp_*`/`xb2_*` dumps). ares baseline over
  frames 700-760: master 10494 groups / 32 calls / 6.53 ticks per group,
  46.8 blit lines per call; 1500-1560: 46.5; 3400-3460: 45.3.
- Write results as they land: BLASTEM.md gets a numbered section per
  item; LESSONS an entry only for a finding that must never be
  re-derived; STATE's BlastEm block updated when the instrument's scope
  changes. Dry voice: claim, number, file:line.
- If an item is blocked, write down exactly where and move to the next.
  Stop the loop when all three are done or blocked; leave a one-paragraph
  status at the top of this file either way.

## Item 3 (first): locate the rig's 1.6x in the MiSTer RTL

Question: the rig's master blit runs 1.6x slower than ares (RIGBLIT
67-86 vs 45-47 blit lines per call; BLITPROF ~10.5 vs 6.53 FRT ticks
per 32-byte group, 1 tick = 32 SH-2 clocks). BLASTEM.md 9 shows the
FPGA's framebuffer WRITE path is fast (VDP.sv VDPFIFO 4 entries, 6
system clocks a word; SH7604 BSC.sv CS2 T0/T1/TW/T2 with WCR1 0x0055;
IF.sv SH_VDP handshake ~3 clocks). So where do ~336 SH-2 clocks per
group go on the FPGA, against ~209 on ares?

Derive from `srcref/S32X_MiSTer/rtl/`:
1. SDRAM reads: `SH/SH7604/BSC.sv` states TRAS/TWCAS/TRCAS/TRD/TWNOP,
   with MCR = 0x0AB8 from the BIOS table (RCD, TRP, TRWL, CAS latency
   bits per the SH7604 manual), and the burst length for a 16-byte
   cache line fill (`cache_2way.sv`). Then the board side: the SH-2's
   SDRAM is behind `sdram.sv`/`SH_mem.v` (CAS 2, tRCD 2 at 107 MHz,
   NO_WRITE_BURST): does a core-side burst become one board burst or
   four single accesses? Count clocks per line fill.
2. The blit inner loop: `sh_src/m_main.c` `blit_half` (~7830) reads
   sbuf (cached SDRAM) and stores to the FB. Per 32-byte group: line
   fills, stores, and whether the core stalls on each external store
   (write buffer present or not in the FPGA core -- `SH/core`).
3. Refresh and arbitration: `RFS_REQ`/TRFS states, and the MD's cart
   accesses through the same SDRAM (`IF.sv` ROM_ST RS_SH_WAIT /
   RS_MD_WAIT: do MD ROM reads block SH-2 SDRAM cycles?).
4. Sum a cycles-per-group and compare with 336 (rig) and 209 (ares).
   Which term is the 1.6x? Write it as BLASTEM.md section 10 with the
   file:line for every clock counted. If the derived number disagrees
   with 336 by more than ~20%, say so; that is the finding.

## Item 1 (second): adapter-bus arbiter in the fork, derived from IF.sv

The decompile thread measured on hardware: a tight SH-2 COMM0 poll made
the 68K's push 4.4x slower (0.28 vs 0.063 lines/word). No emulator shows
it. BlastEm charges fixed per-access costs per CPU and never contends.
IF.sv is the spec: MD sysreg accesses (`MD_SYSREG_SEL`, `MD_REG_DTACK_N`
~298-460) and SH-2 sysreg accesses (`SH_SYSREG_SEL`, ~470-640) go
through one register file; MD and SH VDP accesses share `VDP_A/VDP_DO`
and one `VDP_ACK_N` (~915-1000); the SH-2 side is held by `SHWAIT_N`
(IF.sv:1031), the MD by DTACK.

Build it in the fork (32x.c): an `adapter_busy_until` in MCLK; each
adapter access (68K sysreg/VDP/FB, SH-2 sysreg/VDP/FB/COMM) occupies it
for the RTL's cycle count and a colliding access from the other CPU
waits. Keep it behind an env knob `BLASTEM_BUS_ARB=1` (default off) so
upstream behaviour is unchanged. Verify: with the line rom, the 68K push
span (WRAM stamps 0xFFA0AA/0xFFA0AC, V counter) should lengthen when the
master polls; take the same-picture proof; report push lines/word vs
the hardware 0.28. Commit and push the fork; BLASTEM.md section 11.

## Item 2 (third): ares-style trace hooks in the fork

`--trace-comm file.csv` (frame,source,comm,value; source m68k/shm/shs),
`--trace-dreq` (frame,event,source,value: start/push/miss/read/end),
`--trace-flip` (frame,event,source,select,vcounter,deferred: write/flip
rows) with ares-headless's exact columns (its `--help` text is the
spec; the fork's `blastem.c` `case '-'` is where `--dump` is parsed,
`genesis.c run_dumps` flushes at exit). Verify by diffing one span
against ares's own trace on bprof3.32x. Commit, push, BLASTEM.md 12.
