/* md_palette.inc: part of md_main.c, included there.
 * Colour and staging on the 68K: the queued palette drain, CRAM writes, the pad
 * mapping, and the staged VDP playback (md_stage_play). */
#if defined(R60) && defined(NT_WRAP) && defined(PAL_FIRST)
/* Queued palettes live in WRAM, so they can drain at the next safe blank even
 * while FM belongs to the SH-2 or no fresh packet arrives. Both shims use the
 * same gate and DMA sequence. */
static void r60_palette_drain(void)
{
        if (r60_palette_pending) {
            uint16_t pv = (uint16_t)(*(volatile uint16_t*)0xC00008 >> 8);
            if (pv >= 0xE0u && pv < 0xF8u) {
                uint32_t src = ((uint32_t)r60_palette_hold) >> 1;
                *(volatile uint16_t*)VDP_CTRL_PORT = 0x8F02;
                *(volatile uint16_t*)VDP_CTRL_PORT = 0x9340;
                *(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
                *(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
                *(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
                *(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
                *vdp_ctrl_wide = ((uint32_t)0xC000u << 16) | 0x80u;
                r60_palette_pending = 0u;
            }
        }
}
void r60_palette_reset(void) { r60_palette_pending = 0u; }

#endif

__attribute__((section(".data")))
static void vdp_color(uint16_t index, uint16_t color) {
	index <<= 1;
	*vdp_ctrl_wide = ((0xC000 + (((uint32_t)index) & 0x3FFF)) << 16) + (((uint32_t)index) >> 14);
	*vdp_data_port = color;
}

__attribute__((section(".data")))
static uint8_t md_to_arcade(uint16_t p) {
	// read_joypad: 0 0 0 1 M X Y Z S A C B R L D U (active high)
	// arcade Pn (active low): b0 BTN3 b1 BTN1 b2 BTN2 b4 DOWN b5 UP b6 RIGHT b7 LEFT
	// altbeast: BTN1 punch, BTN2 kick, BTN3 jump -> MD A punch, B kick, C jump
	uint8_t a = 0;
	if (p & 0x0001) a |= 0x20;  // up
	if (p & 0x0002) a |= 0x10;  // down
	if (p & 0x0004) a |= 0x80;  // left
	if (p & 0x0008) a |= 0x40;  // right
	if (p & 0x0040) a |= 0x02;  // A -> punch
	if (p & 0x0010) a |= 0x04;  // B -> kick
	if (p & 0x0020) a |= 0x01;  // C -> jump
	return (uint8_t)~a;
}

/* LOOP 13 part 3 — STAGED VDP PLAYBACK. The receiver's port writes
 * measured V=0x0B..0x35: active display lines 11-53, the VDP fetching
 * nametables mid-rewrite = the tick dashes. The receiver now STAGES
 * every VRAM/CRAM write into WRAM (0xFFA400: [0] record count,
 * [1] vsB [2] vsA [3] hscroll addr|0 [4] hscroll val [5] flags bit0 =
 * scroll valid; records from [8]: wlen, ctrl_hi, ctrl_lo, data...)
 * and THIS plays it back at the top of the next vint, inside vblank,
 * via 68K->VDP DMA (~205 words/line; ~1050 words + ~50 record setups
 * ≈ 12 lines). Runs AFTER the window post so the SH-2's V-gate is
 * not starved; the 68K is halted during transfers, which only delays
 * the ack-spin entry. Reg 1 already carries DMA enable (md_start.s
 * 0x54). Cost: all MD plane data lands one window late, uniformly. */
__attribute__((section(".data")))
static void md_stage_play(void) {
	volatile uint16_t *stg = (volatile uint16_t*)0xFFA400;
	uint16_t n = stg[0];
	if (!n && !(stg[5] & 1))
		return;
	if (stg[5] & 1) {
		*vdp_ctrl_wide = ((uint32_t)(0x4000u | 2u) << 16) | 0x10u;
		*vdp_data_port = stg[1];              /* VSRAM 2 = plane B vy */
		*vdp_ctrl_wide = ((uint32_t)0x4000u << 16) | 0x10u;
		*vdp_data_port = stg[2];              /* VSRAM 0 = plane A vy */
		if (stg[3]) {
			*vdp_ctrl_wide = ((uint32_t)(0x4000u | stg[3]) << 16) | 3u;
			*vdp_data_port = stg[4];
		}
	}
	/* PLANE-A WIPE RECHECK (LOOP 13): the immediate post-DMA readback
	 * matched 37k times on ares while NT A audited all-zero at every
	 * freeze — so either something zeroes the plane MID-FRAME (game
	 * running), or the immediate readback is fooled. Re-read the
	 * PREVIOUS playback's first NT-A cell now, at vint top, before
	 * this vint's records touch the VDP: a mismatch HERE brackets the
	 * wipe to the frame in between.
	 *   0xFFA02C prev addr(A13:0)|0x8000 valid   0xFFA02E prev value
	 *   0xFFA030 recheck mismatches  0xFFA032 last recheck value
	 *   0xFFA034 rechecks performed */
	{
		uint16_t pa = *(volatile uint16_t*)0xFFA02C;
		if (pa & 0x8000) {
			*vdp_ctrl_wide = ((uint32_t)(pa & 0x3FFF) << 16) | 3u;
			uint16_t rc = *vdp_data_port;
			*(volatile uint16_t*)0xFFA032 = rc;
			(*(volatile uint16_t*)0xFFA034)++;
			if (rc != *(volatile uint16_t*)0xFFA02E)
				(*(volatile uint16_t*)0xFFA030)++;
		}
	}
	{
		const uint16_t *p = (const uint16_t*)(stg + 8);
		uint16_t did_rb = 0;
		for (uint16_t r = 0; r < n; r++) {
			uint16_t wl = p[0];
			uint32_t src = ((uint32_t)(p + 3)) >> 1;
			*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9300 | (wl & 0xFF));
			*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9400 | ((wl >> 8) & 0xFF));
			*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
			*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
			*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
			*vdp_ctrl_wide = ((uint32_t)p[1] << 16) | p[2];  /* CD5 fires DMA */
			/* LOOP 13 part 4, PLANE-A HUNT. ares bs9: FG cell records
			 * stage correctly to NT A (0x4C00_0083 in the dead buffer)
			 * yet NT A stays virgin-zero and the FG content appears
			 * 0x2000 higher, in NT B — while MAME lands everything
			 * where addressed. Split receive/staging (exonerated) from
			 * DMA playback ON ares: count cell records played per
			 * plane, and read back the first NT-A cell just written.
			 *   0xFFA024 cell recs -> NT A   0xFFA026 -> NT B
			 *   0xFFA028 last NT-A readback  0xFFA02A mismatches
			 * Readback-zero = the VDP write itself is lost/redirected
			 * at playback; readback-match = VRAM had it and something
			 * later wipes it (savestate told the truth either way). */
			/* (A tile-record readback probe lived here during the
			 * Presentation 2.0 hunt — 0xFFA038/3A/3C — and proved the
			 * staged-DMA upload path loss-free: 4009 records, 0
			 * mismatches. Removed from the play build; re-add from the
			 * LOOP13 entry if the MD-plane lane needs it again.) */
			if (p[2] == 0x83) {
				if (p[1] & 0x2000)
					(*(volatile uint16_t*)0xFFA026)++;
				else {
					(*(volatile uint16_t*)0xFFA024)++;
					if (!did_rb) {
						did_rb = 1;
						uint32_t a = (uint32_t)(p[1] & 0x3FFF);
						*vdp_ctrl_wide = (a << 16) | 3u;   /* VRAM read */
						uint16_t rb = *vdp_data_port;
						*(volatile uint16_t*)0xFFA028 = rb;
						if (rb != p[3])
							(*(volatile uint16_t*)0xFFA02A)++;
						/* arm the next-vint wipe recheck */
						*(volatile uint16_t*)0xFFA02C =
							(uint16_t)((p[1] & 0x3FFF) | 0x8000);
						*(volatile uint16_t*)0xFFA02E = p[3];
					}
				}
			}
			p += 3 + wl;
		}
	}
	stg[0] = 0;
	stg[3] = 0;
	stg[5] = 0;
}
