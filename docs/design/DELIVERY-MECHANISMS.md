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
| 6 | DREQ ship FB to 68K | both | compare 68K-side bytes to FB |
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
