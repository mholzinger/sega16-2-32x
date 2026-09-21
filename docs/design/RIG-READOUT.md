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
