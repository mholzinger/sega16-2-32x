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
	uint16_t skip_a = 0, skip_b = 0;
#ifdef R60_IDLECONSUME
	if (!r60_shim_idle)
#endif
	(*(volatile uint16_t*)0xFFB0F0)++;            /* vints: the gate's counter */
#ifdef LAUNCHER
	{	/* the launcher's smoke: this vblank's line scroll, the launcher's own code in RAM (launcher_smoke.c); off once the game runs */
		extern volatile unsigned short launcher_smoke_on;
		if (launcher_smoke_on) ((void (*)(void))0x00FF4980uL)();   /* frontend.h: LAUNCHER_SMOKE_RAM */
	}
#endif
#if defined(R60) && defined(NT_WRAP) && defined(PAL_FIRST)
    r60_palette_drain();
#endif
	/* PLAY LAST VINT'S STAGED VDP WRITES, FIRST, INSIDE VBLANK. md_consume does not write
	 * VRAM: it stages every art and name-table record into WRAM (0xFFA400) for
	 * md_stage_play, which the OLD shim ran at the top of its vint and this shim never
	 * called -- so on ares the planes' VRAM did not change by one byte between frames
	 * (2026-10-14 22:20) while every packet read as consumed. Touches the MD VDP only, no
	 * FM needed; the planes land one vint late, uniformly, as the old line did. */
	md_stage_play();
	{	/* THE MASTER'S CRAM VERDICT, ON THE MD BACKDROP. A 32X-side instrument cannot
		 * report a CRAM fault -- every 32X pixel resolves through CRAM -- so the master
		 * posts it here and the backdrop carries it: GREEN matched, RED did not. */
		uint16_t v = *mars_comm12;
#ifdef R60_MDVERDICT
		if ((v & 0xF000u) == 0xA000u)
			vdp_color(0, (v & 1u) ? 0x00E : 0x0E0);
#else
		(void)v;                                      /* the backdrop stays black on the line */
#endif
	}
	if (*(volatile uint16_t*)0xA15100 & 0x8000)   /* FM up: the master owns the FB */
		return;
    r60_md_consuming = 1;
	{	/* A STALE PACKET (sw173): one published into the bank the flip then showed, never
		 * consumed, met again when that bank is written next -- at a hold's release it
		 * landed 560 zero cells on the eye. The window sequence rides word 2's low byte;
		 * a marker with the last-consumed sequence is cleared and skipped. */
		static uint8_t lastsq_a = 0xFF, lastsq_b = 0xFF;
		volatile uint16_t *ea = (volatile uint16_t*)0x857000uL, *eb = (volatile uint16_t*)0x85E800uL;
		if (ea[0] == 0xB6B6) { uint8_t sq = (uint8_t)ea[2], d = (uint8_t)(sq - lastsq_a); if (lastsq_a != 0xFF && (d == 0 || d > 0x80)) {
				ea[0] = 0; skip_a = 1; (*(volatile uint16_t*)0xFFA09A)++; } else lastsq_a = sq; }
		if (eb[0] == 0xB6B6) { uint8_t sq = (uint8_t)eb[2], d = (uint8_t)(sq - lastsq_b); if (lastsq_b != 0xFF && (d == 0 || d > 0x80)) { eb[0] = 0; skip_b = 1; (*(volatile uint16_t*)0xFFA09C)++; } else lastsq_b = sq; }
	}
	{	/* AN EMPTY PACKET (header bit 12, set by the master): take it without the walk. The
		 * hold bit is still applied and the packet still consumed-marked and counted. */
		volatile uint16_t *ea = (volatile uint16_t*)0x857000uL;
		if (!skip_a && ea[0] == 0xB6B6 && (ea[1] & 0x1000u)) {
			uint8_t h = (uint8_t)((ea[1] >> 13) & 1u);
			/* Other packet paths/frontend may have changed reg1. */
			*(volatile uint16_t*)VDP_CTRL_PORT = h ? 0x8134 : 0x8174;
			ea[0] = 0; (*(volatile uint16_t*)0xFFA076)++; (*(volatile uint16_t*)0xFFB0E2)++;
			(*(volatile uint16_t*)0xFFA094)++;        /* census: empty A taken */
			skip_a = 1;
		}
	}
	if (!skip_a)
	{	/* the odd-parity direct hold packet sits at 0x851800; its stub at 0x857000 says so (bit 11) */
		volatile uint16_t *ea2 = (volatile uint16_t*)0x857000uL;
		if (ea2[0] == 0xB6B6 && (ea2[1] & 0x0800u)) { ea2[0] = 0; md_consume(R60_PKTODD_68K); }
		else md_consume(0x857000uL);
	}
	/* PLAY A's STAGING BEFORE B IS CONSUMED. Each md_consume starts its staging at
	 * stg+8 and ends with stg[0] = its own record count; packet B's NT_WRAP branch DMAs
	 * its rows directly and stages nothing, so B left stg[0] = 0 and A's art records
	 * (16 words each, ~36 a frame) were discarded every vint: VRAM tile memory never
	 * changed by a byte on ares while 555 tile batches read as consumed. The old shim
	 * consumed A and B in different windows with a play between. ~3 lines, in vblank. */
	md_stage_play();
	{	volatile uint16_t *eb = (volatile uint16_t*)0x85E800uL;
		if (!skip_b && eb[0] == 0xB6B6 && (eb[1] & 0x1000u)
            && r60_tb_blank_resident /* a reset/menu invalidation must consume its zero command */
            ) {
			uint8_t h = (uint8_t)((eb[1] >> 13) & 1u);
			/* Other packet paths/frontend may have changed reg1. */
			*(volatile uint16_t*)VDP_CTRL_PORT = h ? 0x8134 : 0x8174;
			eb[0] = 0; (*(volatile uint16_t*)0xFFA07A)++; (*(volatile uint16_t*)0xFFB0E2)++;
			(*(volatile uint16_t*)0xFFA096)++;        /* census: empty B taken */
			skip_b = 1;
		}
	}
	if (!skip_b)
	md_consume(0x85E800uL);                       /* plane packet B: the name table */
    r60_md_consuming = 0;
	r60_shim_seq++;
	*mars_comm0 = (uint16_t)(R60_SHIM_DONE | (r60_shim_seq & 0xFFu));
#ifdef R60_LIVEPAD
	{	/* LIVE PADS (sw211; moved below the DONE post in sw216: at the shim's top it pushed the packet DMA out of vblank on the MiSTer and the planes came up garbage, ares unaffected). The 68K owns the MD pad ports; the game runs on the SH-2 and reads
		 * its controller ports 0xC41003/0xC41007 and the MCU's coin mailbox. Post both pads in
		 * the arcade's active-low byte form (md_to_arcade, the old shim's mapping) in COMM2 and
		 * the service bits in COMM8 behind an 0xA5 tag; the port applies them each game tick.
		 * Mapping: MD A punch, B kick, C jump; START = start1; Y (bit 9, or START+A) = coin; P2 START = start2. */
		uint16_t p1 = read_joypad(0), p2 = read_joypad(1);
		extern int frontend_input(unsigned short);
		if (frontend_input(p1)) p1 &= (uint16_t)~0x0FF0u;
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
