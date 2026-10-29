# LINE DELTA: bldS (2026-09-14) -> slim31 (2026-09-21), the new line

Mike, 2026-09-21: "THIS is the new line! ... help me validate this is a
far architecture change FROM bldS." Facts only; each has its source.

## Provenance

    bldS     commit 0d5494b0, built 2026-09-14 12:45 (stamp f69b0414+)
             make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1
                  PGSKIPPKT=1 TEXTCAPMASTER=1 TEXTCAPFULL=1 GAMEGATE=1
                  GAMEGATEWAIT=1 TEXTCAPEARLY=1 TAGKEEP=1 PENHOLD=1
                  PENREPAINT=1 NBUILD1=1 MDSPRTOP=1
             Mike's play pass 2026-09-14.
    slim31   commit 6bd04082, `make line` (LINE_FLAGS: the bldS set plus
             C1NOFB C1PUNCH C1STAMP CAT1MD GLOWPAGE MDBATCH=24 MDROUND
             MDSTATE MDSREFUSE SETCOLS SCANMEMO SPRLIST SPRRUN32
             TEXTCAPMASK TEXTMASKPKT TILESMD TILESLIM SLIMCAP=40).
             Mike's play pass 2026-09-21.

    between them   239 commits, 68 files, +18,150 / -152 lines
                   sh_src/m_main.c +1252, md_src/md_main.c +865,
                   Makefile +438; the rest is the record.
    rom bytes      1,855,306 of 4,194,304 differ. Regions 0x040000-0x2FFFFF
                   (the SH-2 image, the RAM code, the baked tile blob at
                   0x268000) are almost entirely new; 0x000000-0x03FFFF
                   (the patched game body) and 0x300000-0x3FFFFF (the
                   game's high copy, sprbake) are byte-identical but for
                   the stamp.

## What moved architecturally (bldS -> slim31)

1. **Tile delivery: three copies to two, and the SH-2 out of the payload.**
   bldS: the SH-2 converts every tile at run time (64 ROM reads, 64 pen
   lookups, 32 writes) into a 17-word record in the FB packet; the 68K
   DMAs it FB -> VRAM. Cart -> SH-2 -> FB -> VRAM.
   slim31: tiles are BAKED into a cart blob (TILESMD, tilesmd4, Mike
   2026-09-18); for a baked set the SH-2 emits a 2-word record and the
   68K fetches the art from cart after the game's IRQ4 and DMAs it from
   WRAM (TILESLIM, slim21, Mike 2026-09-20). Cart -> 68K -> VRAM. Unbaked
   sets ride inline 17-word records. Census (ares, level 1): 841 of 910
   gameplay tiles baked (92%); the attract is unbaked by design (round
   unpublished). docs/design/SLIM-PIPELINE.md.
2. **Priority (cat-1) per pixel from the art.** bldS punched whole cells
   on any page the bake did not cover; slim31 computes per-pixel masks at
   run time for unbaked pages -- the transformation cutscene's page 10
   (all 800 cells priority, MAME census). Mike: transform fixed.
3. **Text writers marked for the capture mask.** The Zeus typewriter
   (0x56E8) was gated but never marked; slim31 marks all rows. Mike: Zeus
   text solved. The high-score writer's gates exist (HSGATE=1) and wait.
4. **BG palette: first 16 publishes forced.** The change detector compared
   against the FB slot's previous content; the FB survives a warm
   relaunch on the rig.
5. **Boot correctness.** The 68K boot stack no longer overlaps the
   FM-gate thunk table (0xFFBFF0 -> 0xFF3FF0, md.ld guard); the tile blob
   sits at 0x268000 behind an ld assert. Both were latent in bldS's
   layout and bit every larger build.
6. **Sky fix**: colour index 0 harvested and packed (tilesmd4).

## What did NOT change (the parts of bldS that carried over)

The FBX transport and its ISR lift, the GAMEGATE frame release, both
scroll planes on the MD VDP (MDBGALL), the MD sprite offload, the
window/flip protocol, the patched game body (byte-identical).

## What the numbers say

    ares, level-1 play script, 1200 frames   bldS 720   slim31 716   (scene timer: same pace)
    rig flips per 64 vints, attract demo     slim31 ~28 (FB route on the same base ~29)
    bldS's rig flip rate                     not measured this session
    scene-anchored frame tests bldS vs slim31   no regression (tools/frametest: tiles/vint 4.93 = 4.93; level1_early colours 74 vs 72, hot_glyphs 76 = 76; level1_mid 65 = 65, 73 = 73; black_cells 0 both)

## What is still open on the new line

The FPGA level-start race (LESSONS 2026-09-20/21): the line wins it
about two launches in three; every build that adds a gate site loses it.
The high-score gates and the Zeus store gate wait on it. Black tile
pop-in on level 1 only (Mike: none on levels 2-3). docs/design/HOTPATH.md
keeps the list of what has worked and how it was verified.
