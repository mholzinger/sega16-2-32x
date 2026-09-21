# RIG READOUT -- reading numbers off the FPGA, and why one byte is not enough

The MiSTer 32X core has no savestate (S32X.sv CONF_STR, checked
2026-09-21) and the cart SRAM save path is not reachable from the 68K
under the adapter's mapping, so a screenshot is the only channel, ~7 s
per capture. What exists:

    SILICON.md 4      CRAM flood: one byte per capture, kills the picture
    SLIMVALUE=1       slim counters through that flood
    BGVALUE=1         CRAM / name-table / tile-art probe
    CRAMPROBE=1       the flood in 64 (or 192) of 512 vints, RESTORED from a
                      WRAM shadow of the last landed BG palette afterwards,
                      so the same run also shows the picture
    CENSUS2=1         the SH-2 posts, per 64 calls, one of three counts
                      (packets published, palette-flagged, stubs) on COMM6,
                      relayed into CRAMPROBE's flood

What CENSUS2 returned on the losing layout (ZEUSPAD=5), 24 captures over
two launches: "packets/64 = 34" every time the flood was caught; the
other two fields never appeared, so either the field rotation on the
SH-2 side is not advancing or the 68K relay latches the first post.
Unresolved; do not read the 34 as more than "the SH-2 is publishing".

## The design that gets 80 bits per capture: a name-table barcode

Instead of a colour, write DATA into the picture: a 68K->VDP DMA each
vint of two name-table rows (plane A rows 24-25, 40 cells each) choosing
between tile 0 (blank) and one solid tile installed once in a spare slot
(VRAM 0xAFE0, 32 bytes of 0x11). Each cell is one bit; two rows carry the
same 40 bits twice so a sprite crossing one row does not corrupt the
read. The 32X layer is transparent over the grass, so the MD row shows.
Decode: sample each cell's centre for bright/dark in the capture. Ten
bytes per capture, picture otherwise intact, no palette touched.

Payload for the race card, per capture: vint count (16), packets
consumed (8), palette-flagged consumed (8), SH-2 packets published (8),
SH-2 palette-flagged (8), SH-2 stubs (8), CRAM entries 16-47 non-zero (6),
BG palette shadow non-zero (6), md_round + scene-installed (8). One run
of twelve captures then tells the whole story of a losing launch.

Cost: a 2-row NT DMA per vint (80 words) and the SH-2 posting its
counters on COMM registers (it already has COMM6 free for this). Build it
under RIGBARCODE=1, verify the decode on a passing launch first.

## What was built (2026-09-21): `make line RIGBARCODE=1`, decode with `tools/rig_barcode.py`

Three things changed from the design above once it met the machine:

1. **Plane A scrolls; the WINDOW plane does not.** The game's FG scroll
   (ares frame 2500: vy 32, hx 169) moved the rows off their cells. The
   68K sets window regs 0x11 = 0, 0x12 = 0x96 (down from row 22) and the
   window table reg 0x03 = 0x30 (plane A's own 0xC000), so name-table
   rows 22-27 draw at screen rows 22-27 unscrolled. Cost: plane A's
   bottom six rows show unscrolled on the probe.
2. **The bottom-right cells are under the text layer** (CREDIT / INSERT
   COIN), row 27 is under the FB's opaque bottom band, and sprites cross
   the rest. Two copies with an 8-bit XOR could not be merged (any
   per-bit merge search is linear over the checksum). So each cell is
   one of FOUR colours — blank, white, red, blue = 2 bits — and one row
   carries all 80 bits; six rows are six votes per bit. Solid tiles at
   VRAM 0xF800/0xF820/0xF840 (pens 15/14/13, pal 3); CRAM 61-63 are
   forced blue/red/white every vint (the BG's third line loses three
   pens on the probe).
3. **The SH-2 counters ride the packet header, not a COMM register.**
   Every COMM register has a user on the line and CENSUS2's relay was
   reading the wrong one (LESSONS 2026-09-21). The publish census
   (`cz_pub`, `cz_pal`, counted where `sc[1] |= 0x8000` is decided) is
   stamped into words 4 and 6 high bytes BY THE ISR AFTER ITS SDRAM->FB
   COPY (a stamp at the publish site is overwritten by that copy). The
   68K masks the low byte before VSRAM (`BC_LO`) and latches the high
   bytes at consume.

Payload (10 bytes, 40 cells): `A5 | vint(16) | pkt consumed | pal-flagged
consumed (0xFF3560) | SH-2 published | SH-2 pal-flagged | [5:0] CRAM
16-47 non-zero, [6] shadow non-zero, [7] a packet magic sat in the FB at
the hook | [2:0] round [3] cut [6:4] attract step [7] play | XOR`.
Counters mod 256.

ares verification (rom/night/barcode5.32x, `--screenshot` at 1500 / 2004 /
2300 / 3004): vint 1483 / 1987 / 2283 / 2985 (boot offset 17), SH-2
published == 68K consumed at every frame (115, 105, 124, 7), palette-
flagged 45 / 45 / 178 / 190 vs consumed-flagged 119 / 119 / 251 / 200 (the
two count different events; not yet reconciled), CRAM 16-47 non-zero 29
on the level scene and 13 on a picture, attract step 3 / 3 / 4 / 5. The
ares crop lands the rows one cell up (r22 in the 320x224 frame); the
decoder searches r22/21/23 and votes.

Rig protocol: three launches, 12 captures at 8 s from 25 s after launch
(`rigcap.sh rom 12 25`), then `tools/rig_barcode.py screenshots/rig_<rom>/*.png`.

Rig results 2026-09-21 (12 captures per launch, three launches each):

    barcode5    (line layout + barcode)   3/3 background; 35/36 decoded
    barcode5p5  (ZEUSPAD=5 + barcode)     3/3 background; 30/36 decoded
                                          (the six NO BARCODE captures are
                                          scene cuts: the rows fade with
                                          the picture)

Every decoded capture, both roms: SH-2 published == 68K consumed (mod
256). On the FPGA every packet lands. The pad-5 layout lost 0/3 without
the barcode (LESSONS 2026-09-20 evening); with it, 3/3 -- the lottery
again. The two palette-flag counters (SH-2 `cz_pal` vs the 68K's
0xFF3560) advance by different amounts per interval and are not the
same event; reconcile before reading either as "flagged packets lost".
No losing launch has been read yet; pad-3 and bset layouts were queued
next.
