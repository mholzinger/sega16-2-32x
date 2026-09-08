# LOOP 27 — DISCOVERY-TIMING (Ghidra + RTL + manual)

Protocol and resources: `docs/handoff/HANDOFF-DISCOVERY.md`. One entry
per iteration; every entry ends with a number and a verdict. Negative
results stay.

## 0. Setup and first census (2026-09-07)

- Ghidra 11.2.1 headless imports and auto-analyses `prog68k.bin`
  (68000:BE:32:default, base 0) in ~1 min; `tools/ghidra_run.sh census`
  runs `tools/ghidra/timing_census.py` against the saved project in
  ~20 s. Jython 2: keep scripts ASCII, take arguments from
  `getScriptArgs()`; the stock `ExportFunctionInfoScript` fails headless
  (askFile).
- Census: 234 functions, 272 backward branches, 92 static hardware-range
  instructions (text RAM 25w/2r, tile RAM 23w, palette 22w, sprite RAM
  5w, I/O 13r/2w). 36 tight loops (<= 40 B) touch hardware; the bulk
  are `dbf` block fills/copies into tile/text/palette RAM (the scene
  loads), plus the I/O poll at 0x3028 and the text-RAM handshake family
  at 0x1500-0x15B2. Vectors: reset 0x400, level-4 autovector 0x404, all
  other levels 0x40C (rte stub).
- RTL facts banked (jtcores, cited in the handoff): 68000 at
  50.3496 MHz x 29/146 = 10.001 MHz; DTACK wait cycles programmed per
  region by the game's own mapper table (`jts16b_mapper.v:284-292`):
  tile+text RAM 3, sprite RAM 1, everything else 0. The DTACK module
  (`jtframe_68kdtack_cen.v`) is in the jtframe submodule, not checked
  out — fetch before converting to 68K clocks.
- Manual: 22-page kit installation manual (DIP tables, test-mode menu,
  harness pinout, 15.75 kHz/60 Hz monitor). No CPU timing. Its test
  mode (ROM / Fix RAM / Scroll RAM / Color RAM / Scratch RAM tests) is a
  self-test oracle to run on the port.
- MiSTer: Nuked-MD `m68kcpu` netlist 68000 + `md_board.v` present; no
  HDL simulator installed.

Verdict: tooling ready. Next entry = question 1, the write-tax census.

## NEGATIVE RESULTS

(none yet)
