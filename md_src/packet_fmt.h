/* DREQ PACKET FORMAT — SINGLE SOURCE OF TRUTH (LOOP 22).
 *
 * The format has FOUR consumers: the MD builder (md_main.c push
 * loops), the MD published length (0xA15110), the SH-2 landed-length
 * whitelist, and the SH-2 apply offsets. The PAL32 whitelist bug
 * (S2_pal32_flick.bs9: every palette packet rejected whole on ares,
 * invisible in MAME where landed always reads 0) happened because
 * three of the four were updated by hand and the fourth was not.
 * These constants are included by BOTH CPUs' sources; a shape change
 * here recompiles every consumer, and the asserts below pin the
 * arithmetic the DMA protocol relies on.
 *
 * k2 (text-window) packet, FBTEXT + PAL32 era:
 *   words 0..79   prefix (junk under FBTEXT; layout kept)
 *   word  80      tilemap dirty bitmap
 *   word  81      text base | (K << PKT_PAL_KSHIFT) | 0x8000 when K>0
 *   words 82..85  K<=4 dirty-block ids (unused = 0xFFFF)   [K>0 only]
 *   then K * 32   block payload
 *   last 2        magic tail 0xA55A 0x5AA5
 */
#ifndef PACKET_FMT_H
#define PACKET_FMT_H

#if defined(PKT_SLIM) && !defined(PAL32)
#error PKT_SLIM requires PAL32 (and the FBSPR+FBTEXT era it implies)
#endif

/* PKT_SLIM (LOOP 22): under FBSPR + FBTEXT + PAL32, prefix words 0..79
 * are provably dead — regs ride the text capture, sprite records ride
 * FB staging, text chunks are gone — so the prefix shrinks to the two
 * live words (tilemap bitmap + tag) and the k1 junk record goes with
 * it. ~168 junk words/cycle off the 68K's FIFO feed. */
#ifdef PKT_SLIM
#define PKT_PREFIX     2u                 /* bitmap + tag words only */
#define PKT_K1_LEN     (PKT_PREFIX + PKT_TAIL)              /* 4 */
#else
#define PKT_PREFIX     82u                /* incl. bitmap + tag words */
#endif
#define PKT_W_BM       (PKT_PREFIX - 2u)  /* tilemap dirty bitmap word */
#define PKT_W_TAG      (PKT_PREFIX - 1u)  /* text base / pal tag word */
#define PKT_TAIL       2u
#define PKT_K2_BARE    (PKT_PREFIX + PKT_TAIL)              /* 84 / 4 */

#define PKT_PAL_IDS    4u                 /* id words, always 4 when K>0 */
#define PKT_PAL_BLK    32u                /* words per dirty block */
#define PKT_PAL_KMAX   4u
#define PKT_PAL_KSHIFT 11                 /* K field in the tag word */
#define PKT_PAL_ID0    PKT_PREFIX                            /* 82 */
#define PKT_PAL_PAY0   (PKT_PREFIX + PKT_PAL_IDS)            /* 86 */
#define PKT_K2_LEN(K)  (PKT_PREFIX + PKT_PAL_IDS \
                        + (unsigned)(K) * PKT_PAL_BLK + PKT_TAIL)

/* the FIFO drains in 4-word bursts: a non-multiple-of-4 length leaves
 * the tail un-drained and TE never sets (see dreq_rearm) */
_Static_assert((PKT_K2_BARE & 3u) == 0, "bare k2 length not burst-aligned");
_Static_assert((PKT_K2_LEN(1) & 3u) == 0 && (PKT_K2_LEN(PKT_PAL_KMAX) & 3u) == 0
               && (PKT_PAL_BLK & 3u) == 0, "pal k2 lengths not burst-aligned");
/* pin the concrete family so silent drift in any constant screams */
#ifdef PKT_SLIM
_Static_assert(PKT_K1_LEN == 4 && PKT_K2_BARE == 4
               && PKT_K2_LEN(1) == 40 && PKT_K2_LEN(2) == 72
               && PKT_K2_LEN(3) == 104 && PKT_K2_LEN(4) == 136,
               "slim packet family changed - update EVERY consumer");
#else
_Static_assert(PKT_K2_LEN(1) == 120 && PKT_K2_LEN(2) == 152
               && PKT_K2_LEN(3) == 184 && PKT_K2_LEN(4) == 216,
               "k2 packet family changed - update EVERY consumer");
#endif
/* K must fit its 3-bit tag field */
_Static_assert(PKT_PAL_KMAX <= 7, "K overflows the tag field");

#ifdef K2_FREE
/* LOOP 24 K2FREE — MIXED FAMILIES BY K. k1 = the non-slim SPR_TRUNC
 * family (84 + 8n: the 82-word prefix carries live regs/rowscroll via
 * the patch_game mirror split, then truncated records). k2 = the SLIM
 * k2 family (bitmap+tag+ids+blocks+tail): pushing the 80 prefix words
 * again at k2 bought regs at 60Hz for ~7 lines/cycle of 68K feed —
 * 30Hz (k1-only) is today's rate and free. Constants mirror the
 * PKT_SLIM family exactly (same proven lengths 4/40/72/104/136). */
#define K2F_W_BM       0u
#define K2F_W_TAG      1u
#define K2F_PAL_ID0    2u
/* LOOP 25: the PALSTORM census (attract, 2539 pal vints) killed the
 * LOOP22 sizing — max backlog 64 (the WHOLE palette dirty at once,
 * both halves), mean dirty rate 4.0/vint against a 4-block channel =
 * saturation with zero burst headroom, 16.9% of pal vints leaving
 * CRAM torn across generations (the black smoke / inverted-flash /
 * stale-grass family). K2FREE's family widens to 8 id slots and
 * KMAX=7 (the 3-bit tag field's ceiling): drain 1.75x, a full storm
 * clears in ~9 cycles instead of 16. If Mike's eyes still catch the
 * tear, the next step is the STORM FLUSH (whole-mirror sync through
 * FB scratch, one cycle) — see docs/log/LOOP25.md. */
#define K2F_PAL_IDS    4u
#define K2F_PAL_KMAX   4u
/* (KMAX 7 field-tested 2026-08-21 and REVERTED same night: the wide
 * push through the DREQ FIFO during the master's busy span lost
 * words wholesale — misaligned x4, Mike: borderline unplayable. The
 * storm drains via the FB flush instead; the census above still
 * stands as the requirement.) */
#define K2F_PAL_PAY0   (2u + K2F_PAL_IDS)
#define K2F_K2_BARE    4u
#define K2F_K2_LEN(K)  (2u + K2F_PAL_IDS + (unsigned)(K) * PKT_PAL_BLK \
                        + PKT_TAIL)
#define K2F_K2_OK(landed) \
    ((landed) == K2F_K2_BARE \
     || ((landed) >= K2F_K2_LEN(1) && (landed) <= K2F_K2_LEN(K2F_PAL_KMAX) \
         && (((landed) - K2F_K2_LEN(0)) % PKT_PAL_BLK) == 0))
_Static_assert(K2F_K2_BARE == 4 && K2F_K2_LEN(1) == 40
               && K2F_K2_LEN(4) == 136, "K2F k2 family drifted");
_Static_assert((K2F_K2_LEN(1) & 3u) == 0 && (K2F_K2_LEN(4) & 3u) == 0,
               "K2F pal lengths not burst-aligned");
_Static_assert(K2F_PAL_KMAX <= 7, "K overflows the 3-bit tag field");
#endif

#ifdef R60
/* REBUILD (docs/design/REBUILD.md P2) — THE ONE PACKET. One vint = one frame =
 * one push, 68K -> SH-2, built at the 68K's vint entry and landed
 * against an IDLE master (the master waits for it before its FM
 * span — the LOOP25 FIFO verdict inverted into a design rule).
 * Layout v2 (words) — header-first so the tag names the layout,
 * rowscroll OPTIONAL (60 words = ~14 lines of 68K push cost saved
 * every frame the game doesn't row-effect, which is most of them):
 *   0..19   layer regs        (0xFF8000+0x740 mirror)
 *   20      tilemap dirty bitmap
 *   21      tag: bit15 pal-present, bits14..11 K (0..15),
 *           bit10 rowscroll-present, bits9..0 EXACT packet length
 *           in words (max 924 fits; harvest requires landed ==
 *           this — an interior FIFO drop of a multiple of 8 words
 *           otherwise validates on magic+arithmetic alone and
 *           applies a shifted payload: the blue-white wedge)
 *   22..81  rowscroll         (0xFF8000+0x7C0)  [rs-present only]
 *   then    8 pal block ids, byte-packed 2/word  [pal-present only]
 *   then    K * 32 pal block payload             [pal-present only]
 *   then    n * 8 sprite records, terminator inside (SPR_TRUNC)
 *   last 2  magic tail 0xA55A 0x5AA5
 * ids byte-packed hi/lo (0xFF = unused): 8 words address 16 blocks.
 * Records cap 40 (measured live max 21). Max = 22+60+8+512+320+2 =
 * 924 words; arm 936. All lengths 4-word-burst aligned. */
#define R60_W_BM       20u
#define R60_W_TAG      21u
#define R60_HDR        22u                /* words before optional parts */
#define R60_RS_BIT     0x0400u            /* tag: rowscroll present */
#define R60_RS_W       60u
#define R60_PAL_IDW    8u                 /* id WORDS (2 ids each) */
#define R60_PAL_KMAX   16u
#define R60_REC_MAX    40u
#define R60_KSHIFT     11
#define R60_LEN(rs, K, n)  (R60_HDR + ((rs) ? R60_RS_W : 0u)                         + ((K) ? (R60_PAL_IDW                         + (unsigned)(K) * PKT_PAL_BLK) : 0u)                         + (unsigned)(n) * 8u + PKT_TAIL)
_Static_assert(R60_LEN(0, 0, 0) == 24 && (R60_LEN(0, 0, 0) & 3u) == 0,
               "R60 bare length drifted");
_Static_assert(R60_LEN(1, 16, 40) == 924, "R60 max length drifted");
_Static_assert((R60_LEN(1, 1, 1) & 3u) == 0 && (R60_LEN(0, 16, 40) & 3u) == 0
               && (R60_LEN(1, 3, 21) & 3u) == 0,
               "R60 lengths not burst-aligned");
#define R60_ARM        936u
#ifdef PAL_DELTA
/* R60 layout v3 (PALDELTA) — the pal payload ships WORD DELTAS.
 * Only the pal section changes; header/tag/rowscroll/records/tail
 * are v2 verbatim, and the tag's K counts shipped blocks as ever.
 *   len: ONE word before the ids = payload+pad word count, so the
 *        SH-2 finds rec0 with a single read instead of a mask-
 *        popcount pre-parse (the parse loop cost ~200B of SDRAM
 *        code the 0x19000 region guard did not have). It is 68K-
 *        authored but rides the same exact-length gate the tag
 *        does; the harvest bound-checks rec0 <= landed regardless.
 *   ids: 8 words as before, but bit 7 of an id byte = RAW block
 *        (payload is 32 words, the v2 form). Bare ids are DELTA:
 *        payload is 2 mask words (w0 = entries 0..15, bit i = entry
 *        i changed; w1 = entries 16..31) + the changed words in
 *        ascending entry order.
 *   pad: 0-3 zero words after the last pal payload so the pal
 *        section (1 + 8 + payload + pad) stays a multiple of 4
 *        (DREQ burst alignment — every v2 length was %4==0 by
 *        construction; v3 must not regress it).
 *        pad = (-(payload_words + 1)) & 3.
 * The 68K keeps a 2048-word shadow of the palette mirror at
 * PAL_SHADOW; a block's delta is mirror-vs-shadow at pack time, and
 * the shadow updates only for blocks actually selected into the
 * packet. Empty deltas do the ship-twice dirty->retry bookkeeping
 * WITHOUT consuming a K slot. Tears poison the shadow (words marked
 * shipped that never applied), so the BAD1 echo re-marks carried
 * blocks FORCE-RAW; the force mask boots all-set, which reproduces
 * the v2 boot storm exactly. */
#define R60_PAL_RAW    0x80u              /* id bit 7: raw 32-word block */
#define R60_PAL_DMAX   29u                /* delta cap: 2+29 < 32 raw */
#define PAL_SHADOW     0xFF6000           /* 68K shadow, 2048 words */
#endif
/* whitelist: landed must satisfy the family for SOME (K, n) — the
 * tag names K, so the check is arithmetic on (landed - fixed(K)):
 * given K from the tag, (landed - 82 - (K?8+32K:0) - 2) must be a
 * multiple of 8 within 0..320. Checked in code, not a macro. */
#endif /* R60 */

/* SH-2 whitelist term for the PAL32 k2 family (bare or 1..KMAX blocks) */
#define PKT_K2_OK(landed) \
    ((landed) == PKT_K2_BARE \
     || ((landed) >= PKT_K2_LEN(1) && (landed) <= PKT_K2_LEN(PKT_PAL_KMAX) \
         && (((landed) - PKT_K2_LEN(0)) % PKT_PAL_BLK) == 0))

#endif /* PACKET_FMT_H */
