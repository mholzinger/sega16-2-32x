/* THE R60 68K VINT SHIM — REBUILD section 2's "<=10 lines".
 *
 * Mike's beat, 2026-10-15. FM=1 is the STEADY state: the slave draws straight into the
 * hidden framebuffer bank during active display, which is the commercial model
 * (REBUILD section 0, "keeps FM parked with the SH-2, the 68K a mailbox peer"). The one
 * concession is the MD planes, and it has a natural window -- the 68K's plane DMAs into
 * VRAM must happen in vblank anyway, and SILICON FACT 2 says the 68K can only read the
 * packet at FM=0. So FM=0 for the vblank span alone:
 *
 *   slave DONE before vblank
 *     -> master V-ISR flips and DROPS FM          (sh_src/r60/r60_main.c, visr_vbi)
 *     -> 68K vint: read both packets from the FB, DMA planes and CRAM, post DONE  [here]
 *     -> master RAISES FM on the post, or at vblank end as the backstop so a late 68K
 *        can never hold the frame                 (sh_src/r60/r60_main.c, m_main)
 *     -> slave GO into the other bank.
 *
 * Vblank is ~36 of 262 lines and the consume measures 12-17, so it fits with margin;
 * the backstop raises are counted and a nonzero count says by how much it does not.
 *
 * WHAT IS NOT HERE, deliberately: no windows, no announce/post pairs, no FM writes (the
 * master owns FM), no spin waiting on the SH-2. The old shim_vblank is 68K-era renderer
 * and stays out of r60 the way m_main.c does -- it is compiled as shim_vblank_legacy and
 * nothing calls it. The boot beacons (0x600D / 0xB007 / the slave warm-up) stay in
 * md_main.c because they are boot, not cadence.
 *
 * md_consume keeps the DMA bodies verbatim: its staging is self-contained
 * (md_main.c:2326, stg = 0xFFA400) so it is callable from anywhere, and it is the same
 * relationship compose_sprites had to the new sprite record -- the reference for what to
 * write to the hardware, not code to be reinvented.
 */
#define R60_SHIM_DONE  0xC000u       /* COMM0: 0xC0 | sequence, the 68K's DONE post */

#ifdef R60_IDLECONSUME
static volatile uint8_t r60_shim_idle;   /* 1 while the idle loop runs the shim: the vint count stays honest */
#endif
void shim_vblank(void)
{
	static uint16_t r60_shim_seq;
#ifdef R60_EMPTYPKT
	uint16_t skip_a = 0, skip_b = 0;
#endif
#ifdef R60_IDLECONSUME
	if (!r60_shim_idle)
#endif
	(*(volatile uint16_t*)0xFFB0F0)++;            /* vints: the gate's counter */
#ifdef R60_SPANPROBE
	*(volatile uint16_t*)0xFFA084 = *(volatile uint16_t*)0xC00008;   /* V at shim entry */
#endif
#ifdef MD_BG
	/* PLAY LAST VINT'S STAGED VDP WRITES, FIRST, INSIDE VBLANK. md_consume does not write
	 * VRAM: it stages every art and name-table record into WRAM (0xFFA400) for
	 * md_stage_play, which the OLD shim ran at the top of its vint and this shim never
	 * called -- so on ares the planes' VRAM did not change by one byte between frames
	 * (2026-10-14 22:20) while every packet read as consumed. Touches the MD VDP only, no
	 * FM needed; the planes land one vint late, uniformly, as the old line did. */
	md_stage_play();
#endif
	{	/* THE MASTER'S CRAM VERDICT, ON THE MD BACKDROP. A 32X-side instrument cannot
		 * report a CRAM fault -- every 32X pixel resolves through CRAM -- so the master
		 * posts it here and the backdrop carries it: GREEN matched, RED did not. */
		uint16_t v = *mars_comm12;
#ifdef R60_MDPROBE
		/* UNCONDITIONAL: is the MD layer alive on the board at all? Every 32X pixel
		 * resolves through 32X CRAM, so when the board disagrees with ares about the
		 * picture, no 32X-side instrument can say why. The MD backdrop is a separate
		 * path (MD CRAM entry 0) and shows wherever the 32X layer is transparent --
		 * but only if the 68K reached its VDP setup and the MD display is on, which is
		 * exactly what the MD PLANES will need too. Green = this code ran. */
		vdp_color(0, 0x0E0);
		/* AND WRITE 32X CRAM FROM THIS SIDE. The 68K reaches 32X CRAM at 0xA15200 --
		 * md_main.c's MDSTAGE uses exactly that for its boot colours, so it is a path
		 * this board is known to honour. Entry 255 is the renderer's reserved bar pen,
		 * which the master also writes from 0x20004200. If the white bar appears with
		 * this and not without it, the master's CRAM writes are what the FPGA is
		 * dropping -- which would explain all four board symptoms at once: speckled
		 * sprites (most pens stale), no bar, no test block, and a palette that ares
		 * reads back clean. */
		*(volatile uint16_t*)(0xA15200uL + 255u * 2u) = 0x7FFFu;
#endif
#ifdef R60_MDVERDICT
		if ((v & 0xF000u) == 0xA000u)
			vdp_color(0, (v & 1u) ? 0x00E : 0x0E0);
#else
		(void)v;                                      /* the backdrop stays black on the line */
#endif
	}
	if (*(volatile uint16_t*)0xA15100 & 0x8000)   /* FM up: the master owns the FB */
		return;
#ifdef R60_HTPROBE
	/* PROBE (R60HTPROBE): what the consume saw. The slave draws COMM12 as blocks. */
	uint16_t ht_fs0 = (uint16_t)(*(volatile uint16_t*)0xA1518A & 1u);
	uint8_t ht_a = *(volatile uint16_t*)0x857000uL == 0xB6B6, ht_b = *(volatile uint16_t*)0x85E800uL == 0xB6B6;
#endif
#ifdef R60_SPANPROBE
	*(volatile uint16_t*)0xFFA086 = *(volatile uint16_t*)0xC00008;   /* V before consume A */
#endif
#if defined(R60_HOLDDIRECT) && defined(R60_EMPTYPKT)
	{	/* A STALE PACKET (sw173): one published into the bank the flip then showed, never
		 * consumed, met again when that bank is written next -- at a hold's release it
		 * landed 560 zero cells on the eye. The window sequence rides word 2's low byte;
		 * a marker with the last-consumed sequence is cleared and skipped. */
		static uint8_t lastsq_a = 0xFF, lastsq_b = 0xFF;
		volatile uint16_t *ea = (volatile uint16_t*)0x857000uL, *eb = (volatile uint16_t*)0x85E800uL;
		if (ea[0] == 0xB6B6) { uint8_t sq = (uint8_t)ea[2], d = (uint8_t)(sq - lastsq_a); if (lastsq_a != 0xFF && (d == 0 || d > 0x80)) {
#ifdef R60_SKIPPROBE
				{ volatile uint16_t *rg = (volatile uint16_t*)0xFFA180; uint16_t k = (uint16_t)(rg[0] & 7u); rg[1 + k*4] = (uint16_t)((lastsq_a << 8) | sq); rg[2 + k*4] = ea[1]; rg[3 + k*4] = *(volatile uint16_t*)0xFFB0F0; rg[4 + k*4] = ea[5]; rg[0]++; }
#endif
				ea[0] = 0; skip_a = 1; (*(volatile uint16_t*)0xFFA09A)++; } else lastsq_a = sq; }
		if (eb[0] == 0xB6B6) { uint8_t sq = (uint8_t)eb[2], d = (uint8_t)(sq - lastsq_b); if (lastsq_b != 0xFF && (d == 0 || d > 0x80)) { eb[0] = 0; skip_b = 1; (*(volatile uint16_t*)0xFFA09C)++; } else lastsq_b = sq; }
	}
#endif
#ifdef R60_EMPTYPKT
	{	/* AN EMPTY PACKET (header bit 12, set by the master): take it without the walk. The
		 * hold bit is still applied and the packet still consumed-marked and counted. */
		volatile uint16_t *ea = (volatile uint16_t*)0x857000uL;
		if (!skip_a && ea[0] == 0xB6B6 && (ea[1] & 0x1000u)) {
			static uint8_t hold_last_e = 0xFF;
			uint8_t h = (uint8_t)((ea[1] >> 13) & 1u);
			if (h != hold_last_e) { *(volatile uint16_t*)VDP_CTRL_PORT = h ? 0x8134 : 0x8174; hold_last_e = h; }
			ea[0] = 0; (*(volatile uint16_t*)0xFFA076)++; (*(volatile uint16_t*)0xFFB0E2)++;
			(*(volatile uint16_t*)0xFFA094)++;        /* census: empty A taken */
			skip_a = 1;
		}
	}
	if (!skip_a)
#endif
#ifdef R60_HOLDDIRECT
	{	/* the odd-parity direct hold packet sits at 0x851800; its stub at 0x857000 says so (bit 11) */
		volatile uint16_t *ea2 = (volatile uint16_t*)0x857000uL;
		if (ea2[0] == 0xB6B6 && (ea2[1] & 0x0800u)) { ea2[0] = 0; md_consume(R60_PKTODD_68K); }
		else md_consume(0x857000uL);
	}
#else
	md_consume(0x857000uL);                       /* plane packet A: the art, STAGED */
#endif
#ifdef R60_SPANPROBE
	*(volatile uint16_t*)0xFFA088 = *(volatile uint16_t*)0xC00008;   /* V after A */
#endif
#ifdef R60_FSPROBE
	{	/* did the framebuffer bank flip under the A consume? */
		extern uint16_t fsp_fs0;
		if ((*(volatile uint16_t*)0xA1518A & 1u) != fsp_fs0) (*(volatile uint16_t*)0xFFA07E)++;
	}
#endif
#ifdef MD_BG
	/* PLAY A's STAGING BEFORE B IS CONSUMED. Each md_consume starts its staging at
	 * stg+8 and ends with stg[0] = its own record count; packet B's NT_WRAP branch DMAs
	 * its rows directly and stages nothing, so B left stg[0] = 0 and A's art records
	 * (16 words each, ~36 a frame) were discarded every vint: VRAM tile memory never
	 * changed by a byte on ares while 555 tile batches read as consumed. The old shim
	 * consumed A and B in different windows with a play between. ~3 lines, in vblank. */
	md_stage_play();
#endif
#ifdef R60_EMPTYPKT
	{	volatile uint16_t *eb = (volatile uint16_t*)0x85E800uL;
		if (!skip_b && eb[0] == 0xB6B6 && (eb[1] & 0x1000u)) {
			static uint8_t hold_last_f = 0xFF;
			uint8_t h = (uint8_t)((eb[1] >> 13) & 1u);
			if (h != hold_last_f) { *(volatile uint16_t*)VDP_CTRL_PORT = h ? 0x8134 : 0x8174; hold_last_f = h; }
			eb[0] = 0; (*(volatile uint16_t*)0xFFA07A)++; (*(volatile uint16_t*)0xFFB0E2)++;
			(*(volatile uint16_t*)0xFFA096)++;        /* census: empty B taken */
			skip_b = 1;
		}
	}
	if (!skip_b)
#endif
	md_consume(0x85E800uL);                       /* plane packet B: the name table */
#ifdef R60_SPANPROBE
	*(volatile uint16_t*)0xFFA08A = *(volatile uint16_t*)0xC00008;   /* V after B */
#endif
#ifdef R60_RB_PROBE
	{	/* THE READBACK PROBE. Thirty builds have argued about the board's picture with
		 * no way to ask it anything; this gives it one honest bit through the only path
		 * it is known to honour. The 68K reaches the write-side framebuffer bank at
		 * 0x840000 and 32X CRAM at 0xA15200, both at FM=0, and its whole span is FM=0
		 * by construction -- md_main.c's MDSTAGE boot colours prove that path on this
		 * board. So: READ what the slave drew, and REPORT it as the background colour,
		 * which is what the board shows wherever the 32X pixel is pen 0.
		 *
		 *   green  the slave's writes LAND and the problem is display-side
		 *   yellow they land in part: the write path drops
		 *   red    nothing lands, or this window does not see the drawn bank
		 *   unchanged: the 68K's own CRAM write at FM=0 is not landing either, which
		 *              breaks the assumption this probe rests on -- that is the finding
		 *
		 * Rows 60-168 step 2, x step 4: 4,320 reads, and the longer span is accepted. */
		volatile uint8_t *fb = (volatile uint8_t*)(0x840000uL + 0x200u);
		uint16_t row, x, n = 0;
		for (row = 60; row < 168; row += 2)
			for (x = 0; x < 320; x += 4)
				if (fb[(uint32_t)row * 320u + x]) n++;
		*mars_comm10 = n;
		*(volatile uint16_t*)0xA15200 = (n >= 400u) ? 0x03E0u
		                              : (n >= 100u) ? 0x03FFu : 0x001Fu;
	}
#endif
#ifdef R60_HTPROBE
	{	/* 16-bit counts in WRAM 0xFFA1F4.. (ares dumps), 3-bit copies in COMM12 (the rig's blocks):
		 * F4 FM up at the end of a consume that had a packet (the 68K's FB reads after the rise are junk
		 *    on the FPGA), F6 FM up at the end of a run that had no packet, F8 runs with no packet
		 *    (a DONE posted for nothing), FA FS changed under a run, FC FS changed under a run that had a
		 *    packet (the bank switched under the consume). COMM12 bits 6-8 carry FC. */
		volatile uint16_t *hc = (volatile uint16_t*)0xFFA1F4;
		static uint8_t hc_init; if (!hc_init) { hc[0] = hc[1] = hc[2] = hc[3] = hc[4] = 0; hc_init = 1; }
		uint8_t fm = (*(volatile uint16_t*)0xA15100 & 0x8000) != 0, pk = ht_a | ht_b;
		if (fm && pk) hc[0]++;
		if (fm && !pk) hc[1]++;
		if (!pk) hc[2]++;
		if ((uint16_t)(*(volatile uint16_t*)0xA1518A & 1u) != ht_fs0) { hc[3]++; if (pk) hc[4]++; }
		{	uint16_t a = hc[0] > 7 ? 7 : hc[0], b = hc[1] > 7 ? 7 : hc[1], c = hc[4] > 7 ? 7 : hc[4], d = hc[3] > 7 ? 7 : hc[3];
			*mars_comm12 = (uint16_t)(0xE000u | a | (b << 3) | (c << 6) | (d << 9)); }
	}
#endif
	r60_shim_seq++;
#ifdef R60_SPANPROBE
	*(volatile uint16_t*)0xFFA08C = *(volatile uint16_t*)0xC00008;   /* V at DONE */
#endif
	*mars_comm0 = (uint16_t)(R60_SHIM_DONE | (r60_shim_seq & 0xFFu));
#ifdef R60_LIVEPAD
	{	/* LIVE PADS (sw211; moved below the DONE post in sw216: at the shim's top it pushed the packet DMA out of vblank on the MiSTer and the planes came up garbage, ares unaffected). The 68K owns the MD pad ports; the game runs on the SH-2 and reads
		 * its controller ports 0xC41003/0xC41007 and the MCU's coin mailbox. Post both pads in
		 * the arcade's active-low byte form (md_to_arcade, the old shim's mapping) in COMM2 and
		 * the service bits in COMM8 behind an 0xA5 tag; the port applies them each game tick.
		 * Mapping: MD A punch, B kick, C jump; START = start1; Y (bit 9, or START+A) = coin; P2 START = start2. */
		uint16_t p1 = read_joypad(0), p2 = read_joypad(1);
#ifdef R60_FRONTEND
		extern int frontend_input(unsigned short);
		if (frontend_input(p1)) p1 &= (uint16_t)~0x0FF0u;
#endif
		uint8_t svc = 0;
		uint16_t start = p1 & 0x0880, aheld = p1 & 0x0040, xbtn = p1 & 0x0700;   /* sw220: START or MODE starts (on the MiSTer Mike's start button arrives as MODE: CREDITS 9 and no start while MODE coined); X, Y, Z coin */
		if (xbtn)  svc |= 0x01;          /* sw219: coin ONLY from X/Y/Z/MODE; START never coins (on the rig every START read as a coin: CREDITS 9, no start) */
		if (start && aheld) svc |= 0x01; /* sw265: START+A = coin, so a 3-button pad (no X/Y/Z/MODE) can credit; START alone still starts */
		else if (start) svc |= 0x10;
		if (p2 & 0x0880)              svc |= 0x20;
		*mars_comm2 = (uint16_t)(((uint16_t)md_to_arcade(p1) << 8) | md_to_arcade(p2));
		*mars_comm8 = (uint16_t)(0xA500u | svc);
	}
#endif
#ifdef R60_SOUND
	{ extern void snd_vint(void); snd_vint(); }   /* sound command from the SH-2 (COMM10) + one Z80 ring block, after everything else */
#endif
}

#ifdef R60_IDLECONSUME
/* CONSUME OFF THE VINT UNDER THE HOLD (sw192). The shim runs at the 68K's vint, so a packet
 * published at line L waited for the next vblank before its DMA began: the span was one to
 * two frames whatever the packet's size (slave event ring, sw189). The 68K's main loop in the
 * port build only idles; here it polls the span instead. Only packets carrying the MD hold
 * bit (header bit 13: the display is off, so DMA runs full speed and nothing can tear) are
 * taken this way; with the display up the vint keeps the timing. Interrupts are masked for the
 * call so the real vint cannot re-enter the consume. */
void r60_idle_consume(void)
{
	volatile uint16_t *ea = (volatile uint16_t*)0x857000uL, *eb = (volatile uint16_t*)0x85E800uL;
	if (*(volatile uint16_t*)0xA15100 & 0x8000) return;              /* FM up: not our span */
	if (!((ea[0] == 0xB6B6 && (ea[1] & 0x2000u)) || (eb[0] == 0xB6B6 && (eb[1] & 0x2000u)))) return;
	__asm__ __volatile__("move.w #0x2700,%%sr" ::: "memory");
	r60_shim_idle = 1; shim_vblank(); r60_shim_idle = 0;
	(*(volatile uint16_t*)0xFFA17E)++;                               /* census: idle consumes */
	__asm__ __volatile__("move.w #0x2000,%%sr" ::: "memory");
}
#endif
