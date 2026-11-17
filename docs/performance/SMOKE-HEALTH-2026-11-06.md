# Smoke as a renderer-health benchmark

Mike's indicators are Neff entrance smoke, the consistency of white-wolf red
flashing, and boss entrance/death smoke. He is home again; the office MiSTer is
reachable. Before probing, its selected `rdplay01` matched the accepted local
SHA256 `d7d2f4a395013e171c9ccb002e192473b09cc65645ce81549c12fc803326472f`.
Every rig capture uses automatic restoration of that normal ROM. No configuration
was changed. Evidence: `build/codex-smoke-health/`.

## Separate clock progress from presentation

Diagnostic source `17883736` adds `R60RIGSPLIT=4`. It retains mode3's phase scales,
replaces row219's IRQ time with peak per-pass plot time in the 32-pass window,
and paints four marked 16-bit words in rows188..195:

1. V-int count, DIAG[0].
2. Native game progress clock, `port_gf_count`, derived from the game's blink clock.
3. Calls into `port_play_tick`, DIAG[11]. These alone do not prove game progress.
4. Observed framebuffer bank acknowledgments, DIAG2[11]. These are not unique images.

Each word is repeated on two rows, with a separate 0xA55A marker. Explicit dark
pen253 avoids transparent-zero and stale-bit problems; through254 and white255
remain separate. This costs one probe palette pen and extra overlay writes;
normal builds are untouched. Both clocks and phase bars are painted once per
render pass. If the display freezes completely, frozen words alone cannot tell
whether the simulation also stopped.

RSX70/71, previously unused, hold peak plot time and the prior accumulated plot
time; no persistent BSS is added. `tools/rig_health.py` rejects missing markers,
unexpected colors and disagreements between duplicate rows. Interior samples
tolerate the one-pixel hardware shift. `tools/rig_split.py --health` reads mode4;
row219 is **peak plot in eight-line units**, not IRQ cycles.

Actual C painter ASan/UBSan checks all65,536 words, marker bits, both rows,
padding and guards. Decoder fixtures check native/raw-ares geometry and shifts
-1/0/+1 and reject an injected bad color. A cold ares3,600 run validates the
shown metadata against its DRAM pens and CRAM253/254/255 = 0000/8000/7FFF.
The screenshot's bank is selected from the flip trace **before visible metadata
rows**, not the final dump's selector: frame3599 flips at line225, after those
rows were drawn. Selecting the later bank gives the next picture's counters.
Mode3 preprocessing remains identical to accepted source3112f8df.

## Initial hardware measurement

An ordinary accepted-source mode3 strip collected20 valid windows. A mode4
scripted run then collected75 valid windows from21:12:47 to21:21:46 New York time;
none were rejected. Normal rdplay01 was restored at21:22:15, confirmed by the
fresh launch log and selected path.

Least-squares rates from timestamped screenshots in that instrumented run:

| Counter | Rate/second |
| --- | ---: |
| V-ints | 59.9209 |
| Native game clock | 59.9200 |
| Tick calls | 59.9201 |
| Observed applied-bank acknowledgments | 36.3958 |

Native clock minus V-int count stays between2 and5 at all75 samples; tick calls
minus V-int count stays between3 and6. The native/V-int rate ratio is0.9999855.
This supports a healthy game clock in this run while bank acknowledgments lag.
It does not prove every object/state is correct, nor measure sub-frame jitter.

Across sampled32-pass windows: median pass interval1.863V-ints, largest window
mean2.687. Median plot128scanlines/pass, largest sampled mean208; largest observed
individual plot408scanlines (about26ms against a16.7ms frame budget). That peak
was during ordinary enemies, **not a confirmed Neff smoke frame**. Clear med24 /
max sampled mean64; punch32/104; overlay phase8/8. Window averages are quantized
down in8-line units, and sparse screenshots can miss windows or short effects.

The acknowledgment rate is an instrumented-run measure, not normal-build FPS or
unique-image cadence. Slopes also average changing scenes: residuals are retained
in `hardware-health.json`, not hidden as measurement noise. These results locate
presentation pressure; they do not close white-wolf flash consistency, which
requires consecutive-frame output/video and palette/state comparison.

## Controlled smoke setup and arcade logic duration

The first FORM3-only native setup did not reach the boss within16k frames because
live enemies kept the end lock up. The controlled setup uses the existing matched
MAME/port FORM3, FORM_TICK120, ENDCLEAR and GOD probes. The game performs its own
transformation, boss setup and effects; setup clears ordinary enemies at a lock.
This is an isolated rendering benchmark, not gameplay parity or a player ROM.

A fresh headless MAME16,001 run records the setup with fresh cfg/NVRAM. The port
uses the same input prefix, re-keyed from that new native timeline. A build guard
correctly rejected the full36k tape against a16k corpus. The replacement tape has
an identical prefix through16001 and drops only future events; hashes/equivalence
are recorded. No invalid zero-key events were allowed into the probe.

Selected round-one state durations, measured in the native tick counter:

| State | MAME | Port |
| --- | ---: | ---: |
| Neff entrance,16D7E | 243 | 243 |
| Boss materialization,1794C | 126 | 126 |
| Main death smoke,17B54 | 124 | 124 |
| Death follow-up,17B88 | 120 | 120 |
| Post-death wait,17B9A | 43 | 60 |

Only these state durations are compared. Fight outcomes/positions and the
post-death wait are not claimed equal. Matching the core smoke states supports
presentation cost rather than a longer programmed smoke delay in this setup.

The alpha-renderer probe is `shsmoke03`. The paired `shsmokectl01` disables the
optional sprite preparation, progress handoff, tile batches, source-row planner
and row-copy removal together. It is a **bundle** comparison, not attribution to
one optimization. Both use identical setup, input, sound and instrumentation;
their headless boss-handler transition frames and captured final fields match.
Neither includes the withheld CLEARPLAN experiment.

One hardware capture per renderer (25 validated samples over approximately
185 seconds each) is complete. Alpha: applied-bank acknowledgments36.24/s,
median pass1.710 VInts, peak sampled mean PLOT288lines, peak individual PLOT440.
Control bundle: acknowledgments35.32/s, median pass1.740 VInts, peak sampled mean
PLOT712lines, peak individual PLOT1256. Native progress/VInt is0.999988 vs1.000021.
No timing rows saturated. Evidence hardware-smoke-alpha.json and
hardware-smoke-control.json in build/codex-smoke-health/.

These are single runs with sparse screenshots, not event-matched maxima or a
repeatable hardware improvement. In particular, do not turn the two maxima into
a percentage win: capture windows/poses differ. The alpha peak screenshot is a
round-clear/crystal scene, not confirmed main Neff entrance. The observed whole-
run acknowledgment difference is only about2.6%; repeat controls and match
individual effects before attributing a gain. The accepted normal rdplay01 was
restored at21:50:27 local and the active path/log subsequently verified.

## Bounded experiment: pre-scaled poses (c3e0371f)

Native zoomed reference:107 saved smoke snapshots,1,776,015 visible sprite pixels,
zero RGB mismatches against MAME. This validates the decoder, not port pixels or
cadence.8192 synthetic frames independently test all1024 horizontal/vertical zoom
pairs with flips, signed pitch, physical banks and address wrapping.

Optional R60ZOOMBAKE reserves at most48KiB at the prefix of existing baked-art
window2. The remaining unscaled atlas is re-ranked within the reduced window;
there is no expanded ROM fence or new mapper protocol. The full census produces
42 of1034 observed scaled keys in49140bytes, including12 of19 observed smoke
keys. Missing poses, left clipping and right clipping fall back to the scaler.
The new packet stores exact pixel extents rather than source-word extents; raw
pens remain palette independent and zero remains transparent. It bypasses the
unscaled prepared-pose cache. Default OFF; no alpha promotion or performance claim.

Final quiet native7544 fixture (zbref01 vs zbnative02) exercises all15 non-unity
smoke scales, mixed alignments and right-edge fallback. Zero of71,680 shown pens
differ; zero of343,845 complete screenshot pixels differ. Against native MAME:
18,458 visible sprite pixels,842 FG-covered and124 text-covered; zero missing,
extra, wrong-pen/palette, leaks or wrong-through-pen errors on both sides.
The final fixture counters prove atlas use (see evidence). Independently counted
native drawable rows total959; the completed959-entry span lists match byte for
byte. Fixed full clearing bypasses normal temporal clearing; no ghost/cadence
proof follows from this fixture alone.

Final live zbsmoke04 runs16,001 forced smoke-benchmark frames:15,986 VInts,
15,992 tick calls,12,457 flip publications,zero timeouts. Every captured boss
handler/level/round field matches shsmoke03 across those samples; final captured
mirror fields match. This is selected-field progression, not full-state or pixel
parity. Neither live probe is a player ROM. zbsmoke02 and03 failed the hot-code
fence; do not use them. The final04 copy loop fits with four bytes to spare.
Extra atlas counters are diagnostic-only R60ZOOMBAKESTATS and off for timing.

With the new flag off, normal accepted SH C preprocessing tokens match17883736
and assembled bkcopy bytes match exactly (692bytes). Existing unscaled bake
packets are unchanged. The native zoom reference tool is explicitly selected by
--scaled-reference; the existing unscaled comparison remains the default.

A repeat hardware shsmoke03 control yielded25 valid samples,36.09 acknowledgments
per second (prior36.24), median pass1.466 VInts (prior1.710) and sampled peak
PLOT472lines (prior440). This variability is why sparse maxima are not treated
as event-matched speed proof. The live atlas run zbsmoke04 produced25 valid samples/zero rejections, native
progress/VInt0.999994 and35.85 applied acknowledgments/s. Control launches were
36.24 and36.09/s. No overall gain is established; do not promote this atlas to
the alpha. Sparse samples are not exact effect matches.

### Fixed-scene hardware comparison

Cold ares checks zfcontrol01/zfatlas01 confirm the fixed native7544 table in
live RAM and both snapshots, fixture-active flag, valid metadata, and identical
60,160 RGB pixels above the overlay. MiSTer runs were control/atlas/control,
seven valid screenshots per launch, zero rejected or saturated rows:

| Probe | Median PLOT lines/pass | Median VInts/pass |
| --- | ---: | ---: |
| zfcontrol01-a | 1056 | 6.473 |
| zfatlas01-a | 768 | 5.679 |
| zfcontrol01-b | 1056 | 6.443 |

All21 hardware scene images match pixel by pixel above row188: zero RGB
differences. This is a machine comparison, not a thumbnail verdict or a native
pen-ID proof on hardware. Native PENID checks remain the separate ares proof.
The atlas launch observes about27% less plot time at this pose. Two controls
repeat the baseline; only one atlas launch was measured. This does not satisfy
three launches per line for a performance promotion (tools/rig_bars.py method).

These fixtures intentionally freeze native gameplay. Its displayed progress
clock stays constant within each run (2686/2685/2687 respectively; boot offsets
differ), while tick calls and display acknowledgments continue. This is expected
fixture behavior, not a gameplay lock. Both probes retain full fixture clearing,
quiet audio and **repeated plane-input unpack/copy every tick**. That extra master
load and bus contention can affect the measured slave cost; absolute timing and
the percentage cannot be transferred to normal gameplay. This is evidence for
continuing the scaled-art approach, not a general frame-rate win.

Final capture restored rdplay01 at22:35:01 local. Active path, fresh launch log
and remote SHA256 were checked after restoration; exact accepted hash matches.
Evidence zoom-fixed-hardware-comparison.json, hardware-fixed-*.json,
zoom-live-hardware-comparison.json and zoom-roms.json.

### Next axis

The raw atlas takes49140bytes from the unscaled pool and does not accelerate
clipped scaled rows. Preserve the native pixel/span gates while reducing that
storage tax and examining clipped cases. A packed4bpp version of the same chosen
poses is estimated at28280bytes including row alignment/index, versus49140raw;
this is a storage estimate only, not an implemented consumer or speed result.
Normal-game effect timing and three fresh launches per line remain required
before promotion. White-wolf flash cadence is still open; a healthy game clock
does not independently prove its visible cadence.



