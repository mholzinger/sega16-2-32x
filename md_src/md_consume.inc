/* md_consume.inc: part of md_main.c, included there.
 * The packet from the SH-2, consumed in vblank: the echo belt, the two hooks around
 * the frame, and md_consume with its DMA bodies. */
#if defined(FBX_STAGE) && defined(FBX_ECHO)
/* FBX ECHO BELT (2026-09-21, the level-start race). The master's
 * packet header carries the sequence it last lifted. Its ISR lifts
 * BEFORE it flips, and a blast still in progress at that moment leaves
 * the finished packet in the bank that just became the DISPLAYED one:
 * unreadable by either CPU until the next flip, and the master's scene
 * fill erases it first (ares, boot packet 2 of the losing layouts,
 * docs/design/RIG-READOUT.md). So: two vints after a blast, if the echo
 * has not caught up, write the kept packet again, same sequence, into
 * whatever bank is draw now. Runs from partb_hook, every vint, at FM=0.
 * (Its first home, the pre-post slot, is not on the boot-time path.) */
__attribute__((section(".data"), noinline))
static void fbx_echo_belt(void)
{
	if (*(volatile uint16_t*)0xA15100 & 0x8000) return;   /* FM up: a write would be dropped */
	if (fbx_age < 255) fbx_age++;
	if (fbx_rb_age < 255) fbx_rb_age++;
	if (fbx_keep_live && fbx_rb_age >= 2
	    && ((uint8_t)(fbx_seq_pub - fbx_echo) & 15u)) {
		const uint32_t *sp = (const uint32_t*)fbx_stage;
		volatile uint32_t *dp = (volatile uint32_t*)FBX_PKT_MD;
		uint16_t n = fbx_keep_n, nl = (uint16_t)(n >> 1);
		while (nl--) *dp++ = *sp++;
		if (n & 1) ((volatile uint16_t*)FBX_PKT_MD)[n - 1] = fbx_stage[n - 1];
		{
			volatile uint16_t *pub = (volatile uint16_t*)FBX_PUB_MD;
			pub[1] = n;
			pub[0] = (uint16_t)(FBX_MAGIC | fbx_seq_pub);
		}
		fbx_rb_age = 0;                      /* spacing between re-blasts only:
		                                      * fbx_age keeps counting from the
		                                      * ORIGINAL blast, so the re-mark
		                                      * half (age >= 2 at the next build)
		                                      * still fires (ares: it never did,
		                                      * the re-blast reset the age) */
		fbx_reblasts++;
	}
}
#endif
/* PART-B END HOOK: md_start.s calls this at the END of fmgate_partb,
 * after the window post and the FM raise. */
__attribute__((section(".data")))
void partb_end_hook(void)
{
	/* (slim26 landed the art HERE, after the FM raise and the post, and
	 * the FPGA lost the level background -- the slim18-20 shape; ares was
	 * fine. A WRAM->VRAM DMA burst after the raise breaks the frame on
	 * silicon; before the raise it does not. Moved into partb_hook.) */
}
/* PART-B HOOK. md_start.s calls this at the top of fmgate_partb, i.e.
 * after the game's IRQ4 has returned and before FM is raised -- the one
 * place on the line's vint path that runs late in vblank from C. The
 * cart -> VRAM walk lives here because 68K cart reads at the TOP of
 * vblank collapse the FPGA's frame (see slim_fetch / CART_READ_AT). */
__attribute__((section(".data")))
void partb_hook(void)
{
#if defined(FBX_STAGE) && defined(FBX_ECHO)
	fbx_echo_belt();
#endif
	/* THE ONE SPOT THE FPGA ACCEPTS THE DMA: first thing after the game's
	 * IRQ4, before the FM raise (slim21-25 landed here and worked; the
	 * consume top, after the fetch, and after the raise all lost the
	 * background ON THE FPGA while ares rendered every one; real 32X
	 * hardware has not been tried). */
	slim_dma();
}
__attribute__((section(".data"), noinline))
static void md_consume(uint32_t pkt_base) {
	/* (slim step 3 used to run HERE, at the consume's top. On the FPGA the
	 * extra DMAs pushed the consume's end -- where the BG palette DMA is
	 * vblank-gated -- past line 0xFF on every load vint, and the palette
	 * deferred for the whole level: art in VRAM, black pens (Mike,
	 * 2026-09-20: "the palette swap at Neff is what triggers the
	 * background"). The fetch drains it in partb_hook instead.) */
	if (*(volatile uint16_t*)0xA15100 & 0x8000) {
		(*(volatile uint16_t*)0xFFA0F2)++;   /* diag: consume skipped, FM still up */
	}
	if (*(volatile uint16_t*)0xA15100 & 0x8000)
		return;
		// PIVOT SLICE 1b — RECEIVE A TILE BATCH AND PUSH IT TO VRAM.
		// FM is 0, so the FB window at 0x840000 is ours to read. The
		// packet is self-describing; re-uploading a batch we already
		// have is harmless, which is what makes this immune to the
		// per-bank staging skew that bit the palette path.
		{
			// PIVOT SLICE 1c — packet from the SH-2. Header is always
			// valid; payload alternates tiles and name-table chunks.
			//   [0] magic [1] type [2] param [3] hscroll [4] vscroll
			//   [5] count  [8..] payload
			// TEAR GUARD RETIRED (measured, WIP-ladder bisect): the
			// 736-word FB copy cost ~half the window rate ([9] 1147->632)
			// — 68K FB reads are slow — and the tear it guarded against
			// cannot happen: the receiver and the SH-2's packet rebuild
			// are strictly sequential (both inside the vint chain), and
			// the torn-packet counter never fired. The ares confetti was
			// the fallback-pen bug, fixed by the usage-mask allocator.
			volatile uint16_t *live = (volatile uint16_t*)pkt_base;
			volatile uint16_t *sc = live;
			uint16_t r60_isB = (pkt_base == 0x85E800uL);
			if (r60_isB) *(volatile uint16_t*)0xFFA08E =
				*(volatile uint16_t*)0xC00008;    /* B: entry */
			// last-magic diag RELOCATED to WRAM 0xFFA020 (LOOP 13 part 4):
			// it sat at 0xFFB0E0 — the DREQ push-abort counter — so every
			// MD_BG build stamped 0xB6B6 over the abort count each window
			// and push_aborts read as garbage. FOURTH slot collision this
			// era. All MD_BG push_aborts figures before this line are void.
			(*(volatile uint16_t*)0xFFA020) = live[0];    // diag: last magic seen
			// SPAN-SPLIT PROBES (write-budget design): V at each stage,
			// packed per type so lua can attribute the cost.
			(*(volatile uint16_t*)0xFFB0B0) =
				*(volatile uint16_t*)0xC00008;            // V at entry
			if (live[0] == 0xB6B6) {
				(*(volatile uint16_t*)0xFFB0E2)++;        // diag: packets consumed
				/* per-buffer census (0xFFA076+, free): who consumes,
				 * carrying what */
				if (pkt_base == 0x857000uL || pkt_base == R60_PKTODD_68K) {
					(*(volatile uint16_t*)0xFFA076)++;
					*(volatile uint16_t*)0xFFA078 = live[1];
				} else {
					(*(volatile uint16_t*)0xFFA07A)++;
					*(volatile uint16_t*)0xFFA07C = live[1];
				}
				/* consumed-mark (LOOP24 lossless handshake): zero the
				 * magic so the SH-2's publish can tell consumed from
				 * pending and DEFER instead of overwriting. FM=0 here
				 * by the guard above, so the FB write lands. Cleared
				 * FIRST: a re-entry mid-consume then skips cleanly.
				 * (R60: without this the MD plane re-consumed one boot
				 * packet forever — tiles 1046, chunks 0.) */
				live[0] = 0;
				// LOOP15 (NT_WRAP): sc[1] bit15 = "palette block changed
				// this window"; the type lives in the low byte.
				if (r60_isB) *(volatile uint16_t*)0xFFA090 =
					*(volatile uint16_t*)0xC00008;   /* B: post-census */
				fbx_echo = (uint8_t)(sc[5] >> 12);            /* FBX ECHO: master's lifted seq */
				uint16_t typ = (uint16_t)(sc[1] & 0xFF), cnt = (uint16_t)(sc[5] & 0x0FFFu);
				(*(volatile uint16_t*)0xFFB0BE) = sc[1];
				if (sc[1] & 0x8000u) (*(volatile uint16_t*)0xFFB0C6)++;
				if (sc[1] & 0x2000u) md_hold_seen = 2;
				else if (!md_hold_seen) md_hold_seen = 1;
				{	/* THE DISPLAY GATE'S HOLD, applied here in the port build (the k1/k2 block
					 * that mirrored IO_MISC into reg 1 is not compiled): bit 13 = MD display
					 * off, keeping the port's reg-1 value 0x74 (IE0 on, DMA on) otherwise. */
					uint8_t h = (uint8_t)((sc[1] >> 13) & 1u);
					/* Full/empty packets and the frontend all write reg1. A
					 * path-local cache cannot describe its current value. */
					*(volatile uint16_t*)VDP_CTRL_PORT = h ? 0x8134 : 0x8174;
				}
				uint16_t palp = (uint16_t)(sc[1] & 0x8000u);
#ifdef CRAM_PROBE
				/* shadow of every BG palette block this consume will land */
				if (palp) {
					volatile uint16_t *sh = pal_shadow_w;
					for (uint16_t i = 0; i < 48; i++) sh[i] = sc[688 + i];
					pal_shadow_seen++;              /* palettes seen */
				}
#endif
#ifdef PAL_FIRST
				/* PALETTE FIRST (2026-09-20). The BG palette DMA used to sit at
				 * the END of the consume behind a vblank gate; anything that
				 * lengthened the consume or the vint on the FPGA deferred it,
				 * and a deferred palette ON THE FPGA showed as a black level
				 * background (Mike: "the palette swap at Neff is what triggers
				 * the background"). Land it before any tile or name-table
				 * work when the blank budget allows. R60's A consume may
				 * already have run into the picture before B starts, so its
				 * late palette is queued for the next safe blank instead. */
				if (palp) {
                    uint16_t pv = (uint16_t)(*(volatile uint16_t*)0xC00008 >> 8);
                    if (pv >= 0xE0u && pv < 0xF8u) {
                        r60_palette_pending = 0u; /* this newer palette supersedes a queued one */
					uint32_t src = ((uint32_t)(sc + 688)) >> 1;
					*(volatile uint16_t*)VDP_CTRL_PORT = 0x8F02;
					*(volatile uint16_t*)VDP_CTRL_PORT = 0x9340;
					*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
					*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
					*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
					*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
					*vdp_ctrl_wide = ((uint32_t)(0xC000u | 0u) << 16) | 0x80u;
					(*(volatile uint16_t*)0xFFA164)++;                 /* diag: palettes landed first */
                    } else {
                        for (uint16_t i = 0; i < 64u; i++) r60_palette_hold[i] = sc[688u + i];
                        r60_palette_pending = 1u;
                    }
					palp = 0;                                          /* the gated block below is skipped */
				}
#endif
				if (typ == 0) (*(volatile uint16_t*)0xFFB0E4)++;   // tile batches
				else          (*(volatile uint16_t*)0xFFB0E6)++;   // name chunks
				(*(volatile uint16_t*)0xFFB0E8) = (uint16_t)(sc[5] & 0x0FFFu);  // last count
				// vertical fine scroll, every window and nearly free
				// (horizontal is per-strip now: cell-mode hscroll words
				// ride each name-table chunk)
				// VSRAM addr 2 = plane B. The old 0x40000010|2 put the
				// 2 in the SECOND control word (address bits 16:14), so
				// it wrote VSRAM 0 -- PLANE A's vscroll -- all along.
				// (A/B'd during the MAME blackout hunt: not the cause.)
				// STAGED (LOOP 13 part 3): NO VDP port writes here — the
				// beam is drawing these very rows (measured V=0x0B..0x35).
				// Everything appends to the 0xFFA400 buffer, flushed by
				// md_stage_play inside the NEXT vblank via DMA. The V
				// stage probes now time the staging copy, not port writes.
				/* R60 DIRECT CONSUME — no WRAM staging, no stage_play.
				 * The staging indirection existed because the legacy
				 * consume ran MID-FRAME (beam on the picture); under R60
				 * this runs at vint top, INSIDE vblank, so the payload
				 * goes straight to the VDP — and by DMA FROM THE FB
				 * WINDOW (source 0x85xxxx), the commercial-title idiom.
				 * The 68K copy loop it replaces was the whole post
				 * delay: measured +3..+40 beam lines (f703 blew the
				 * 1748-tick flip guard outright), 26%% flips declined. */
				{
				(*(volatile uint16_t*)0xFFB0B2) =
					*(volatile uint16_t*)0xC00008;
				*vdp_ctrl_wide = ((uint32_t)(0x4000u | 2u) << 16) | 0x10u;
				*vdp_data_port = sc[4];    /* VSRAM 2 = plane B vy */
				*vdp_ctrl_wide = ((uint32_t)0x4000u << 16) | 0x10u;
				*vdp_data_port = sc[6];    /* VSRAM 0 = plane A vy */
				if (typ == 0) {
				/* SLIM PIPELINE (docs/design/SLIM-PIPELINE.md): the SH-2
				 * ships a 2-WORD record (slot|fg<<15, blk*64+code) and the
				 * 68K fetches the art from cart itself. cart -> 68K -> VRAM
				 * is two payload copies against the FB route's three.
				 *
				 * STAGE HERE, SHIP LATER (2026-09-19, rig-proven). This
				 * consume runs at the TOP of vblank, and 68K cart-window
				 * reads here collapse the FPGA's picture (64 reads on the
				 * line: 7-11% non-black) while the same 64 reads after the
				 * game's IRQ4 has returned are clean (99%). The master's
				 * V-ISR needs the cart bus in early vblank and the adapter
				 * gives a pending 68K access priority (S32X_MiSTer IF.sv,
				 * the ROM arbiter). ares charges instruction cycles only and
				 * cannot see it: every slim build rendered there. So this
				 * branch only COPIES the records to WRAM; slim_fetch() reads
				 * the art after the game's IRQ4 and slim_dma() lands it
				 * next vint -- see the SLIM PIPELINE STATE comment. */
					{
					volatile uint16_t *e = sc + 8;
					slim_walk(e, cnt);
					}
					{
					/* FETCH HERE (slim28, 2026-09-20): cart -> slim_art now, so
					 * the DMA at the start of partb_hook lands this vint's art
					 * in this vint. The FPGA accepts the DMA only at that spot
					 * (slim18-20, 26, 27 lost the background elsewhere); the
					 * "cart reads in the consume kill the FPGA" reading was a
					 * title-screen misread (LESSONS 2026-09-20). */
					slim_fetch();
					}
					/* THE SCROLL RUNS ON BOTH PATHS. It used to sit
					 * inside the FB branch, and the first TILE_SLIM cut
					 * spliced that branch out with `} else if (0) {` --
					 * which silently took the hscroll table write with
					 * it. Under slim the planes therefore never got a
					 * scroll value at all, which is a black screen, not
					 * a tile defect. That is why the failure survived
					 * SLIMCAP=8, the stride fix, and every read-route
					 * experiment: none of them were the bug. */
					{	/* scroll rides the tile chunk too: sc[3]/sc[7] */
						*vdp_ctrl_wide = ((uint32_t)(0x4000u | 0x3C00u) << 16) | 3u;
						*vdp_data_port = sc[3];
						*vdp_data_port = sc[7];
					}

				} else {
					/* NT_WRAP chunk: mirror-diffed rows; each span's
					 * cells are contiguous in the FB — DMA per span. */
					volatile uint16_t *e = sc + 8;
					uint16_t isa = (uint16_t)(sc[2] & 0x8000u ? 1 : 0);
					uint32_t nbase = isa ? 0xC000u : 0xE000u;
					/* THE ROW COUNT RIDES sc[5] AS rows*64 (sw168): 448 = 7 rows with the display up,
					 * 896 = 14 under the game's blank; 2048 = 32 short entering-edge spans. */
					uint16_t nrows = (uint16_t)(sc[5] >> 6);
					if (nrows == 0 || nrows > 32) nrows = 7;
					for (uint16_t r = 0; r < nrows; r++) {
						uint16_t hdr = *e++;
						uint32_t rb = nbase
							+ (uint32_t)((hdr >> 8) & 31u) * 128u;
						if (hdr & 0x8000u) {
							/* FULL ROW (2026-09-22, the eye's pan): the plane
							 * pans faster than the edge pair's one-column
							 * lead covers (9-12 px a frame against a 9-window
							 * row rotation), so the master ships the whole
							 * 64-cell plane row in plane order: one DMA. */
							uint32_t src = ((uint32_t)e) >> 1;
							*(volatile uint16_t*)VDP_CTRL_PORT = 0x9340;
							*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
							*vdp_ctrl_wide = ((uint32_t)(0x4000u | (rb & 0x3FFFu)) << 16)
								| (((rb >> 14) & 3u) | 0x80u);
							e += 64;
                            r60_md_completed();
							continue;
						}
						uint16_t w1  = *e++;
						uint16_t c0 = hdr & 63u;
						uint16_t st = (uint16_t)((w1 >> 8) & 0x7F);   /* bit 15: EDGE42 pair follows */
						uint16_t nc2 = (uint16_t)(w1 & 0xFF);
						if (nc2) {
							if (isa) (*(volatile uint16_t*)0xFFA024) += nc2;
							else     (*(volatile uint16_t*)0xFFA026) += nc2;
						}
						while (nc2) {
							uint16_t pc = (uint16_t)((c0 + st) & 63u);
							uint16_t l1 = (uint16_t)(64u - pc);
							if (l1 > nc2) l1 = nc2;
							uint32_t a = rb + (uint32_t)pc * 2u;
							uint32_t src = ((uint32_t)e) >> 1;
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9300 | (l1 & 0xFF));
							*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
							*vdp_ctrl_wide = ((uint32_t)(0x4000u | (a & 0x3FFFu)) << 16)
								| (((a >> 14) & 3u) | 0x80u);
							e += l1;
							st = (uint16_t)(st + l1);
							nc2 = (uint16_t)(nc2 - l1);
                            r60_md_completed();
						}
						if (w1 & 0x8000u) {      /* edge pair: cols -1 and 40 */
							uint32_t a0 = rb + (uint32_t)((c0 + 63u) & 63u) * 2u;
							uint32_t a1 = rb + (uint32_t)((c0 + 40u) & 63u) * 2u;
							*vdp_ctrl_wide = ((uint32_t)(0x4000u | (a0 & 0x3FFFu)) << 16)
								| ((a0 >> 14) & 3u);
							*vdp_data_port = *e++;
							*vdp_ctrl_wide = ((uint32_t)(0x4000u | (a1 & 0x3FFFu)) << 16)
								| ((a1 >> 14) & 3u);
							*vdp_data_port = *e++;
						}
					}
					*(volatile uint16_t*)0xFFA08A =
						*(volatile uint16_t*)0xC00008;   /* V after spans */
					*(volatile uint16_t*)0xFFA170 = *(volatile uint16_t*)0xC00008;   /* fine: spans done */
					{	/* full-screen hscroll: reg 0x0B = 00 (init), so only
						 * 0xFC00 (A) and 0xFC02 (B) matter — two header words,
						 * sc[3] and sc[7], in EVERY packet (2026-09-05). The 56-
						 * word per-strip DMAs wrote a table the VDP ignored. */
						*vdp_ctrl_wide = ((uint32_t)(0x4000u | 0x3C00u) << 16) | 3u;
						*vdp_data_port = sc[3];
						*vdp_data_port = sc[7];
						/* VSRAM: plane A vy (sc[6]) then plane B vy (sc[4]); the R60 direct
						 * path never wrote them, so a plane's vertical page half was fixed. */
						*vdp_ctrl_wide = ((uint32_t)0x4000u << 16) | 0x10u;
						*vdp_data_port = sc[6];
						*vdp_data_port = sc[4];
					}
					if (sc[1] & 0x4000u) {   /* art tail: [n] n x [slot][16 words] */
						uint16_t na = *e++;
						slim_walk(e, na);        /* mixed records (2026-09-21) */
						(*(volatile uint16_t*)0xFFA0F0) += na;
						na = 0;
						slim_fetch();            /* fetch the tail's art this vint too, not at the next batch */
						for (uint16_t i = 0; i < na; i++, e += 17) {
							uint32_t va = (uint32_t)e[0] * 32u;
							if (va + 32u > 0xB000u) continue;
							uint32_t src = ((uint32_t)(e + 1)) >> 1;
							*(volatile uint16_t*)VDP_CTRL_PORT = 0x9310;
							*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
							*vdp_ctrl_wide = ((uint32_t)(0x4000u | (va & 0x3FFFu)) << 16)
								| (((va >> 14) & 3u) | 0x80u);
						}
						(*(volatile uint16_t*)0xFFA0F0) += na;   /* diag: tail records */
					}
					(*(volatile uint16_t*)0xFFB0B6) =
						*(volatile uint16_t*)0xC00008;
				}
				if (palp)
				{	/* live BG palette: DMA 48 FB words -> CRAM 16-63.
					 * VBLANK-GATED (2026-09-03, Mike's "black MD dots"): a
					 * CRAM write during active display paints a DAC dot at
					 * the beam (ares models it; the VDP does it). Measured
					 * on the story panel: this consume ran to active line
					 * 17 and its palette landed on-screen as a row of dots.
					 * Hoisting the DMA ahead of the name tables cost 9% of
					 * ships in the soak (everything before the DREQ push
					 * moves the launch onto the one-vint cliff), so: beam
					 * still in vblank -> DMA now as always; beam in the
					 * picture -> copy the block to WRAM and DMA it at the
					 * next vint top, before anything else. */
					uint16_t vnow = *(volatile uint16_t*)0xC00008;
					*(volatile uint16_t*)0xFFA090 = vnow;              /* V at pal DMA */
					if ((vnow >> 8) >= 0xE0u) {
						uint32_t src = ((uint32_t)(sc + 688)) >> 1;
						*(volatile uint16_t*)VDP_CTRL_PORT = 0x9340;
						*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
						*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
						*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
						*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
						*vdp_ctrl_wide = ((uint32_t)(0xC000u | 0u) << 16) | 0x80u;
						/* THE FOURTH LINE (2026-10-09). Lines 1-3 are the 48 words above;
						 * line 0 is the block the SH-2 publishes at FB 0x1EDC0, which is
						 * where the MD sprite palette used to live and which MDSPROFF
						 * frees. 15 words from 0x85EDC2 = entries 1-15, so CRAM entry 0
						 * stays the backdrop and is never written -- pen 0 of a line is
						 * the baker's permanently-free 0xFFFF slot, so nothing is lost.
						 * Byte for byte the DMA mdspr_consume used to issue. VBLANK ONLY,
						 * the same rule and for the same reason: the block re-ships every
						 * window, so a skipped frame just keeps the last colours. */
						{
							uint32_t s4 = 0x85EDC2uL >> 1;
							/* AUTOINC TO 2 FIRST, 2026-10-10. The hunk this came from left
							 * this line out and the 15 words never landed: CRAM line 0 read
							 * zero in every dump. mdspr_consume sets it before the same DMA
							 * (:595, and the reason at :496 -- "the hscroll DMA leaves 32
							 * behind on some paths"). The 48-word DMA just above does not
							 * set it either; the only 0x8F02 near it is in the direct-store
							 * test branch at :2248, which ends `if (0)`. */
							*(volatile uint16_t*)VDP_CTRL_PORT = 0x8F02;
							*(volatile uint16_t*)VDP_CTRL_PORT = 0x930F;
							*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (s4 & 0xFF));
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((s4 >> 8) & 0xFF));
							*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((s4 >> 16) & 0x7F));
							*vdp_ctrl_wide = ((uint32_t)0xC002u << 16) | 0x80u;
						}
					} else {
						volatile uint16_t *pb = (volatile uint16_t*)0xFFA100;  /* 48-word hold */
						for (uint16_t i = 0; i < 48; i++) pb[i] = sc[688 + i];
						*(volatile uint16_t*)0xFFA160 = 1;                 /* deferred flag */
						(*(volatile uint16_t*)0xFFA162)++;                 /* deferrals */
					}
				}
				}
				(*(volatile uint16_t*)0xFFB0B8) =
					*(volatile uint16_t*)0xC00008;    // V after CRAM stage
				*(volatile uint16_t*)0xFFA172 = *(volatile uint16_t*)0xC00008;   /* fine: after pal */
				if (r60_isB) *(volatile uint16_t*)0xFFA092 =
					*(volatile uint16_t*)0xC00008;   /* B: pre-clear */
				live[0] = 0;                // consumed (the FB packet)
				*(volatile uint16_t*)0xFFA174 = *(volatile uint16_t*)0xC00008;   /* fine: after mark */
				// WINSPAN (LOOP15): packet-consume span in beam lines,
				// entry probe (0xFFB0B0) -> post-CRAM-stage (0xFFB0B8).
				// 0xFFA038 u32 sum, 0xFFA03C u16 n, 0xFFA03E u16 max.
				// Wrap-ambiguous samples (V jump region) discarded; a
				// consistent relative meter for ranking builds, not an
				// absolute clock. state_health prints it.
				{
					uint16_t d = (uint16_t)
						((((*(volatile uint16_t*)0xFFB0B8) >> 8)
						- ((*(volatile uint16_t*)0xFFB0B0) >> 8)) & 0xFF);
					if (d < 0x80) {
						(*(volatile uint32_t*)0xFFA038) += d;
						if (d > 16) (*(volatile uint16_t*)0xFFA096)++;
						if (d > 28) (*(volatile uint16_t*)0xFFA098)++;
						if (d > 16 && r60_isB)
							(*(volatile uint16_t*)0xFFA09A)++;
						if (d > 16)             /* worst offender's typ+cnt */
							*(volatile uint16_t*)0xFFA09C =
								(uint16_t)((sc[1] << 8) | (sc[5] & 0xFF));
						(*(volatile uint16_t*)0xFFA03C)++;
						if (d > *(volatile uint16_t*)0xFFA03E)
							*(volatile uint16_t*)0xFFA03E = d;
					}
				}
packet_done: ;
			*(volatile uint16_t*)0xFFA176 = *(volatile uint16_t*)0xC00008;   /* fine: consume end */
			}
		}
}
