# BUSES AND MESSAGE PASSING — what the port uses, and what each path carries

2026-09-12, decompile thread, from ARCHITECTURE.md, SILICON.md,
HANDOFF-PIPELINE.md, packet_fmt.h and the two mains. Every rate here is
someone's measurement with its source named; nothing is a datasheet.

---------------------------------------------------------------------
## 1. The processors and their memories

    68000 @ 7.67 MHz    127,841 cycles a vint, ~12,400 instructions at our
                        measured 10.08 cycles each (LOOP-DECOMPILE 90)
                        64 KB work RAM (the game's, plus our shim in
                        0xFF0000-0xFFBFFF below the object table)
                        the cart: the game's rebased program, read through
                        the 32X adapter
    master SH-2 @ 23 MHz, slave SH-2 @ 23 MHz
                        256 KB SDRAM shared (code, staging, mailboxes)
                        2 x 128 KB framebuffer banks (DRAM), one displayed,
                        one composed, swapped by the FS bit
                        32X CRAM, 256 entries
    MD VDP              64 KB VRAM (two planes' name tables, 1,024 usable
                        tile slots, the SAT), 64-entry CRAM (four lines
                        of 15 usable pens), fed only by DMA from the
                        68000's bus

The 68000 bus is the only bus every party can reach: the SH-2s see the
cart and the framebuffer; the 68000 sees the cart, its RAM, the VDP
ports and the 32X window at 0x840000-0x85FFFF (the framebuffer, at
FM=0 only: SILICON fact 2) plus the COMM registers. The SH-2s CANNOT
read 68000 work RAM. Everything the game writes has to be copied by the
68000 into something the SH-2s can see.

---------------------------------------------------------------------
## 2. The paths, with measured cost

### 68000 -> SH-2: the packet (r60_push -> r60_blast -> master harvest)

    what     ~150 words a vint: dirty palette blocks (14 words each, the
             delta form 2+n), the rowscroll table when changed (60), the
             sprite records (8 each, capped), the tilemap dirty bitmap,
             the tag word
    route    FBXPORT: staged in WRAM, blasted into the framebuffer at
             FBX_PKT_MD 0x852000 at FM=0 in the vint tail, then a
             publish word 0xB600|seq written LAST (SILICON section 3)
    cost     ~0.05 scanlines a word on hardware; the whole push 57 lines
             on the FB route against 99 on the FIFO (SILICON section 2).
             In 68000 instructions: r60_push 1,672 a vint with R60TIGHT
             (2,512 without), r60_blast ~510, r60_ship_words ~690
    ordering the 68000's own program order is the guarantee; the master
             checks magic and sequence and copies n words into SPR_LAND

### 68000 -> SH-2: the DREQ FIFO (retired for the packet, kept for boot)

    cost     ~2.4 scanlines a WORD on the MiSTer, 3 lines per 20 words
             on ares -- a 48x gap that hid the cost for weeks. 20 words =
             48 lines. Per-access and intrinsic; RTL cause unknown.
    ceiling  ~110 words a vint if the 68000 did nothing else. That is
             why the packet moved to the framebuffer.

### 68000 -> SH-2: the COMM registers (8 x 16 bits, zero-cost reads)

    COMM0    the post: 0x2020 opens the window; cleared by the master
    COMM2    bank shadow, and the text row-group mask in the high byte
    COMM4    master -> slave commands (0xF1xx, 0xA000|vk); boot 0xF103
    COMM6    slave acks (0xB101)
    COMM8    the heal channel: 0xBAxx scene switch, 0xBBxx flip counts,
             BAD1 torn-packet echoes, the glow-mask grant
    COMM10   the 13-bit DIRTY-PAGE mask (0xFFB9FE, set by the tile-RAM
             write thunks; the master ORs it into pg_pending) | round<<13
             (masked to 0x1FFF since 222/226) -- THREE writers, one reader
             at an edge; the race in LOOP29 227. (Corrected 2026-09-13:
             an earlier line here called it the palette-dirty word.)
    COMM12   0xD000 | the late-V stamp
    COMM14   boot handshake (B007/B008), then the vblank count
    Master<->slave signalling moved OFF COMM into SDRAM mailboxes (SYNC)
    because the MD stream owns COMM2-10 whenever the game runs.

### SH-2 -> 68000 -> VDP: tiles, name tables, palette lines, sprites

    what     tile records of 16 words (32 bytes) the master converted to
             MD 4bpp under the baked pen map; name-table words; CRAM
             lines; the SAT (32 entries, 128 words); hscroll
    route    md_consume: the 68000 reads the batch from the framebuffer
             window at FM=0 and DMAs it to VRAM/CRAM inside vblank
    cost     the read is the FB rate again (~14 lines for a 280-word
             chunk, md_main.c); the DMA budget is vblank: 24 tiles a vint
             (768 bytes) ships clean, 40 overruns vblank on hardware and
             tears the top band (LOOP29 214). So the effective ceiling
             on tile art reaching the VDP is ~768 B a vint plus the SAT
             and one CRAM line, and that is what pop-in is.

### The frame: SH-2 compose and the flip

    32X framebuffer write bandwidth   6.76 MB/s measured (ARCHITECTURE 1)
    one 320x224 8bpp pass             71,680 bytes
    budget at 60 Hz                   ~1.6 passes a frame
    the blit                          28.5 lines of 262 to move one pass
    slave compose, per generation     0.59 v sprites (+0.51 cat1 tiles
                                      before fold 1)
    master maps, per generation       0.44 v
    the flip                          FS write inside vblank; a generation
                                      is vint-quantised: 80% take 2 vints
                                      today, 15% take 1 (LOOP29 161)

### The game's own clock: the frame flag

    IRQ4 (H-int at line 223) runs the shim then the game's handler; the
    main loop clears 0xFFF01C and spins. GAMEGATE makes the release
    ours: once per presented frame (GAMEGATEWAIT falls back after N).
    RELBANK (consume instead of clear) measured wall 0.81 and starved
    the transport, which lived in the discarded vint (LOOP29 184).

---------------------------------------------------------------------
## 3. Wholesale, per vint, as it stands

    68000 instructions       ~12,400   game 4,400-9,800 a game frame
                                       shim 3,573 (R60TIGHT) / 4,556
    68000 -> SH-2 packet     ~150 words, ~8 lines of writes, ~57 with
                             the build; FB route, FM=0 tail
    SH-2 -> VDP              ~768 B tile art + SAT + a CRAM line, inside
                             vblank
    SH-2 compose             ~1.6 screen passes of framebuffer bandwidth
                             a frame; today's compose uses 0.59 of a
                             generation on sprites alone
    COMM                     eight words, free, one of them raced

The scarce resources, in order: the vblank DMA window into the VDP
(768 B), the FM=0 window for the packet, the slave's compose time, the
68000's vint. The abundant one is the MD VDP itself, which draws two
planes and the SAT for nothing once the data is in VRAM -- which is
why every fold moves work toward it.

---------------------------------------------------------------------
## 4. The framebuffer as shared memory ("the overdraw"), both directions

A 32X framebuffer bank is 128 KB; the 320x224 image plus its line table
end at 0x11A00. The remaining ~58 KB of each bank is never displayed,
and the port uses it as the only memory both the 68000 and the SH-2s
can write and read. The 68000 reaches it through the 0x840000 window at
FM=0 only; the SH-2s at any time, uncached, at 0x2400xxxx. Three things
live there:

    0x11A00-0x11FC0   md_pkt A     SH-2 -> 68000, tile batch / name chunks
    0x12000-0x1DFFF   the game's tile RAM, pages 0-11, as the patch rebased
                      it (0x400000 -> 0x852000): the game WRITES its
                      tilemaps straight into the framebuffer hole
    0x1E000-0x1E7F8   r60 packet    68000 -> SH-2 (FBXPORT), page 12 first
                      half, publish word 0xB600|seq at 0x1E7F8 LAST
    0x1E800-0x1EDC0   md_pkt B     SH-2 -> 68000, page 12 second half
    0x1EDC0-0x1EE40   MDSPR palette and SAT images the 68000 DMAs

### 68000 -> SH-2: the game's own stores are the message

The patch rebases every tile-RAM store the program makes into the hole,
and thunks at the write sites set a bit in a 16-region dirty word
(COMM10 low 13 bits, packet word 80 for the bitmap). The master's
`cap_page` copies each dirtied page from the hole into an SDRAM truth
copy (TILEMAP_C / TILEMAP_U at 0x06019000), which is what the compose
and the maps drain read. So there is no "send tilemap" step: the game
writes where the SH-2 looks, and the dirty word says which 4 KB to
re-read. The r60 packet (section 2) carries the rest -- palettes,
records, rowscroll -- the same way, published by a magic word written
last.

The price of putting game memory in a double-buffered framebuffer is
the BANK: the 68000's stores land in whichever bank the window maps
at FM=0, and the flip swaps banks. `restore_pages` therefore replays
the truth copy into the NEW bank's staging after every flip, for the
pages dirtied since the previous flip, before the window acks -- so the
game's own read-backs (the collision `tst.w` against tile RAM,
LOOP-DECOMPILE 99; the 1 KB page-1 save/restore at boot) see the same
bytes in either bank. Getting this wrong is "the bank disease": a
stale bank captured as truth, name tables from two generations mixed.

### SH-2 -> 68000: md_pkt A/B, consumed by DMA straight from the hole

The master writes a self-describing packet -- [0] magic 0xB6B6 written
LAST, [1] type|flags (bit 15 palette changed, bit 13 display hold, bits
8-12 the TILE_VERIFY verdicts), [2] param, [3] hscroll, [4]/[6] the two
planes' vscroll, [5] count, then 17-word records (VRAM slot, 16 words
of converted 4bpp tile) or name-table chunks. At vint top, FM=0, inside
vblank, `md_consume` checks the magic, ZEROES it as the consumed mark
(so the master can tell consumed from pending and defers instead of
overwriting), writes VSRAM, then issues one VDP DMA per record with
the FRAMEBUFFER as the DMA source (0x85xxxx) -- the commercial-title
idiom; no 68000 copy loop. A and B alternate with the bank; after a
flip the master replays the last A image into the new bank
(PG_SKIP_PKT) because the 68000 will read that bank next.

Direction summary: the 68000 talks to the SH-2 by writing into the hole
at FM=0 and posting COMM0; the SH-2 talks to the 68000 by writing into
the hole at any time and letting the 68000's next FM=0 vint DMA it out.
COMM registers carry only signals and small state; the payload never
crosses through them.
