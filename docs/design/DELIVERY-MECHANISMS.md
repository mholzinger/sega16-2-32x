# FRAME DELIVERY — the atomic mechanisms, and a test for each

Mike, 2026-09-19: *"Lets break them all down into tiny parts. What
mechanisms are we testing when we build iteration?"*

Written because four slim-pipeline builds failed in one evening — black,
black, black, red — each on a different hypothesis. Every ISOLATED probe
with a control answered first time; everything assembled without one
failed. The difference was not effort, it was scope.

## The mechanisms

| # | mechanism | side | how to test it alone |
|---|---|---|---|
| 1 | dirty-slot scan | SH-2 | count slots visited vs slots dirty |
| 2 | slot to tile code (md_tag) | SH-2 | dump tags, compare to the name table |
| 3 | code to art bytes (bake vs convert) | SH-2 | re-emit through the tool, byte-compare |
| 4 | record write into the packet | SH-2 | dump the packet, decode by stride |
| 5 | packet publish (typ, cnt, first) | SH-2 | read the header back |
| 6 | packet visible to the 68K (it READS the FB window at 0x851A00/0x85E800 at FM=0; no DREQ carries tiles) | both | compare 68K-side bytes to FB |
| 7 | header read | 68K | echo typ/cnt to WRAM |
| 8 | record walk at the right stride | 68K | echo each decoded slot |
| 9 | record to VRAM address | 68K | echo computed addresses |
| 10 | record to source address | 68K | echo computed cart offsets |
| 11 | source read | 68K | read a KNOWN constant, compare |
| 12 | VDP address set | 68K | write, read back |
| 13 | byte move (DMA vs port) | 68K | deliver a known sample, read back |
| 14 | state restore (bank, autoinc, latch) | 68K | run it, then check an unrelated write still lands |
| 15 | side-channel riding the packet (hscroll, VSRAM) | 68K | check the scroll value after |

## Where this evening's bugs actually lived

- **#4 and #8** — the second emit site advanced `o` by 17 words per
  2-word record. The packet was structurally corrupt from record one.
- **#14** — cart reads interleaved between the VDP address write and the
  data writes. Turned the screen from black to red, so it was real and
  was not the whole story.
- **#15** — an `else if (0)` splice removed the FB branch tail, which
  carried the **hscroll table write**. Under slim the planes never got a
  scroll value. *This mechanism has nothing to do with tile delivery.*
  No amount of testing 1-14 would have found it.

## The rule this produces

**A delivery change must name which mechanisms it touches, and each one
gets a test before they are assembled.** #15 is the reason the list must
include things that merely SHARE the packet, not just the things that
deliver tiles.

## Harness status

`make ... DELIVTEST=n` covers #11, #12, #13 and #14 today: it delivers a
known 16-word sample to free VRAM 0xF800, reads it back, and paints a
STICKY verdict (green pass / red wrong bytes / blue nothing) into CRAM
0-31 for the rig, plus WRAM 0xFFB350 for a deterministic local dump.

Methods: 0 DMA-from-WRAM (**the control - must pass**), 1 port writes
immediate, 2 port writes from cart via the bank window, 3 same via the
identity map, 4 DMA sourced from cart (**known broken - must fail, or
the harness cannot detect failure and no pass from it means anything**).

Validated in ares: 0 GREEN, 1 GREEN, 4 BLUE - control passes and the
known-bad is caught. 2 and 3 currently write **no verdict at all**,
which means the path does not reach the paint - itself a finding, and
the next thing to isolate.

**The rig has no fast capture** (screenshots ~1 per 6.8 s, /dev/fb0 is
the OSD layer), so every rig verdict must be STICKY and STABLE. A
per-frame value cannot be read.


## Added 2026-09-20, after the slim bisect

| # | mechanism | side | how to test it alone |
|---|---|---|---|
| 16 | WRAM layout: the vint's stack vs the thunk page | 68K | MAME watchpoint on the table's last words; deepest boot SP vs table end |
| 17 | bound on a computed bus address | 68K | count records outside the blob; a stray one locks silicon, not emulators |
| 18 | record format agreement, INCLUDING the fallthrough path | both | dump the first stagings' header and first words; pixel nibbles where records belong = a second format |

Where the slim failures actually lived: #16 (every slim build until the
stack moved), #18 (the converter fallthrough emitted 17-word pixel
records under the 2-word flag) and #17 (those pixels, read as block
numbers, sent the 68K into the VDP/PSG mirrors on the FPGA).
