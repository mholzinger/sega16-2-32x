#include "common.h"
#include "tile_thunks.h"
#include "pal_thunks.h"
#ifdef MD_HSCROLL_DIRECT
#include "hscr_thunks.h"
#endif
#include "packet_fmt.h"
/* PAL32 (LOOP 22) and the generated thunks must agree on the dirty-
 * state layout — a mismatch reads garbage as dirt or misses all of it.
 * game_body/pal_thunks.h regenerate on flag flips via FLAGSTAMP; this
 * catches a stale header from a partial build. */
#ifdef PAL32
#if !PAL_THUNKS_PAL32
#error pal_thunks.h generated without PAL32 (stale header - rebuild)
#endif
#if (!defined(WIN_TWO) || !defined(FB_TEXT_READ)) && !defined(R60)
#error PAL32 requires WIN_TWO + FBTEXT (k2 packet layout)
#endif  /* R60: the merged family replaces the k2 packet layout */
#elif PAL_THUNKS_PAL32
#error pal_thunks.h generated WITH PAL32 (stale header - rebuild)
#endif
#include "fmgate_tab.h"
#include "game_irq.h"      /* generated: GAME_IRQ4, GAME_ABSW_JMP_TARGET, GAME_MCU_* */
#ifdef MDSPR
#include "md_sprart_info.h"   /* generated: tools/bake_mdspr.py */
#endif
#if defined(FM_GATE) && !FMGATE_ON
#error fmgate_tab.h generated without FMGATE (stale header - rebuild)
#endif
#if !defined(FM_GATE) && FMGATE_ON
#error fmgate_tab.h generated WITH FMGATE (stale header - rebuild)
#endif
#ifdef FM_GATE
/* LOOP 23 — part A/B split state (part B lives in md_start.s and runs
 * via the rte trampoline, AFTER the game's vint upload). C globals on
 * purpose: the fixed-address diag blocks are a slot-collision
 * minefield eight collisions deep — the linker allocates these, and
 * probes read them via the map. */
uint16_t fmgate_wcmd;                /* stashed window cmd; 0 = none */
uint16_t fmgate_sr;                  /* real interrupt frame, saved   */
uint32_t fmgate_pc;                  /*   around the trampoline       */
volatile uint16_t fmgate_posted;     /* part B posted last vint       */
uint16_t fmgate_belt;                /* overrun-belt entries (diag)   */
uint16_t fmgate_defer;               /* part-B defers (diag)          */
const uint32_t fmgate_spans[] = FMGATE_SPANS;
#endif

// MD-side shim for the arcade game. Runs entirely from work RAM (.data):
// once RV=1 the low ROM map belongs to the game and the 0x880000 window
// must stay untouched.
//
// Stage B: replicate the i8751 MCU (see NOTES.md "MCU FULLY REVERSE-
// ENGINEERED") and run the game's own boot from its RAM copy at 0xFFB400.

static volatile uint16_t* const mars_comm0  = (uint16_t*) MARS_COMM0;
static volatile uint16_t* const mars_comm2  = (uint16_t*) MARS_COMM2;
static volatile uint16_t* const mars_comm4  = (uint16_t*) MARS_COMM4;
static volatile uint16_t* const mars_comm6  = (uint16_t*) MARS_COMM6;
#ifdef BOOT_FBXFER
static uint8_t fbx_seq;                  /* FB-transport probe sequence */
#endif
static volatile uint16_t* const mars_comm8  = (uint16_t*) MARS_COMM8;
/* per-scene sprite-art upload state (docs/design/BOSSFIGHT.md); WRAM slots since
 * the 68K RAMCODE trick makes .data execute-typed */
#define mdspr_up_left (*(volatile uint16_t*)0xFFA0DC)
#define mdspr_up_woff (*(volatile uint16_t*)0xFFA0DE)
#define mdspr_up_voff (*(volatile uint16_t*)0xFFA0E0)
static volatile uint16_t* const mars_comm10 = (uint16_t*) MARS_COMM10;
static volatile uint16_t* const mars_comm12 = (uint16_t*) MARS_COMM12;
static volatile uint16_t* const mars_comm14 = (uint16_t*) MARS_COMM14;

// Palette lives in FB staging now (game 0x840000 -> MD 0x85F000), read
// in-window by the SH-2 — the 0xFFA000 shadow and its stream are gone.

extern uint16_t read_joypad(uint8_t player);

// ---- shadow / mailbox addresses (see NOTES.md memory map) ----
#define IO_MISC     (*(volatile uint8_t*)0xFFB001)  // c40001: flip/display/lamps
#define IO_SERVICE  (*(volatile uint8_t*)0xFFB011)  // c41001: coins/service/start
#define IO_P1       (*(volatile uint8_t*)0xFFB013)  // c41003
#define IO_P2       (*(volatile uint8_t*)0xFFB017)  // c41007
#define IO_DSW2     (*(volatile uint8_t*)0xFFB021)  // c42001
#define IO_DSW1     (*(volatile uint8_t*)0xFFB023)  // c42003
#define IO_C43007   (*(volatile uint8_t*)0xFFB037)
#define BANK_SHADOW (*(volatile uint8_t*)0xFFB043)  // 3F0002 low byte
#ifdef TWO_POST
#define TP_ECHO_OK 0xF104   /* LOOP29 149: the ISR flipped, handed the FB back, ate post A */
#define TP_ECHO_NO 0xF1FE   /* declined, same hand-back */
#else
#define TP_ECHO_OK 0xF102
#define TP_ECHO_NO 0xF1FF
#endif
// MCU mailboxes are per-game (game_irq.h: US 0xFFF0C0/C2/C4, JP
// 0xFFF0D2/D0/D4 — the 68K program reads only its own MCU's addresses)
#define MCU_COINS   (*(volatile uint8_t*)GAME_MCU_COINS)  // MCU posts inverted SERVICE
#define MCU_BANKREQ (*(volatile uint8_t*)0xFFF095)  // game's tile bank request
#define MCU_SNDCMD  (*(volatile uint8_t*)GAME_MCU_SND)    // sound mailbox (0xFF = idle)
#define MCU_BUSY    (*(volatile uint8_t*)GAME_MCU_BUSY)   // screen-sync handshake
#define TEXT_SYNC   (*(volatile uint8_t*)0xFF8002)  // text RAM shadow +2

uint16_t game_running = 0;

static volatile uint16_t* const vdp_data_port = (uint16_t*) VDP_DATA_PORT;
static volatile uint32_t* const vdp_ctrl_wide = (uint32_t*) VDP_CTRL_PORT;

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

#ifdef MD_BG
/* LOOP 11 PIVOT, SLICE 1a — can MD video show THROUGH our 32X layer?
 * Everything downstream of the pivot assumes it can, and nothing has
 * ever tested it: the port has driven the MD VDP with 0.2 writes/frame
 * since it was written, and both name tables read empty on hardware.
 * So before converting a single S16 tile, paint a recognisable pattern
 * into Plane B (0xE000, 64x32, display already on from md_start.s) out
 * of the font glyphs md_start.s already uploaded to VRAM 0, and have
 * the SH-2 leave the BG rows at pixel 0 -- the documented MD-through
 * value that the allocator deliberately never assigns.
 * If this does not appear, the pivot is dead and we have spent an hour
 * instead of a month. */
/* Grey ramp in palette 0, pens 0-7 -- S16 tiles are 3bpp so pens 0-7 is
 * all they use. Matches the ramp tools/md_tiles.py renders with, so the
 * on-screen result can be compared directly against the offline PNG. */
__attribute__((section(".data")))
static void md_bg_palette(void) {
	/* NOTE: vdp_color() takes a CRAM BYTE address, so index i for i>0
	 * lands on entry i/2 (A0 ignored) — this ramp actually programs
	 * entries 0-3 with ramp[1,3,5,7]. Kept as-is; line 0 is only the
	 * placeholder/text line. */
	static const uint16_t ramp[8] = {
		0x0000, 0x0222, 0x0444, 0x0666, 0x0888, 0x0AAA, 0x0CCC, 0x0EEE };
	for (uint16_t i = 0; i < 8; i++)
		vdp_color(i, ramp[i]);
	/* Slot 1023 (VRAM 0x7FE0) is the RESERVED blank the SH-2 allocator
	 * never claims; nothing ever uploads it, and hardware VRAM powers up
	 * as garbage, so zero it here or "blank" cells show noise.
	 * (uint32_t) casts are LOAD-BEARING: -mshort makes int 16-bit, so a
	 * constant-only expression shifted <<16 evaluates to 0 and the VDP
	 * gets a null command — the CRAM block below silently vanished that
	 * way for a whole debugging arc. */
	*vdp_ctrl_wide = ((uint32_t)(0x4000u | 0x3FE0u) << 16) | 1u;
	for (uint16_t i = 0; i < 16; i++)
		*vdp_data_port = 0;
#ifdef MDSPR_SPIKE
	/* M0 SPIKE (docs/design/P3.md): prove the MD hardware-sprite plumbing under
	 * our transport in one screenshot. Line-0 CRAM colors, 16 tiles of
	 * striped test art at VRAM 0x8000 (tile index 1024), and TWO 32x32
	 * sprites mid-screen: entry 0 SAT-priority HIGH, entry 1 LOW —
	 * one build answers sprite-vs-plane both ways plus FB-vs-sprite
	 * (FB pixels must cover both). vdp_color takes a CRAM BYTE
	 * address, so entry i is index 2*i. */
	for (uint16_t i = 1; i < 16; i++)
		vdp_color(2u * i, (uint16_t)(((i & 1) ? 0x000E : 0x0000)
		                | ((i & 2) ? 0x00E0 : 0x0000)
		                | ((i & 4) ? 0x0E00 : 0x0000)
		                | ((i & 8) ? 0x0666 : 0x0000)));
	*vdp_ctrl_wide = ((uint32_t)(0x4000u | 0x0000u) << 16) | 2u; /* 0x8000 */
	for (uint16_t t = 0; t < 16; t++)
		for (uint16_t r = 0; r < 8; r++) {
			uint16_t pen = (uint16_t)((r + t) % 14u + 1u);
			uint16_t w = (uint16_t)(pen << 12 | pen << 8 | pen << 4 | pen);
			*vdp_data_port = w;              /* 8px row = 2 words */
			*vdp_data_port = w;
		}
	*vdp_ctrl_wide = ((uint32_t)(0x4000u | 0x3000u) << 16) | 3u; /* SAT */
	*vdp_data_port = 128 + 96;               /* e0: Y (screen y 96) */
	*vdp_data_port = 0x0F01;                 /* 4x4 tiles, link -> 1 */
	*vdp_data_port = 0x8400;                 /* prio 1, pal 0, tile 1024 */
	*vdp_data_port = 128 + 120;              /* X (screen x 120) */
	*vdp_data_port = 128 + 96;               /* e1: Y */
	*vdp_data_port = 0x0F00;                 /* 4x4 tiles, link 0 = end */
	*vdp_data_port = 0x0400;                 /* prio 0, pal 0, tile 1024 */
	*vdp_data_port = 128 + 190;              /* X (screen x 190) */
#endif
	/* Clear PLANE A's whole name table (0xC000, 64x32): the boot
	 * console left glyph entries there, and they drew a full-screen
	 * glyph grid OVER Plane B wherever the 32X layer was transparent.
	 * Tile 0's pattern is zeroed too so the all-zero table stays
	 * invisible. Plane A must show nothing until FG cat-0 moves onto
	 * it. */
	*vdp_ctrl_wide = ((uint32_t)0x4000u << 16);
	for (uint16_t i = 0; i < 16; i++)
		*vdp_data_port = 0;
	*vdp_ctrl_wide = ((uint32_t)0x4000u << 16) | 3u;   /* VRAM 0xC000 */
	for (uint16_t i = 0; i < 2048; i++)
		*vdp_data_port = 0;
	/* Plane B out-of-window cells -> the reserved blank slot. The
	 * packet ships 40 columns x 28 rows; fine scroll (vx&7 / vy&7)
	 * shifts the plane and reveals nametable cols 40+ and rows 28+,
	 * which nothing ever writes — VRAM boot garbage. That was BOTH
	 * edge artifacts (LOOP 13, native-capture diagnosis): the grey
	 * right-edge strip (cols 40-41 via hscroll) and the sky tick-row
	 * (row 31 at the top via vscroll; today's zero-fine-scroll
	 * capture had no ticks, yesterday's tick scenes did). Arcade
	 * shows real art in these 1-7px slivers; a 41-column/29-row
	 * packet is the fidelity follow-up. */
	/* BOTH planes. The first cut blanked only plane B; plane A's margin
	 * cells stayed 0x0000 = SLOT 0, which is a live cache slot — once
	 * real art lands there, fine hscroll leaks it into the right-edge
	 * sliver (savestate-proven 2026-08-15: plane A margins 672/672 at
	 * 0x0000 while plane B's were exactly 0x03FF; the unblanked plane-A
	 * margins also fooled the VRAM-base fingerprint into the +0x2000
	 * alias AGAIN — validate against BOTH planes, or anchor on mirror
	 * content). */
	for (uint16_t pl = 0; pl < 2; pl++) {
		uint32_t nt = pl ? 0xE000u : 0xC000u;
		for (uint16_t row = 0; row < 32; row++) {
#ifdef NT_WRAP
			/* wrap protocol: the WHOLE 64x32 plane is live window
			 * (cells land at wrapped positions) — blank-fill all of
			 * it; there are no margins anymore. */
			uint32_t a = nt + (uint32_t)row * 128u;
			uint16_t n = 64;
#else
			uint32_t a = nt + (uint32_t)row * 128u
			           + ((row < 28) ? 80u : 0u);
			uint16_t n = (row < 28) ? 24 : 64;
#endif
			*vdp_ctrl_wide = ((0x4000u | (a & 0x3FFFu)) << 16)
			               | ((a >> 14) & 3u);
			for (uint16_t c2 = 0; c2 < n; c2++)
				*vdp_data_port = 0x03FF;   /* blank slot, pal 0 */
		}
	}
#ifdef NT_WRAP
	/* cell-strip hscroll mode + a clean table: reg 0x0B bit1 selects
	 * per-8-line entries (32 bytes apart at VRAM 0xFC00; A +0, B +2).
	 * Zero the whole table so unstamped strips show the blank fill. */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8B02;
	*vdp_ctrl_wide = ((uint32_t)(0x4000u | 0x3C00u) << 16) | 3u;
	for (uint16_t i = 0; i < 512; i++)
		*vdp_data_port = 0;
#endif
	/* A/B: full-screen hscroll (reg 11 = 00). Cell mode made the MD
	 * plane vanish per-strip on MAME while VRAM/CRAM verified correct
	 * through the data port; bisecting whether the per-strip table is
	 * the breakage. */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8B00;
	/* staged-playback buffer starts empty (WRAM powers up random) */
	((volatile uint16_t*)0xFFA400)[0] = 0;
	((volatile uint16_t*)0xFFA400)[3] = 0;
	((volatile uint16_t*)0xFFA400)[5] = 0;
#ifdef MD_VERIFY
	/* verifier state, ALL of it in the free WRAM block (first cut put
	 * the tally at 0xFFB0EA, which the palette-scan span max already
	 * writes — the same single-writer trap as SPAN_PROBE v1, caught
	 * the same day). Layout:
	 *  [0] valid  [1] vram addr  [2] word idx  [3] wrote  [4] read
	 *  [5] HV     [6] mismatch tally
	 *  [7] stale packets (seq == last: the bank-skew re-read)
	 *  [8] seq jumps (seq != last+1 and != last)
	 *  [9] packets consumed */
	for (uint16_t i = 0; i < 10; i++)
		((volatile uint16_t*)0xFFA000)[i] = 0;
#endif
#ifdef MDSPR
	/* per-scene upload state lives in WRAM (0xFFA0DC..E0) — boot RAM
	 * is random and a garbage word count would pump a bogus upload */
	mdspr_up_left = mdspr_up_woff = mdspr_up_voff = 0;
	(*(volatile uint16_t*)0xFFA0DA) = 0;     /* uploads diag */
#endif
}

__attribute__((section(".data")))
static void md_bg_testpattern(void) {
	for (uint16_t row = 0; row < 28; row++) {
		uint32_t a = 0xE000u + (uint32_t)row * 128u;   /* 64-cell stride */
		*vdp_ctrl_wide = ((0x4000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
		for (uint16_t col = 0; col < 40; col++) {
			/* SLICE 1b: show VRAM slot N in cell N, so the plane is a
			 * tile SHEET of whatever the SH-2 has shipped so far. A
			 * correct transport paints recognisable Altered Beast
			 * artwork; a broken one paints noise, and the difference
			 * needs no interpretation. */
			*vdp_data_port = (uint16_t)(row * 40 + col);
		}
	}
}
#endif

#ifdef MD_BG
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
#endif

__attribute__((section(".data")))
#ifdef MD_BG
/* PACKET CONSUME, extracted (LOOP 23 v4): called post-window as
 * always, and ALSO pre-window on k2 vints under FM_GATE — the k1
 * window (spin-free, runs mid-gap) rebuilds the single packet
 * buffer mid-frame, and its packet's only FM=0 shim moment is the
 * NEXT vint's part A. The guard makes the k1-vint call a no-op
 * (window just raised); re-consume is harmless by design. */
#ifdef MDSPR
/* P3 M2: consume the SH-2's SAT + sprite-palette blocks — two DMAs
 * from the FB packet hole, beside the existing consumes, FM=0 in
 * vblank. SAT: 256 words 0x85EE00 -> VRAM 0xF000. Palette: 15 words
 * 0x85EDC2 -> CRAM entries 1-15 (entry 0, the backdrop, untouched).
 * Autoinc forced to 2 first — the hscroll DMA leaves 32 behind on
 * some paths. */
/* PER-SCENE ART UPLOAD (docs/design/BOSSFIGHT.md): chunk state armed by the
 * 0xBA50|scene consume; drained 512 words/vint below. Bank-switching
 * the 0x900000 window inside the vint is safe — the game only runs
 * outside the handler — and the window is restored to bank 3 before
 * the handler returns. */

static void mdspr_upload_pump(void) {
#ifdef MDCONSUME_OFF
	/* SESSION 7 CALIBRATION: after the boot/attract loads (vint 900),
	 * no MD-plane / sprite / art upload at all (the VDP planes and
	 * SAT freeze). Sizes the 68K lines the FB-sourced DMAs cost. */
	if (*(volatile uint16_t*)0xFFB0F0 >= 900) return;
#endif
	if (!mdspr_up_left)
		return;
	if (*(volatile uint16_t*)0xA15100 & 0x8000)
		return;                              /* FM=1: not our bus */
	uint16_t n = mdspr_up_left > 512 ? 512 : mdspr_up_left;
	const volatile uint16_t *src = (const volatile uint16_t*)
		(0x900000ul + MDSPR_CART_WINOFF) + mdspr_up_woff;
	uint32_t va = (uint32_t)MDSPR_VRAM_BASE + ((uint32_t)mdspr_up_voff << 1);
	*(volatile uint16_t*)0xA15104 = MDSPR_CART_BANK;
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8F02;
	*vdp_ctrl_wide = ((uint32_t)(0x4000u | (va & 0x3FFFu)) << 16)
	                 | ((va >> 14) & 3u);
	for (uint16_t i = 0; i < n; i++)
		*vdp_data_port = src[i];
	*(volatile uint16_t*)0xA15104 = 3;
	mdspr_up_woff += n;
	mdspr_up_voff += n;
	mdspr_up_left -= n;
}

static void mdspr_consume(void) {
#ifdef MDCONSUME_OFF
	/* SESSION 7 CALIBRATION: after the boot/attract loads (vint 900),
	 * no MD-plane / sprite / art upload at all (the VDP planes and
	 * SAT freeze). Sizes the 68K lines the FB-sourced DMAs cost. */
	if (*(volatile uint16_t*)0xFFB0F0 >= 900) return;
#endif
	*(volatile uint16_t*)0xFFA092 = *(volatile uint16_t*)0xC00008;   /* V at sprite-pal write */
#ifdef FM_GATE
	if (*(volatile uint16_t*)0xA15100 & 0x8000)
		return;
#endif
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8F02;
	{
		uint32_t src = 0x85EE00uL >> 1;
		*(volatile uint16_t*)VDP_CTRL_PORT = 0x9380;  /* 128 words:
		                                 * 32 SAT entries; 32-79 stay
		                                 * behind the link-0 stop */
		*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
		*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
		*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
		*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
		*vdp_ctrl_wide = ((uint32_t)(0x4000u | 0x3000u) << 16) | (3u | 0x80u);
	}
	if ((*(volatile uint16_t*)0xC00008 >> 8) >= 0xE0u)   /* vblank only: a CRAM
	                                                       * write in the picture
	                                                       * is a DAC dot; the
	                                                       * block re-ships every
	                                                       * frame, so skip */
	{
		uint32_t src = 0x85EDC2uL >> 1;
		*(volatile uint16_t*)VDP_CTRL_PORT = 0x930F;
		*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
		*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
		*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
		*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
		*vdp_ctrl_wide = ((uint32_t)0xC002u << 16) | 0x80u;
	}
}
#endif

/* BLANK MODE (2026-09-06): the SH-2 publishes bit 13 of packet word 1
 * while its display gate holds; the MD display stays OFF for as long
 * as the game blanks OR the SH-2 holds, so the vint consume DMA keeps
 * the blank-rate VDP access (the reverted batch-40 ran at the active
 * rate because the hold left the MD display ON). 0 = no packet seen
 * this vint (keep), 1 = seen without hold, 2 = seen with hold. */
static uint8_t md_hold_seen, md_hold;
#ifdef BOOT_PKTCHK
static uint16_t bm_pushed;               /* probe: last word 20 as pushed */
#endif
/* RAMCODE (2026-09-06): the consume ran from cart ROM under the master's
 * compose traffic (nm: 0x8c0b48) — a 4-6x fetch-stall on every
 * instruction; consume B measured 14 lines for a 280-word chunk. */
__attribute__((section(".data"), noinline))
static void md_consume(uint32_t pkt_base) {
#ifdef MDCONSUME_OFF
	/* SESSION 7 CALIBRATION: after the boot/attract loads (vint 900),
	 * no MD-plane / sprite / art upload at all (the VDP planes and
	 * SAT freeze). Sizes the 68K lines the FB-sourced DMAs cost. */
	if (*(volatile uint16_t*)0xFFB0F0 >= 900) return;
#endif
#ifdef FM_GATE
	if (*(volatile uint16_t*)0xA15100 & 0x8000) {
		(*(volatile uint16_t*)0xFFA0F2)++;   /* diag: consume skipped, FM still up */
	}
	if (*(volatile uint16_t*)0xA15100 & 0x8000)
		return;
#endif
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
#ifdef R60
			uint16_t r60_isB = (pkt_base == 0x85E800uL);
			if (r60_isB) *(volatile uint16_t*)0xFFA08E =
				*(volatile uint16_t*)0xC00008;    /* B: entry */
#endif
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
#ifdef R60
				/* per-buffer census (0xFFA076+, free): who consumes,
				 * carrying what */
				if (pkt_base == 0x851A00uL) {
					(*(volatile uint16_t*)0xFFA076)++;
					*(volatile uint16_t*)0xFFA078 = live[1];
				} else {
					(*(volatile uint16_t*)0xFFA07A)++;
					*(volatile uint16_t*)0xFFA07C = live[1];
				}
#endif
#if defined(K2_FREE) || defined(R60)
				/* consumed-mark (LOOP24 lossless handshake): zero the
				 * magic so the SH-2's publish can tell consumed from
				 * pending and DEFER instead of overwriting. FM=0 here
				 * by the guard above, so the FB write lands. Cleared
				 * FIRST: a re-entry mid-consume then skips cleanly.
				 * (R60: without this the MD plane re-consumed one boot
				 * packet forever — tiles 1046, chunks 0.) */
				live[0] = 0;
#endif
#ifdef MD_VERIFY
				// SEQ FRESHNESS (LOOP 13 tick-row): sc[7] was always
				// written by the SH-2 and never read here — the "tear
				// detector" was never wired. The packet lives in the
				// PER-BANK FB dead block; a stale-bank read re-applies
				// the previous generation's CELLS after the plane
				// scrolled = mixed-generation nametable = the sky
				// speckle band (ares VRAM decode vs MAME, 2026-08-12).
				// This counts how often it actually happens.
				{
					volatile uint16_t *vw = (volatile uint16_t*)0xFFA000;
					static uint16_t last_seq;
					uint16_t sq = sc[7];
					vw[9]++;
					if (sq == last_seq)          vw[7]++;   // stale re-read
					else if (sq != (uint16_t)(last_seq + 1)) vw[8]++;
					last_seq = sq;
				}
#endif
				// LOOP15 (NT_WRAP): sc[1] bit15 = "palette block changed
				// this window"; the type lives in the low byte.
#ifdef R60
				if (r60_isB) *(volatile uint16_t*)0xFFA090 =
					*(volatile uint16_t*)0xC00008;   /* B: post-census */
#endif
				uint16_t typ = (uint16_t)(sc[1] & 0xFF), cnt = sc[5];
				if (sc[1] & 0x2000u) md_hold_seen = 2;
				else if (!md_hold_seen) md_hold_seen = 1;
#ifdef NT_WRAP
				uint16_t palp = (uint16_t)(sc[1] & 0x8000u);
#endif
				if (typ == 0) (*(volatile uint16_t*)0xFFB0E4)++;   // tile batches
				else          (*(volatile uint16_t*)0xFFB0E6)++;   // name chunks
				(*(volatile uint16_t*)0xFFB0E8) = sc[5];  // last count
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
#if defined(R60) && defined(NT_WRAP)
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
#ifdef TILE_VERIFY
				uint16_t tv_fs0 = *(volatile uint16_t*)0xA1510A;
				*(volatile uint16_t*)0xFFA1EC = sc[1];   /* SH-2 verdict bits 8-12 */
#endif
				(*(volatile uint16_t*)0xFFB0B2) =
					*(volatile uint16_t*)0xC00008;
				*vdp_ctrl_wide = ((uint32_t)(0x4000u | 2u) << 16) | 0x10u;
				*vdp_data_port = sc[4];           /* VSRAM 2 = plane B vy */
				*vdp_ctrl_wide = ((uint32_t)0x4000u << 16) | 0x10u;
				*vdp_data_port = sc[6];           /* VSRAM 0 = plane A vy */
				if (typ == 0) {
					/* tile pixels: 16 contiguous FB words per record */
					volatile uint16_t *e = sc + 8;
					for (uint16_t i = 0; i < cnt; i++, e += 17) {
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
#ifdef DMA_CENSUS
						(*(volatile uint16_t*)0xFFA246)++;      /* tile-record DMAs */
						(*(volatile uint32_t*)0xFFA248) += 16;  /* words */
#endif
					}
					(*(volatile uint16_t*)0xFFB0B4) =
						*(volatile uint16_t*)0xC00008;
#ifdef TILE_VERIFY
					/* LOOP29 231 rig instrument: the FPGA shows whole
					 * tile sets black where ares shows art (vi75 attract
					 * demo, 195726). Read every record's 16 VRAM words
					 * back right after its DMA and classify:
					 *   0xFFA1E0 records checked   0xFFA1E2 VRAM all-zero
					 *   0xFFA1E4 VRAM != FB source 0xFFA1E6 FB source all-zero
					 * (the source re-read at FM=0 -- if the DMA read a
					 * different bank than this re-read, E4 counts it). */
					e = sc + 8;
					for (uint16_t i = 0; i < cnt; i++, e += 17) {
						uint32_t va = (uint32_t)e[0] * 32u;
						if (va + 32u > 0xB000u) {
							(*(volatile uint16_t*)0xFFA1EA)++;   /* slot out of range */
							continue;
						}
						uint16_t vz = 1, sz = 1, mm = 0;
						*vdp_ctrl_wide = ((uint32_t)(va & 0x3FFFu) << 16)
							| ((va >> 14) & 3u);
						for (uint16_t k = 0; k < 16; k++) {
							uint16_t v = *vdp_data_port, s = e[1 + k];
							if (v) vz = 0;
							if (s) sz = 0;
							if (v != s) mm = 1;
						}
						(*(volatile uint16_t*)0xFFA1E0)++;
						if (vz) {
							(*(volatile uint16_t*)0xFFA1E2)++;
							/* vi81: where in the packet the zeros sit */
							*(volatile uint16_t*)0xFFA1EE = i;
							*(volatile uint16_t*)0xFFA1F0 = cnt;
						}
						if (mm) (*(volatile uint16_t*)0xFFA1E4)++;
						if (sz) (*(volatile uint16_t*)0xFFA1E6)++;
					}
					/* 0xFFA1E8: consumes where the 68K's FB bank (FS,
					 * 0xA1510A bit 0) changed between entry and here --
					 * a flip landing while the tile DMAs read the FB. */
					if ((tv_fs0 ^ *(volatile uint16_t*)0xA1510A) & 1u)
						(*(volatile uint16_t*)0xFFA1E8)++;
#endif
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
					for (uint16_t r = 0; r < 7; r++) {
						uint16_t hdr = *e++;
						uint16_t w1  = *e++;
						uint16_t c0 = hdr & 63u;
						uint32_t rb = nbase
							+ (uint32_t)((hdr >> 8) & 31u) * 128u;
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
#ifdef DMA_CENSUS
							/* DMA CENSUS (LOOP27 48). Entry 47 moved the
							 * suspect from "the 68K reads the FB" (false)
							 * to "the FB-sourced DMAs are the cost". A DMA
							 * costs six register writes plus the transfer,
							 * so MANY SHORT spans are much worse than few
							 * long ones — and nothing has ever counted
							 * them. 0xFFA240 spans, 0xFFA242 words. */
							(*(volatile uint16_t*)0xFFA240)++;
							(*(volatile uint32_t*)0xFFA242) += l1;
#endif
							e += l1;
							st = (uint16_t)(st + l1);
							nc2 = (uint16_t)(nc2 - l1);
						}
#ifdef EDGE42
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
#endif
					}
					*(volatile uint16_t*)0xFFA08A =
						*(volatile uint16_t*)0xC00008;   /* V after spans */
					*(volatile uint16_t*)0xFFA170 = *(volatile uint16_t*)0xC00008;   /* fine: spans done */
					{	/* full-screen hscroll: reg 0x0B = 00 (init), so only
						 * 0xFC00 (A) and 0xFC02 (B) matter — two header words,
						 * sc[3] and sc[7], in EVERY packet (2026-09-05). The 56-
						 * word per-strip DMAs wrote a table the VDP ignored. */
#ifdef MD_HSCROLL_DIRECT
						/* MDHSCR phase 2 (LOOP-DECOMPILE 26): the GAME writes
						 * both entries itself, from its own scroll stores in
						 * IRQ4, one vint fresher than this packet. Doing it
						 * again here would only overwrite the newer value with
						 * the older one. sc[3]/sc[7] are still carried; only
						 * this write is dropped. */
#else
						*vdp_ctrl_wide = ((uint32_t)(0x4000u | 0x3C00u) << 16) | 3u;
						*vdp_data_port = sc[3];
						*vdp_data_port = sc[7];
#endif
					}
#ifdef ART_TAIL
					if (sc[1] & 0x4000u) {   /* art tail: [n] n x [slot][16 words] */
						uint16_t na = *e++;
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
#endif
					(*(volatile uint16_t*)0xFFB0B6) =
						*(volatile uint16_t*)0xC00008;
				}
#ifdef NT_WRAP
				if (palp)
#endif
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
#ifdef BOOT_PALPEEK
					/* WHAT IS ACTUALLY IN THE PALETTE BLOCK? (2026-09-08,
					 * LOOP27 32). Forcing the palette through WRAM instead
					 * of the FB-sourced DMA changed NOTHING on hardware
					 * (entry 31), and those two paths read the framebuffer
					 * by completely different means — 68K reads vs VDP DMA.
					 * Both wrong says the DATA is wrong, not the transport.
					 * So stop inferring and READ IT, the discipline that
					 * worked for the framebuffer (s16_abread) and for rom
					 * identity (BOOTTAGBLUE).
					 * The 68K peeks the 48 words the master wrote at
					 * offset 688 and paints the verdict on the MD backdrop,
					 * which nothing can gate:
					 *   RED    = all 48 words ZERO. The master never wrote
					 *            the palette block, or the 68K cannot read
					 *            it on this core.
					 *   YELLOW = all 48 identical and non-zero. Landed, but
					 *            it is a fill pattern, not a palette.
					 *   GREEN  = varied non-zero, i.e. a plausible palette.
					 *            Then the data IS there and correct-ish,
					 *            and the fault is downstream in the upload.
					 * Note the geometry (name tables, earlier in the same
					 * packet) is CORRECT on hardware, so a red/yellow here
					 * means the packet is good early and bad deep. */
#ifdef BOOT_PALRAMP
					/* RAMP VERDICT (LOOP27 36). The master wrote
					 * 0x0100+i into the 48 palette words. Check what
					 * actually arrived:
					 *   GREEN  = exact match, every word. The transport
					 *            is clean and the fault is the SOURCE
					 *            palette the master builds from, i.e.
					 *            upstream over DREQ.
					 *   YELLOW = the ramp is there but SHIFTED by k
					 *            words. A landing alignment slip — the
					 *            documented partial-DREQ hazard — and the
					 *            shift amount is encoded in the green
					 *            level so k is readable off the screen.
					 *   RED    = neither: the words are corrupted, not
					 *            merely displaced. */
					{
						uint16_t col;
						uint16_t exact = 1;
						for (uint16_t i = 0; i < 48; i++)
							if (sc[688 + i] != (uint16_t)(0x0100 + i)) { exact = 0; break; }
						if (exact) {
							col = 0x00E0;                 /* GREEN */
						} else {
							int k = 0;
							for (k = -8; k <= 8; k++) {
								if (!k) continue;
								uint16_t ok = 1;
								for (uint16_t i = 8; i < 40; i++)
									if (sc[688 + i] != (uint16_t)(0x0100 + i + k)) { ok = 0; break; }
								if (ok) break;
							}
							if (k >= -8 && k <= 8 && k != 0)
								col = (uint16_t)(0x00E0 | ((k < 0 ? -k : k) & 7) << 1);  /* YELLOWish, k in red */
							else
								col = 0x000E;             /* RED */
						}
						*(volatile uint16_t*)0xFFA168 = col;
					}
#else
					{
						uint16_t z = 0, same = 1, w0 = sc[688];
						for (uint16_t i = 0; i < 48; i++) {
							uint16_t v = sc[688 + i];
							if (v) z = 1;
							if (v != w0) same = 0;
						}
						uint16_t col = !z ? 0x000E          /* RED   */
									 : same ? 0x00EE        /* YELLOW*/
									        : 0x00E0;       /* GREEN */
						/* STASH ONLY. Painting here is useless: the
						 * real 48-word palette upload runs immediately
						 * after and overwrites entries 16-63, which is
						 * exactly what the MD plane draws with. ares
						 * showed this — the flood left only the border
						 * green. The flood happens at VINT TOP instead,
						 * the site BOOTMDPAL proved covers the screen. */
						*(volatile uint16_t*)0xFFA168 = col;
#ifdef BOOT_PALSHOW
						/* SHOW THE VALUE, NOT A VERDICT (LOOP27 34).
						 * palpeek came back GREEN on hardware: the 48
						 * words are varied non-zero, a PLAUSIBLE palette.
						 * So the data is there and the 68K can read it —
						 * but "plausible" is not "correct", and the
						 * screen is magenta. The next question is whether
						 * those words ARE the magenta.
						 * Stash the raw palette word for CRAM entry 17
						 * (offset 688+1, the sky pen) and flood the
						 * screen with it at vint top. If hardware floods
						 * MAGENTA, the packet itself carries the wrong
						 * colour and the fault is upstream of the 68K
						 * entirely — in what the master wrote, hence in
						 * the palette the master received over DREQ. If
						 * it floods the arcade's blue, the words are
						 * right and the upload mangles them. */
						/* Index 1 was a bad pick: sc[689] reads 0x0000
						 * on ares too (CRAM 17 is legitimately black
						 * there), so it would have flooded nothing and
						 * cost another round trip. Take the FIRST
						 * NON-ZERO word instead — robust without knowing
						 * which pen the scene actually uses. */
						{
							uint16_t sh = 0;
							for (uint16_t i = 0; i < 48; i++)
								if (sc[688 + i]) { sh = sc[688 + i]; break; }
							*(volatile uint16_t*)0xFFA168 = sh;
						}
#endif
					}
#endif
#endif
					uint16_t vnow = *(volatile uint16_t*)0xC00008;
					*(volatile uint16_t*)0xFFA090 = vnow;              /* V at pal DMA */
#ifdef BOOT_CRAMCHK
					/* DID THE PALETTE UPLOAD ACTUALLY LAND IN CRAM?
					 * (2026-09-08, LOOP27 46.) Read the value, do not
					 * infer it from the picture — the rule that has
					 * worked every time tonight.
					 * Stash the previous vint's readback verdict first
					 * (this runs BEFORE this vint's upload, so it grades
					 * the upload that already happened). MD CRAM read:
					 * command ((addr & 0x3FFF) << 16) | 0x20, then read
					 * the data port. Mask to 0x0EEE — the 9 valid BGR
					 * bits — because the unused bits read back
					 * undefined. */
					{
						static uint16_t verdict;
						volatile uint16_t *shadow = (volatile uint16_t*)0xFFA1C0;
						volatile uint16_t *valid  = (volatile uint16_t*)0xFFA1BE;
						uint16_t match = 0;
						/* grade against WHAT WAS ACTUALLY LAST SENT, held in
						 * a WRAM shadow — not against this vint's packet.
						 * CRAM holds the PREVIOUS upload, and under NT_WRAP
						 * the palette only uploads when it changed, so
						 * comparing to the current packet mismatches for
						 * reasons that have nothing to do with the transfer.
						 * That flaw made the first cut read 5/48 on ares,
						 * where the picture is plainly correct. */
						if (*valid) {
							*(volatile uint16_t*)VDP_CTRL_PORT = 0x8F02;
							*vdp_ctrl_wide = ((uint32_t)32u << 16) | 0x0002u;   /* CRAM READ */
							for (uint16_t i = 0; i < 48; i++) {
								uint16_t got = (uint16_t)(*vdp_data_port & 0x0EEE);
								if (got == (uint16_t)(shadow[i] & 0x0EEE)) match++;
							}
						}
						for (uint16_t i = 0; i < 48; i++) shadow[i] = sc[688 + i];
						*valid = 1;
						verdict = (match == 48) ? 0x00E0      /* GREEN all 48 */
								: (match >  0)  ? 0x00EE      /* YELLOW some  */
								                : 0x000E;     /* RED none     */
						*(volatile uint16_t*)0xFFA168 = verdict;
						*(volatile uint16_t*)0xFFA16A = match;  /* readable count */
					}
#endif
#ifdef BOOT_PALDIRECT
					/* DMA-TO-CRAM vs DIRECT CRAM WRITES (2026-09-08,
					 * LOOP27 45). Colour census of every MiSTer capture
					 * this session: the hardware has NEVER shown more
					 * than 9 distinct colours, while ares shows 76-92 on
					 * the identical rom. So almost every palette entry is
					 * black on hardware.
					 * But the probe FLOODS — 64 CRAM entries written one
					 * at a time straight to the VDP data port — DO land;
					 * they cover the screen every time. And the geometry
					 * is correct, which is DMA TO VRAM working.
					 * The one path that is neither is the palette upload:
					 * a VDP DMA whose DESTINATION IS CRAM. Hypothesis:
					 * DMA-to-CRAM does not land on this core (or lands
					 * truncated) while DMA-to-VRAM and direct CRAM writes
					 * both do.
					 * Test AND candidate fix: write the 48 words with
					 * direct stores instead of a DMA. 48 word writes is
					 * cheap — the flood already does 64 every vint. */
					{
						*(volatile uint16_t*)VDP_CTRL_PORT = 0x8F02;   /* autoinc 2 */
						*vdp_ctrl_wide = ((uint32_t)(0xC000u | 32u) << 16) | 0x80u;
						for (uint16_t i = 0; i < 48; i++)
							*vdp_data_port = sc[688 + i];
					}
					if (0)
#endif
#ifdef BOOT_PALWRAM
					/* FB-SOURCED DMA BYPASS (2026-09-08, LOOP27 31). The
					 * MD background palette normally reaches CRAM by a
					 * Mega Drive VDP DMA whose SOURCE IS THE 32X
					 * FRAMEBUFFER (sc + 688). On the MiSTer the MD plane
					 * draws correct geometry with a WRONG PALETTE, and
					 * "does this core serve VDP DMA from the framebuffer"
					 * has been an untested suspect since the start of the
					 * arc. The deferred path beside this one already
					 * copies the 48 words to WRAM (0xFFA100) and DMAs
					 * them from there at the next vint top — a path that
					 * never touches the FB as a DMA source. Force it
					 * always: if the colours come right on hardware, the
					 * FB-sourced DMA is the fault. */
					if (0) {
#else
					if ((vnow >> 8) >= 0xE0u) {
#endif
						uint32_t src = ((uint32_t)(sc + 688)) >> 1;
						*(volatile uint16_t*)VDP_CTRL_PORT = 0x9330;
						*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
						*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
						*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
						*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
						*vdp_ctrl_wide = ((uint32_t)(0xC000u | 32u) << 16) | 0x80u;
					} else {
						volatile uint16_t *pb = (volatile uint16_t*)0xFFA100;  /* 48-word hold */
						for (uint16_t i = 0; i < 48; i++) pb[i] = sc[688 + i];
						*(volatile uint16_t*)0xFFA160 = 1;                 /* deferred flag */
						(*(volatile uint16_t*)0xFFA162)++;                 /* deferrals */
					}
				}
				}
#else  /* legacy staged consume */
				{
				volatile uint16_t *stg = (volatile uint16_t*)0xFFA400;
				volatile uint16_t *sp = stg + 8;
				uint16_t nrec = 0;
				stg[1] = sc[4];               // VSRAM 2 = plane B vy
				stg[2] = sc[6];               // VSRAM 0 = plane A vy
				stg[5] = 1;
				(*(volatile uint16_t*)0xFFB0B2) =
					*(volatile uint16_t*)0xC00008;    // V after scroll stage
				if (typ == 0) {
					// each entry: slot word + 32 bytes of 4bpp planar
					volatile uint16_t *e = sc + 8;
					for (uint16_t i = 0; i < cnt; i++, e += 17) {
						uint32_t va = (uint32_t)e[0] * 32u;
						if (va + 32u > 0xB000u) continue;
						sp[0] = 16;
						sp[1] = (uint16_t)(0x4000u | (va & 0x3FFFu));
						sp[2] = (uint16_t)(((va >> 14) & 3u) | 0x80u);
						for (uint16_t k = 1; k < 17; k++)
							sp[2 + k] = e[k];
						sp += 19; nrec++;
					}
					(*(volatile uint16_t*)0xFFB0B4) =
						*(volatile uint16_t*)0xC00008; // V after tiles
				} else {
#ifdef NT_WRAP
					// LOOP15 wrap protocol PHASE B: mirror-diffed rows.
					// Per row: [hdr=(prow<<8)|c0][(start<<8)|count]
					// [count entries] — cells land at wrapped plane
					// columns (span may split at the 64-column seam).
					// Then the all-strips hscroll delta tail:
					// [n] n x [(plane<<7)|strip, value] (cell-strip
					// mode, table entries 32 bytes apart at VRAM
					// 0xFC00; A word +0, B word +2).
					volatile uint16_t *e = sc + 8;
					uint16_t isa = (uint16_t)(sc[2] & 0x8000u ? 1 : 0);
					uint32_t nbase = isa ? 0xC000u : 0xE000u;
					for (uint16_t r = 0; r < 7; r++) {
						uint16_t hdr = *e++;
						uint16_t w1  = *e++;
						uint16_t c0 = hdr & 63u;
						uint32_t rb = nbase
							+ (uint32_t)((hdr >> 8) & 31u) * 128u;
						uint16_t st = (uint16_t)((w1 >> 8) & 0x7F);   /* bit 15: EDGE42 pair follows */
						uint16_t nc2 = (uint16_t)(w1 & 0xFF);
						while (nc2) {
							uint16_t pc = (uint16_t)((c0 + st) & 63u);
							uint16_t l1 = (uint16_t)(64u - pc);
							if (l1 > nc2) l1 = nc2;
							uint32_t a = rb + (uint32_t)pc * 2u;
							sp[0] = l1;
							sp[1] = (uint16_t)(0x4000u | (a & 0x3FFFu));
							sp[2] = (uint16_t)(((a >> 14) & 3u) | 0x80u);
							for (uint16_t c = 0; c < l1; c++)
								sp[3 + c] = e[c];
							sp += 3 + l1; nrec++;
							e += l1;
							st = (uint16_t)(st + l1);
							nc2 = (uint16_t)(nc2 - l1);
						}
#ifdef EDGE42
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
#endif
					}
					{	// hscroll delta tail
						uint16_t nh = *e++;
						while (nh--) {
							uint16_t w0 = *e++;
							uint32_t ha = 0xFC00u
								+ (uint32_t)(w0 & 0x7Fu) * 32u
								+ ((w0 & 0x80u) ? 0u : 2u);
							sp[0] = 1;
							sp[1] = (uint16_t)(0x4000u | (ha & 0x3FFFu));
							sp[2] = (uint16_t)(((ha >> 14) & 3u) | 0x80u);
							sp[3] = *e++;
							sp += 4; nrec++;
						}
					}
					stg[3] = 0;               // no full-screen hscroll
					(void)cnt;
#else
					// name-table cells, 40 per screen row, 64-cell stride.
					// sc[2] bit 15 = PLANE A (FG cat-0) chunk; else B.
					// ROW-MAJOR: one division per packet (the per-cell
					// DIVU pair measured 129 scanlines per chunk).
					volatile uint16_t *e = sc + 8;
					uint16_t cell0 = sc[2] & 0x7FFF;
					uint32_t nbase = (sc[2] & 0x8000u) ? 0xC000u : 0xE000u;
					uint16_t row = cell0 / 40u;
					uint16_t i = 0;
					for (uint16_t r = 0; r < 7 && i < cnt; r++, row++) {
						uint32_t a = nbase + (uint32_t)row * 128u;
						uint16_t wl = (uint16_t)((cnt - i) < 40u
						                         ? (cnt - i) : 40u);
						sp[0] = wl;
						sp[1] = (uint16_t)(0x4000u | (a & 0x3FFFu));
						sp[2] = (uint16_t)(((a >> 14) & 3u) | 0x80u);
						for (uint16_t c = 0; c < wl; c++, i++)
							sp[3 + c] = e[i];
						sp += 3 + wl; nrec++;
					}
					// full-screen hscroll from the chunk's first strip
					// word: plane A word at 0xFC00, plane B at 0xFC02
					stg[3] = (uint16_t)((sc[2] & 0x8000u) ? 0x3C00u
					                                      : 0x3C02u);
					stg[4] = e[280];
#endif
					(*(volatile uint16_t*)0xFFB0B6) =
						*(volatile uint16_t*)0xC00008; // V after cells
				}
				// live BG palette: 48 words at fixed offset 688 -> CRAM
				// lines 1-3 (entries 16-63); line 0 stays the grey/text
				// ramp. NT_WRAP: staged only when the SH2 flagged a
				// change (fades still track at window cadence; static
				// scenes save 48 slow FB reads + the DMA record).
#ifdef NT_WRAP
				if (palp)
#endif
				{
					sp[0] = 48;
					sp[1] = (uint16_t)(0xC000u | 32u);
					sp[2] = 0x80u;
					for (uint16_t i = 0; i < 48; i++)
						sp[3 + i] = sc[688 + i];
					nrec++;
				}
				stg[0] = nrec;
				}
#endif  /* R60 direct vs legacy staged */
				(*(volatile uint16_t*)0xFFB0B8) =
					*(volatile uint16_t*)0xC00008;    // V after CRAM stage
				*(volatile uint16_t*)0xFFA172 = *(volatile uint16_t*)0xC00008;   /* fine: after pal */
#ifdef R60
				if (r60_isB) *(volatile uint16_t*)0xFFA092 =
					*(volatile uint16_t*)0xC00008;   /* B: pre-clear */
#endif
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
#ifdef R60
						if (d > 16) (*(volatile uint16_t*)0xFFA096)++;
						if (d > 28) (*(volatile uint16_t*)0xFFA098)++;
						if (d > 16 && r60_isB)
							(*(volatile uint16_t*)0xFFA09A)++;
						if (d > 16)             /* worst offender's typ+cnt */
							*(volatile uint16_t*)0xFFA09C =
								(uint16_t)((sc[1] << 8) | (sc[5] & 0xFF));
#endif
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
#endif

#if defined(K2_FREE) && !defined(FM_GATE)
#error K2_FREE builds on FM_GATE (the no-spin k1 path is its skeleton)
#endif
#if defined(R60) && !defined(FM_GATE)
#error R60 needs FM_GATE (writer gates + consume guard + belt)
#endif

#ifdef R60
/* REBUILD — THE ONE PUSH (R60 family, packet_fmt.h). Runs at vint
 * entry, BEFORE the window post: the master's V-ISR armed on the
 * COMM6 announce and its body will WAIT for this landing before its
 * FM span — the push drains against an idle master (the LOOP25 FIFO
 * verdict inverted into a design rule). Per-group polling with the
 * spin-guarded write (never write the FIFO after exhaustion). */
#ifdef PAL_DELTA
/* v3 (PALDELTA) force-raw mask, file-scope: the BAD1 echo handler
 * (vint block) sets bits too. Boots ALL-SET so the first ship of
 * every block is raw (= the v2 boot storm, shadow synced as a side
 * effect). Re-set on tears — a torn packet leaves the shadow
 * claiming words the SH-2 never applied, and a delta re-ship
 * against a lying shadow is permanent stale colour. */
static uint8_t pal_force[8] =
	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
#ifdef GLOW_MASK
/* SH-2 mask grant (0xBAD3 off / 0xBAD4 on): mask the glow blocks
 * ONLY while the animator says it is running. Boots OFF, so before
 * the first grant (and in glow-less scenes, where re-seed keeps
 * failing) every glow write ships at full fidelity. */
static uint8_t glow_live;
#endif

/* SHIP LOOP, 68000-SHAPED (unattended slice 2): the C R60P macro
 * measured ~300 cy/word against a ~140 floor (adapter calibration:
 * ~54 cy/access, two accesses/word — the poll-per-word discipline
 * is LAW, Mike's play pass 2026-08-22, and the asm keeps it: tst.b
 * precedes every write; only the ENCODING tightens). The belt is
 * chunk-level here instead of per-word — on a wedged FIFO up to the
 * remaining words of one call go to a full FIFO, which ares DROPS
 * (measured, drq_probe): harmless, the packet is torn anyway and
 * the BAD1 echo re-marks. Returns words NOT shipped (0 = clean). */
#ifdef FB_XPORT
/* FB TRANSPORT (LOOP27 67): the same packet, same layout, same order —
 * only the destination changes. Keeping r60_push's builder untouched is
 * the point: the master's harvest parses the identical bytes and every
 * length, tag and tear rule downstream still holds. */
static uint16_t fbx_i;                   /* word cursor into the FB packet */
static uint8_t  fbx_seq_pub;             /* publish sequence */
#ifdef FBX_STAGE
/* STAGE THE PACKET IN WRAM, BLAST IT AT THE TAIL (LOOP28 89).
 *
 * The bind (HANDOFF-PIPELINE section 3): the 68K cannot touch the
 * framebuffer at FM=1, so today the WHOLE packet build — rotor, palette
 * compare, record packing, ~56 scanlines of it — has to sit in an FM=0
 * window. Before the post it drives V-at-post to 43 and the flip dies
 * (0.3 Hz); at the tail (FBX_TAIL) it runs ahead of the game's IRQ4 and
 * costs 33 points of game speed.
 *
 * Neither placement is wrong about the window. What is wrong is that the
 * BUILD is in it at all: only the FB WRITES need FM=0, and they are ~2
 * lines of the 56. So the build writes into WRAM, where FM does not
 * apply and it can overlap the master's blit, and a straight-line copy
 * moves it into the framebuffer in the FM=0 window at the tail.
 *
 * The buffer is R60_ARM words = 1872 bytes of the 15,916 free between
 * __bss_end and PAL_SHADOW (LOOP28 84); md.ld's ASSERT is the fence.
 * One vint of packet latency, which the harvest already tolerates: a
 * stale publish yields landed = 0 and last frame's records stand. */
static uint16_t fbx_stage[R60_ARM];      /* packet under construction */
static uint16_t fbx_stage_n;             /* words staged, 0 = nothing */
#ifdef TXT_MASK
#define txt_mask (*(volatile uint8_t*)0xFFA1A6)   /* LOOP29 147: 4-row groups the
                                                    * game's text writers touched */
#endif
#ifdef FBX_PEND
/* tail blast skipped (FM up): a WRAM word the generated gate spin
 * (patch_game.py, FMGATE_SPIN_ADDR) tests before calling fbx_late_blast
 * through the vector at 0xFFA0F8. 0xFFA0FC counts late blasts. */
#define fbx_pend (*(volatile uint16_t*)0xFFA0FE)
#endif
#ifdef FBX_BOTH
static uint8_t  fbx_stage_live;          /* the staged packet is worth
                                          * writing into the other bank */
#endif
#define FBX_DST  fbx_stage
#else
#define FBX_DST  ((volatile uint16_t*)FBX_PKT_MD)
#endif
__attribute__((section(".data"), noinline))
static uint16_t r60_ship_words(const uint16_t *src, uint16_t nw)
{
	volatile uint16_t *d = FBX_DST + fbx_i;
	for (uint16_t i = 0; i < nw; i++) d[i] = src[i];
	fbx_i = (uint16_t)(fbx_i + nw);
	return 0;                            /* an FB write cannot fall short */
}
#else
__attribute__((section(".data"), noinline))
static uint16_t r60_ship_words(const uint16_t *src, uint16_t nw)
{
	uint16_t i = 0;
	uint32_t belt = 40000uL;
	__asm volatile(
		"bra.s 2f\n"
		"1:\n\t"
		"tst.b (%[ctrl])\n\t"
		"bmi.s 3f\n\t"
		"move.w (%[src])+,(%[fifo])\n\t"
		"addq.w #1,%[i]\n"
		"2:\n\t"
		"cmp.w %[nw],%[i]\n\t"
		"bcs.s 1b\n\t"
		"bra.s 4f\n"
		"3:\n\t"
		"subq.l #1,%[belt]\n\t"
		"bne.s 2b\n"
		"4:\n"
		: [src] "+a" (src), [i] "+d" (i), [belt] "+d" (belt)
		: [ctrl] "a" ((volatile int8_t*)0xA15107),
		  [fifo] "a" ((volatile uint16_t*)0xA15112),
		  [nw] "d" (nw)
		: "cc", "memory");
	return (uint16_t)(nw - i);
}
#endif
#endif

/* RAMCODE (2026-08-30, the PALDELTA autopsy): r60_push was the ONE
 * piece of the vint path still EXECUTING FROM CART ROM — every
 * instruction fetch crossed the adapter to the cart bus, which the
 * master hammers all through its FM span (capture/restore + compose
 * reading sprite art). Stamps: the v2 selection block (a trivial
 * rotor) measured ~25 lines, the delta compare pre-pass ~65 — a
 * 4-6x fetch-stall multiplier on 68K work, invisible in MAME
 * (which charges instruction issue, not bus contention). noinline
 * keeps LTO from folding it back into ROM-resident shim_vblank. */
#ifdef SHIM_BURN
/* the same burn loop executing from the cart window: ROM fetch cost */
__attribute__((noinline, section(".text.burn"))) static void shim_burn_rom(void)
{
	volatile uint16_t bi;
	for (bi = 0; bi < (uint16_t)(SHIM_BURN * 40u); bi++) ;
}
#endif
#ifdef R60_TIGHT
/* R60_TIGHT (2026-09-12, LOOP-DECOMPILE 97): the two equality scans in
 * r60_push -- the 16-long palette pre-scan (502 instructions a vint at
 * its loop head, 23% of the routine) and the 30-long rowscroll compare
 * (170) -- compiled to 7 instructions / 64 cycles a long. cmpm.l + dbne
 * is 2 / 30. Returns the number of longs REMAINING from the first
 * mismatch (n - its index), 0 iff all equal -- the same value the C
 * `while (eq && *qa++ == *qb++) eq--` left in eq, so the mask walk can
 * start at the first difference (LOOP-DECOMPILE 102). What is shipped
 * does not change. always_inline keeps it in .data with its caller (a
 * .text copy would fetch from the cart under FM). */
static inline __attribute__((always_inline))
uint16_t r60_ne_longs(const uint32_t *a, const uint32_t *b, uint16_t n)
{
	uint16_t cnt = (uint16_t)(n - 1);
	__asm volatile(
		"1:\n\t"
		"cmpm.l (%[a])+,(%[b])+\n\t"
		"dbne %[cnt],1b\n\t"
		: [a] "+a" (a), [b] "+a" (b), [cnt] "+d" (cnt)
		:
		: "cc", "memory");
	return (uint16_t)(cnt + 1u);         /* dbne leaves -1 when exhausted */
}
#endif
__attribute__((section(".data"), noinline))
static void r60_push(void) {
	volatile uint16_t *fifo = (volatile uint16_t*)0xA15112;
	volatile int8_t  *ctrl = (volatile int8_t*)0xA15107;
	uint16_t spin = 2600;
	static uint8_t pal_retry[8];
	static uint8_t pal_next;
#ifdef LAYOUT_PROBE
	/* LOOP28 88 control: 64 bytes of unreferenced .data, present only to
	 * SHIFT the code below it. Changes no behaviour whatsoever. */
	static volatile uint8_t layout_probe_pad[64] = {1};
	(void)layout_probe_pad;
#endif
#ifdef PAL_DIET
	static uint8_t pal_streak[64];       /* consecutive equal compares
	                                      * per block — the chronic
	                                      * redundant writers (census:
	                                      * 91% of the 0x2628 streamer)
	                                      * earn a 4-vint visit backoff;
	                                      * the rotor+cmp stamp read
	                                      * 30-33 LINES/vint mostly
	                                      * re-proving equality */
#ifndef PAL_STREAK_N
#define PAL_STREAK_N 4               /* equal visits before backing off */
#endif
#ifndef PAL_BACKOFF_M
#define PAL_BACKOFF_M 3              /* visit 1 vint in (M+1) once backed off */
#endif
#endif	/* PAL_DIET */
	uint8_t ids[16];
	uint16_t K = 0, nrec = 1;
	volatile uint8_t *pd = (volatile uint8_t*)0xFFBA00;
	const uint16_t *s = (const uint16_t*)0xFF7000;
#ifdef PAL_DELTA
	uint16_t dmask[16][2];
	uint16_t palw = 0;                   /* pal payload words, excl ids */
#endif
	/* PUSH AUTOPSY (temporary, both arms): the delta arm cut the
	 * packet 145->52 words and the push span DID NOT MOVE (~100
	 * lines, spin residual 2600 = never FIFO-full). Delta stamps
	 * read: selection phase ~65 lines(!), ship phase 0.63 l/w (the
	 * old per-word rate). Both arms carry the stamps to split
	 * "ambient stall in that span" from "the compare pre-pass is
	 * expensive". Last-value HV at 0xFFA0B4..BC (grep'd free). */
#define PSTAMP(a) (*(volatile uint16_t*)(a) = *(volatile uint16_t*)0xC00008)
	PSTAMP(0xFFA0B4);                    /* entry */
#ifdef FB_XPORT
	fbx_i = 0;
#endif
	/* (busy-loop calibration retired: loop1 vblank 7 lines, loop2
	 * active-line-87 9 lines — both FULL SPEED, no ambient bus tax
	 * at either end of the mystery span. The 66 lines are the
	 * selection code's own execution.) */
#ifdef ADAPTER_CAL
	/* ADAPTER-ACCESS CALIBRATION (unattended slice, temporary): the
	 * ship runs ~310 cy/word against ~60-70 raw poll+write — split
	 * "C fat" (asm-izable) from "adapter access cost" (bus, not
	 * fixable) before writing any asm. 400 back-to-back DREQ-ctrl
	 * reads between B4 and BE: quiet cost would be ~8 lines at
	 * 10cy/read; every line above that is per-access adapter tax. */
	{
		volatile int8_t *cal = (volatile int8_t*)0xA15107;
		PSTAMP(0xFFA0B4);
		for (uint16_t i = 0; i < 400; i++) (void)*cal;
		PSTAMP(0xFFA0BE);
	}
#endif
	/* pal selection: rotor over dirty|retry, up to 16 blocks */
#ifdef SHIM_NOPUSH
	/* 68K-BUDGET PROBE: bare packet only (no pal rotor, no rowscroll,
	 * no records) — the SH-2 keeps landing and flipping, sprites
	 * freeze; the game's speed at S~30 lines is the measurement */
	if (0)
#endif
	{
		/* K CLAMP: 15 -> 4 -> 8. K=4 under-provisioned storms:
		 * ship-twice halves effective NEW-block drain to ~2/vint,
		 * under the LOOP25 census (mean 4.0 dirty/vint) — Mike's
		 * second pass: black player silhouettes + a ~60-frame
		 * miscolored level-1 load-in (the palette arriving over a
		 * second). K=8 = effective 4 new/vint = the census rate;
		 * worst-case push +128 words on storm frames only. */
		/* STORM ESCALATION (Mike pass 3: load-in shorter but still
		 * present): count total dirty first; past 24 blocks this is
		 * a LOAD storm, not a fade — open the clamp to the tag
		 * field's 15. The push runs AFTER the post, so a ~750-word
		 * storm push costs one frame of 68K time during a scene
		 * load (invisible), never a flip. 64 blocks at effective
		 * 7.5 new/vint converges in ~9 vints vs ~30. */
		uint16_t ndirty = 0;
		for (uint16_t b2 = 0; b2 < 8; b2++) {
			uint8_t m2 = (uint8_t)(pd[b2] | pal_retry[b2]);
			while (m2) { ndirty++; m2 &= (uint8_t)(m2 - 1); }
		}
		uint16_t kcap = (ndirty > 24) ? 15 : 8;
#ifdef PAL_STAMP2
		PSTAMP(0xFFA0C4);                /* after the ndirty count */
#endif
		/* (rotor every other vint tried 2026-09-06: -10 lines, fades would
		 * step at 30Hz, and the demo stayed at half speed — reverted) */
#ifdef GLOW_MASK
		/* PALSTATIC v2 probe: steady state only (a storm/fade vint
		 * must ship the glow blocks' real values), drop the glow
		 * blocks' marks unless force-raw (heal) wants them raw. */
#ifdef PAL_STAMP2
		PSTAMP(0xFFA0C6);                /* before the glow check */
#endif
		if (ndirty <= 24 && glow_live) {
			/* PROGRAM CHECK (the transform-flash lesson: the flash
			 * PLAYS ON THE SAME BLOCKS as the ambient glow — a
			 * blind mask swallowed the whole flash sequence).
			 * Peek two sentinel words in the game's palette mirror:
			 * any value outside the ambient cycle's sets = the game
			 * switched programs -> lift the mask THIS vint so the
			 * foreign values ship; the SH-2 pauses on that landing
			 * and re-grants only when the ambient cycle returns. */
			uint16_t g1 = ((const uint16_t*)0xFF9000)[0x99];
			uint16_t g2 = ((const uint16_t*)0xFF9000)[0xA1];
			/* 0xA6 = constant 0x100F in every scene/level census —
			 * the transform flash is the only writer. Without it
			 * the flash ran UNDER the mask (its ambient programs
			 * keep the other sentinels in-set) and its 0xA6/0xAE
			 * pulses were swallowed (Mike's flash artifacts). */
			uint16_t g3 = ((const uint16_t*)0xFF9000)[0xA6];
#ifdef PAL_STAMP2
			PSTAMP(0xFFA0C8);            /* sentinels read */
#endif
			if ((g1 & 0xFF) != 0
			    || (uint8_t)((uint8_t)(g1 >> 8) - 0x49) > 6
			    || (g2 != 0x100F && (g2 & 0xF00F) != 0x300F)
			    || g3 != 0x100F) {
				glow_live = 0;
			} else {
				uint8_t gm = (uint8_t)(0x30 & (uint8_t)~pal_force[0]);
				pd[0] &= (uint8_t)~gm;
			}
		}
#ifdef PAL_STAMP2
		PSTAMP(0xFFA0CA);                /* after the glow check */
#endif
#endif
		uint8_t r = pal_next;
#ifdef PAL_STAMP2
		PSTAMP(0xFFA0C0);                /* before the rotor loop */
#endif
#ifdef PAL_DELTA
		/* v3: compare-at-select. A block is compared (and the shadow
		 * updated) ONLY when a K slot is open for it — a compared-
		 * but-unselected block would mark its words shipped without
		 * shipping them. Empty deltas ship nothing and do the
		 * ship-twice transition without consuming a slot (this is
		 * what keeps the chronically re-marked redundant writers —
		 * census: 91% of the 0x2628 writer — out of the packet).
		 * The transition itself moves here from the ship loop; the
		 * zero-landed abort corner that move opens is closed at the
		 * !ok epilogue below. */
		/* 68000-SHAPED (the 55-line lesson): the first cut compared
		 * uint16s through indexed addressing and measured ~9 LINES
		 * PER BLOCK (6 compares = 55 lines — ~110 cycles per word).
		 * This walk compares LONGS through post-increment pointers
		 * (cmpm-shaped, ~30cy per 2 words on the match path) and
		 * byte-skips the rotor over clear bitmap bytes. Variable
		 * shifts (no barrel shifter) run only on CHANGED words —
		 * ~10/frame globally per the census. */
		uint16_t ncmp = 0;               /* probe: blocks compared/copied */
		uint16_t fr2 = *(volatile uint16_t*)0xFFB0F0;   /* vint counter
		                                  * for the backoff phase */
#ifdef PALROTOR_OFF
		/* SESSION 7 CALIBRATION: after the boot/attract palette loads
		 * (vint 900), no palette rotor/compare/selection at all (K
		 * stays 0, colours freeze). Measures the 68K lines the
		 * palette-delta selection costs per vint — the prize of moving
		 * the palette compare to the SH-2. A from-boot skip deadlocks
		 * (the SH-2 boot needs the first palette blocks). */
		if (*(volatile uint16_t*)0xFFB0F0 < 900)
#endif
		for (uint16_t n = 0; n < 64 && K < kcap; ) {
			uint8_t by = (uint8_t)(r >> 3);
			uint8_t mby = (uint8_t)(pd[by] | pal_retry[by]);
			if (!mby && (r & 7) == 0) {  /* whole byte clear: skip 8 */
				r = (uint8_t)((r + 8) & 63);
				n = (uint16_t)(n + 8);
				continue;
			}
			uint8_t bit = (uint8_t)(1u << (r & 7));
			if (mby & bit) {
				/* STREAK BACKOFF (the rotor diet, 2026-08-31): a
				 * block that compared EQUAL >=4 consecutive visits
				 * is a chronic redundant writer — visit it every
				 * 4th vint instead of re-proving equality at ~5
				 * lines a visit. Marks stay set (nothing is lost,
				 * detection is <=3 vints late on a slow pulse);
				 * retry transitions are exempt (ship-twice must
				 * finish) and storms bypass via the open clamp. */
#ifdef PAL_DIET
				/* OFF unless PALSTREAK=/PALBACKOFF= asks for it
				 * (LOOP28 85).  The counter below was never
				 * written until 2026-09-08, so this branch had
				 * never fired in a shipped rom; turning it on by
				 * default would change colour timing under the
				 * accepted hardware base without a play pass. */
				if ((pd[by] & bit) && !(pal_retry[by] & bit)
				    && pal_streak[r] >= PAL_STREAK_N && kcap == 8
				    && (uint8_t)((fr2 + r) & PAL_BACKOFF_M)) {
					r = (uint8_t)((r + 1) & 63);
					n++;
					continue;
				}
#endif
				ncmp++;
				const uint32_t *mp4 = (const uint32_t*)
					((const uint16_t*)0xFF9000 + ((uint16_t)r << 5));
				uint32_t *sp4 = (uint32_t*)
					((uint16_t*)PAL_SHADOW + ((uint16_t)r << 5));
#ifdef PAL_NOCMP
				/* NO DISCOVERY ON THE 68K (LOOP27 81). Ship the marked
				 * block raw and let the SH-2 be the one that knows what
				 * changed. v1 of this measured WORSE (74.1% vs 82.0%)
				 * because it still maintained the shadow — a 16-long
				 * copy per block, about what the compare it replaced
				 * cost. With no compare there is no shadow: no copy, no
				 * scan, no mask walk, just the block id. */
				(void)mp4; (void)sp4;
				pal_force[by] &= (uint8_t)~bit;
				ids[K++] = (uint8_t)(r | R60_PAL_RAW);
				palw += 32;
				if (pd[by] & bit) {
					pd[by] &= (uint8_t)~bit;
					pal_retry[by] |= bit;
				} else {
					pal_retry[by] &= (uint8_t)~bit;
				}
#else
				if (pal_force[by] & bit) {
					for (uint16_t i = 0; i < 16; i++)
						*sp4++ = *mp4++;
					pal_force[by] &= (uint8_t)~bit;
					ids[K++] = (uint8_t)(r | R60_PAL_RAW);
					palw += 32;
#ifdef PAL_DIET
					pal_streak[r] = 0;
#endif
				} else {
					/* FAST PRE-SCAN (unattended slice 1): retry
					 * blocks compare EQUAL end to end (~half of
					 * ncmp); a pure cmp loop is ~3x tighter than
					 * the mask walk, and an equal block skips the
					 * walk entirely. Changed blocks pay the scan
					 * up to the first difference, then the walk
					 * resumes from block start. */
#ifdef R60_TIGHT
					uint16_t first_ne = 0;       /* longs to skip in the walk */
#endif
					{
#ifdef R60_TIGHT
						uint16_t eq = r60_ne_longs(mp4, sp4, 16);
#ifdef R60_TIGHT_CHECK
						/* probe: run the C scan too, count disagreements
						 * at 0xFFA1B0, and let the C result decide */
						{
							static uint8_t chk_init;
							const uint32_t *qa = mp4;
							const uint32_t *qb = sp4;
							uint16_t eqc = 16;
							if (!chk_init) {
								chk_init = 1;
								*(volatile uint16_t*)0xFFA1B0 = 0;
								*(volatile uint16_t*)0xFFA1B2 = 0;
								*(volatile uint16_t*)0xFFA1B4 = 0;
								*(volatile uint16_t*)0xFFA1B6 = 0;
								*(volatile uint16_t*)0xFFA1B8 = 0;
							}
							while (eqc && *qa++ == *qb++) eqc--;
							(*(volatile uint16_t*)0xFFA1B4)++;
							if (!eqc) (*(volatile uint16_t*)0xFFA1B6)++;
							if (eqc != eq)   /* exact: same remaining count */
								(*(volatile uint16_t*)0xFFA1B0)++;
							eq = eqc;
						}
#endif
						first_ne = (uint16_t)(16u - eq);
#else
						const uint32_t *qa = mp4;
						const uint32_t *qb = sp4;
						uint16_t eq = 16;
						while (eq && *qa++ == *qb++) eq--;
#endif
						if (!eq) {
							/* THE STREAK COUNTER (2026-09-08).
							 * It was declared and READ at the
							 * backoff test but never written, so
							 * PALSTREAK/PALBACKOFF were inert for
							 * every N >= 1 — a 16-point sweep read
							 * one number.  This is the increment
							 * the diet was designed around. */
#ifdef PAL_DIET
							if (pal_streak[r] != 255)
								pal_streak[r]++;
#endif
							if (pd[by] & bit) {
								pd[by] &= (uint8_t)~bit;
								pal_retry[by] |= bit;
							} else {
								pal_retry[by] &= (uint8_t)~bit;
							}
							r = (uint8_t)((r + 1) & 63);
							n++;
							continue;
						}
					}
					uint16_t m0 = 0, m1 = 0, cnt = 0;
#ifdef R60_TIGHT
					/* the pre-scan proved longs [0, first_ne) equal: they
					 * contribute no bits and need no shadow store */
					mp4 += first_ne;
					sp4 += first_ne;
					for (uint16_t i2 = (uint16_t)(first_ne * 2u); i2 < 32; i2 += 2) {
#else
					for (uint16_t i2 = 0; i2 < 32; i2 += 2) {
#endif
						uint32_t a = *mp4++;
						if (a != *sp4) {
							uint32_t o = *sp4;
							uint16_t mb = 0;
							if ((uint16_t)(a >> 16) != (uint16_t)(o >> 16)) {
								mb |= (uint16_t)(1u << (i2 & 15));
								cnt++;
							}
							if ((uint16_t)a != (uint16_t)o) {
								mb |= (uint16_t)(2u << (i2 & 15));
								cnt++;
							}
							if (i2 < 16) m0 |= mb; else m1 |= mb;
							*sp4 = a;
						}
						sp4++;
					}
#ifdef PAL_DIET
					if (cnt) pal_streak[r] = 0;
#endif
					if (cnt > R60_PAL_DMAX) {
						ids[K++] = (uint8_t)(r | R60_PAL_RAW);
						palw += 32;
					} else if (cnt) {
						dmask[K][0] = m0;
						dmask[K][1] = m1;
						ids[K++] = r;
						palw += (uint16_t)(2 + cnt);
					}
					/* cnt==0: no slot, transition only */
				}
				if (pd[by] & bit) {
					pd[by] &= (uint8_t)~bit;
					pal_retry[by] |= bit;
				} else {
					pal_retry[by] &= (uint8_t)~bit;
				}
#endif
			}
			r = (uint8_t)((r + 1) & 63);
			n++;
		}
		PSTAMP(0xFFA0BE);                /* rotor+compares done */
#ifdef PAL_STAMP2
		PSTAMP(0xFFA0C2);
#endif
		*(volatile uint16_t*)0xFFA0D2 = ncmp;    /* blocks compared */
		*(volatile uint16_t*)0xFFA0D4 = ndirty;  /* dirty|retry pop */
#else
		for (uint16_t n = 0; n < 64 && K < kcap; n++) {
			if ((pd[r >> 3] | pal_retry[r >> 3]) & (1u << (r & 7)))
				ids[K++] = r;
			r = (uint8_t)((r + 1) & 63);
		}
#endif
		pal_next = r;
	}
	PSTAMP(0xFFA178);                    /* fine: after pal_next */
#ifndef FB_SPR_READ
	/* live sprite records, terminator included, cap 40 */
	for (uint16_t i = 0; i < 24; i++) {
		if (s[i * 8 + 2] & 0x8000) { nrec = (uint16_t)(i + 1); break; }
		if (i == 23) nrec = 24;
	}
#else
	nrec = 0;      /* v3: records ride FB staging (S1 strike) */
	(void)s;
#endif
	/* v2: rowscroll only when CHANGED — compare the 60-word WRAM
	 * mirror against the previous ship (0xFFA400: the legacy staging
	 * buffer, free under R60 direct consume). ~1 line of WRAM reads
	 * buys ~14 lines of push on every non-row-effect frame. */
	uint16_t rs_ship = 0;
	PSTAMP(0xFFA17A);                    /* fine: before rs compare */
#ifdef SHIM_NOPUSH
	if (0)
#endif
	/* (compare only in rowscroll mode / every 4th vint tried 2026-09-06:
	 * the demo's cloud band rides the per-row alt-set bits of this table
	 * and littered for ~40 frames after each release — every vint) */
	{	/* 2026-09-06: long compare with post-increment (the word-indexed
		 * loop measured 8 lines/vint; 30 longs is ~1.5) */
		const uint32_t *ra = (const uint32_t*)((const uint16_t*)0xFF8000 + 0x7C0);
		const uint32_t *rb = (const uint32_t*)0xFFA400;
#ifdef R60_TIGHT
		uint16_t k = r60_ne_longs(ra, rb, 30);
#ifdef R60_TIGHT_CHECK
		{
			const uint32_t *xa = ra;
			const uint32_t *xb = rb;
			uint16_t kc = 30;
			while (kc && *xa++ == *xb++) kc--;
			if (kc) (*(volatile uint16_t*)0xFFA1B8)++;
			if ((kc != 0) != (k != 0))
				(*(volatile uint16_t*)0xFFA1B2)++;
			k = kc;
		}
#endif
#else
		uint16_t k = 30;
		while (k && *ra++ == *rb++) k--;
#endif
		if (k) {
			rs_ship = 1;
			ra = (const uint32_t*)((const uint16_t*)0xFF8000 + 0x7C0);
			uint32_t *wb = (uint32_t*)0xFFA400;
			for (k = 0; k < 30; k++) *wb++ = *ra++;
		}
	}
	PSTAMP(0xFFA17C);                    /* fine: after rs compare */
	if (K > 15) K = 15;
	{	/* sprite-half tracer: ids >= 32 entering the push (mask the
		 * v3 raw flag; a no-op on v2 ids, which are < 64) */
		for (uint16_t j3 = 0; j3 < K; j3++)
			if ((ids[j3] & 0x7F) >= 32)
				(*(volatile uint16_t*)0xFFA0B0)++;
	}
	{	/* TORN-PACKET FEEDBACK (the load-in wedge): remember what
		 * this push carries so a BAD1 echo can re-mark it. Without
		 * this, ship-twice tolerates exactly ONE consecutive tear -
		 * a storm vint pair tears both attempts and the blocks are
		 * lost until the game re-dirties them (measured: sprite
		 * set0 stale for 280+ frames = the whole load-in mess). */
		volatile uint8_t *lp = (volatile uint8_t*)0xFFA0C0;
		/* LOST-PUSH BELT (2026-09-05): keep the PREVIOUS push's list
		 * too. An echo can arrive a vint late (pended behind a busy
		 * COMM8) or cover two consecutive tears; re-marking both lists
		 * costs at most one redundant raw re-ship. 0xFFA044..0xFFA054
		 * (17 bytes; 0xFFA040 is the window-span word). */
		{
#ifdef R60_TIGHT
			/* 17 bytes = 4 longs + 1: 85 instructions a vint -> 6 */
			volatile uint32_t *lq4 = (volatile uint32_t*)0xFFA044;
			const volatile uint32_t *lp4 = (const volatile uint32_t*)0xFFA0C0;
			lq4[0] = lp4[0]; lq4[1] = lp4[1];
			lq4[2] = lp4[2]; lq4[3] = lp4[3];
			((volatile uint8_t*)0xFFA044)[16] = lp[16];
#else
			volatile uint8_t *lq = (volatile uint8_t*)0xFFA044;
			for (uint16_t j7 = 0; j7 < 17; j7++)
				lq[j7] = lp[j7];
#endif
		}
		lp[0] = (uint8_t)K;
		for (uint16_t j4 = 0; j4 < K; j4++)
			lp[1 + j4] = (uint8_t)(ids[j4] & 0x7F);   /* BARE ids: the
			                     * BAD1 handler's idb<64 guard */
		lp[1 + K] = rs_ship ? 1 : 0;
	}
#ifdef PAL_DELTA
	/* v3 pal section: 1 length word + 8 ids + payload + pad, %4==0 */
	uint16_t pal_pad = (uint16_t)((0u - (palw + 1u)) & 3u);
#ifdef BOOT_FBFREE
	/* Read back the master's sentinels one vint later. A region still
	 * holding its magic is untouched by everything between; a changed
	 * one is in use. Encoded as a 4-bit result in the value colour:
	 * bit0 0x12000, bit1 0x14000, bit2 0x18000, bit3 0x1C000 — set = FREE. */
	{
		uint8_t m = 0;
		if (*(volatile uint16_t*)0x852000 == 0xA51) m |= 1;
		if (*(volatile uint16_t*)0x854000 == 0xA52) m |= 2;
		if (*(volatile uint16_t*)0x858000 == 0xA53) m |= 4;
		if (*(volatile uint16_t*)0x85C000 == 0xA54) m |= 8;
		*(volatile uint16_t*)0xFFA186 = m;
	}
#endif
#ifdef BOOT_FBTIME
	/* IS THE FRAMEBUFFER A CHEAPER ROUTE THAN THE DREQ FIFO?
	 * (2026-09-08, LOOP27 65.) Measured: the push is 99 scanlines for a
	 * ~52-word packet, 63 for 20 words — roughly 1.1-2.4 lines PER WORD
	 * through the FIFO, against ~0.15 on ares.
	 * But the 68K can also write the 32X FRAMEBUFFER directly through
	 * the 0x840000 window, and s16_68kdraw proved those writes LAND on
	 * this hardware. If an FB write is materially cheaper than a FIFO
	 * write, the packet could cross that way instead and the master
	 * could read it from the FB — no DREQ at all.
	 * Time 20 writes into the FB packet hole (0x851A00, the buffer the
	 * master already consumes from) and report with the value
	 * instrument, directly comparable to the 48-line regs stage. */
	{
		volatile uint16_t *fb = (volatile uint16_t*)0x851A00;
		PSTAMP(0xFFA182);
		for (uint16_t q = 0; q < 20; q++) fb[q] = q;
		PSTAMP(0xFFA184);
	}
#endif
#ifdef BOOT_FBX_B
	/* B: the same packet written where the DREQ push already is — FM=1,
	 * after the post and therefore after the ISR's flip. If A is stale
	 * and B is fresh, the flip is the whole of the sentinel probe's
	 * 0-of-4 and the transport simply belongs here. */
	{
		volatile uint16_t *fx = (volatile uint16_t*)0x852040;
		PSTAMP(0xFFA182);
		fx[0] = fbx_seq;
		for (uint16_t q = 1; q < 13; q++) fx[q] = q;
		PSTAMP(0xFFA184);
	}
#endif
#ifdef BOOT_COMMTIME
	/* IS IT THE DREQ FIFO, OR EVERY 68K->32X ACCESS? (2026-09-08,
	 * LOOP27 60.) Twenty FIFO writes cost 32-80+ lines on hardware and
	 * 2-3 on ares (59). Time twenty writes to a HARMLESS 32X register in
	 * the same build, same place, same load: COMM2 (0xA15122), which the
	 * 68K already writes every vint with BANK_SHADOW — writing the same
	 * value twenty more times changes nothing.
	 * Bucketed with the SAME thresholds as the regs stage so the two are
	 * directly comparable:
	 *   equally slow -> the whole 68K<->32X interface is the problem and
	 *     the data has to cross some other way entirely;
	 *   fast         -> the DREQ FIFO path specifically is slow, and the
	 *     fix is bounded. */
	{
		volatile uint16_t *c2 = (volatile uint16_t*)0xA15122;
		uint16_t keep = *c2;
		PSTAMP(0xFFA17C);
		for (uint16_t q = 0; q < 20; q++) *c2 = keep;
		PSTAMP(0xFFA17E);
	}
#endif
	uint16_t tw = (uint16_t)(22u + (rs_ship ? 60u : 0u)
	                         + (K ? (9u + palw + pal_pad) : 0u)
	                         + nrec * 8u + 2u);
	PSTAMP(0xFFA0B6);                    /* selection/compare done */
#else
	uint16_t tw = (uint16_t)(22u + (rs_ship ? 60u : 0u)
	                         + (K ? (8u + (uint16_t)K * 32u) : 0u)
	                         + nrec * 8u + 2u);
#endif
	/* (FM_LATE v1 — ship at FM=0 after F103 — measured: the master's FB
	 * work is ~50 lines/vint and then landed in the game's time. v2:
	 * ship at FM=1 while the master blits (no landing wait on its side),
	 * ack awaited before the game's IRQ4.) */
	{
#ifndef FB_XPORT
		*(volatile uint16_t*)0xA15110 = tw;
#endif
		(*(volatile uint32_t*)0xFFA0A4) += tw;   /* push words, honest sum */
		(*(volatile uint16_t*)0xFFA0A8)++;
	}
#ifdef BOOT_VALUE
	/* READ THE NUMBER, NOT A BUCKET (2026-09-08, LOOP27 64).
	 * Every hardware probe so far has returned one of four buckets, and
	 * ORANGE spans 32-80 lines — wide enough to hide the entire effect
	 * of a change. MD CRAM is 9 bits (3 per channel), which is exactly
	 * enough to carry a 0-255 scanline count as a COLOUR:
	 *     R = d & 7 , G = (d >> 3) & 7 , B = (d >> 6) & 3
	 * Flood all 64 entries with it and the exact value is recoverable
	 * from a screenshot histogram. No buckets, no bisection, one capture
	 * per number. Reports the regs stage (0xFFA0B6 -> 0xFFA0B8). */
	{
#ifdef BOOT_FBX_TIME
		/* WHAT DOES THE FM=0 FB WRITE COST? The 48-vs-2 comparison in
		 * HANDOFF-DREQ timed the FB write at FM=1 — where this probe has
		 * now shown (runB=0 on hardware) the write does not land at all.
		 * The only FB route that works is this one, so this is the only
		 * number that can be set against the FIFO's 48 lines/20 words. */
		uint8_t p0 = (uint8_t)(*(volatile uint16_t*)0xFFA182 >> 8);
		uint8_t p2 = (uint8_t)(*(volatile uint16_t*)0xFFA184 >> 8);
#elif defined(BOOT_FBXFER)
		/* master's verdict, straight off COMM8 — no WRAM slot to audit.
		 * 0xBBxx = the master ran the check; anything else = it did not
		 * (flooded as 42 so "never checked" cannot read as "zero"). */
		uint8_t p0 = 0;
		uint16_t lt = *(volatile uint16_t*)0xFFA186;
		uint8_t p2 = ((lt & 0xF000) == 0xF000) ? (uint8_t)(lt & 0xFF) : 42;
#elif defined(BOOT_FBFREE)
		uint8_t p0 = 0;
		uint8_t p2 = (uint8_t)*(volatile uint16_t*)0xFFA186;
#elif defined(BOOT_FBTIME)
		uint8_t p0 = (uint8_t)(*(volatile uint16_t*)0xFFA182 >> 8);
		uint8_t p2 = (uint8_t)(*(volatile uint16_t*)0xFFA184 >> 8);
#elif defined(BOOT_BURN_W) || defined(BOOT_BURN_R)
		/* LOOP29 142: the stamps are the raw V counter, which runs
		 * 0xE0..0xEA then JUMPS BACK to 0xE5..0xFF inside vblank, so a
		 * burn straddling the jump read negative (clamped 127). Convert
		 * both to lines from vblank start before subtracting.
		 * W: the SHIM_BURN loop from 68K WRAM; R: the same loop from
		 * cart ROM. R minus W is the adapter's fetch tax. */
#define VLINE(v) ((uint8_t)((v) < 0xE0 ? (v) + 38 : ((v) <= 0xEA ? (v) - 0xE0 : (v) - 0xE5 + 11)))
		/* stamps moved to 0xFFA1A0/A2/A4: 0xFFA186 and 0xFFA188 are written
		 * by the landing diag and the rate probe, which clobbered p0. */
#ifdef BOOT_BURN_W
		uint8_t p0 = VLINE((uint8_t)(*(volatile uint16_t*)0xFFA1A0 >> 8));
		uint8_t p2 = VLINE((uint8_t)(*(volatile uint16_t*)0xFFA1A2 >> 8));
#else
		uint8_t p0 = VLINE((uint8_t)(*(volatile uint16_t*)0xFFA1A2 >> 8));
		uint8_t p2 = VLINE((uint8_t)(*(volatile uint16_t*)0xFFA1A4 >> 8));
#endif
#elif defined(BOOT_GAMERATE)
		uint8_t p0 = 0;
		uint8_t p2 = (uint8_t)(*(volatile uint16_t*)0xFFA188 & 0xFF);
#elif defined(BOOT_TILEVER)
		/* LOOP29 231: the consume's VRAM readback (TILE_VERIFY).
		 * vi76 read on the rig: checked, VRAM-zero 7 (sat), mismatch 0
		 * -> the SOURCE words read zero. vi77 encodes the next question:
		 * bit 7 bias | bit 6 any slot out of range | bits 5-3 VRAM
		 * all-zero records (sat 7) | bits 2-0 consumes where FS changed
		 * mid-consume (sat 7).
		 * vi77 read on the rig: no slot out of range, VRAM-zero 7, FS
		 * never changed mid-consume. vi78 carries the SH-2's own
		 * read-backs (m_main.c TV_BITS, packet word 1 bits 8-12):
		 * bit 7 bias | bit 6 FS changed (any) | bit 5 VRAM-zero (any)
		 * | bits 4-3 replay read-back != tp_lastA (sat 3)
		 * | bits 2-0 publish read-back != staging (sat 7)
		 * vi78 on the rig: both read-backs 0. vi79 re-uses the same
		 * five bits for m_main.c's tv_rep0 (2-0) and tv_pub0 (4-3):
		 * SH-2 FB writes made while the SH-2 sees FM=0.
		 * vi79 on the rig: both 0. vi80: bits 2-0 tv_alt (packet A
		 * payload changed between publish and the next window, sat 7),
		 * bits 4-3 first differing long index >> 7.
		 * vi80: tv_alt saturates on ares too (the bank alternates under
		 * d, so the compare is not a corruption test) -- withdrawn.
		 * vi81: bit 7 bias | bits 6-5 record count >> 3 of the last
		 * consume that read a zero record | bits 4-0 that record's
		 * index (31 = none yet).
		 * vi81 on the rig: zero records at indices 5, 8, 25 of 24-40.
		 * vi82: bit 7 bias | bit 6 FS changed | bit 5 VRAM-zero any |
		 * bits 4-3 emitter records with all-zero output (sat 3) |
		 * bits 2-0 emitter records for a set with no MD line (sat 7),
		 * both from m_main.c md_emit_art via packet word 1.
		 * vi82 on the rig: zout saturated, noline 6. vi83: bit 7 bias |
		 * bit 6 VRAM-zero any | bit 5 pen map all zero (any) | bits 4-3
		 * zero-output records whose ROM source reads zero UNCACHED (sat
		 * 3) | bits 1-0 ... reads zero CACHED (sat 3).
		 * vi83 on the rig: both saturate, map-zero later. vi84 (same
		 * layout): bit 5 md_tag re-read differs (any) | bits 4-3 code
		 * outside the bank (sat 3) | bits 1-0 real nonzero code that
		 * read zero (sat 3). */
		 * vi76-vi84's zero-record readings RETRACTED (ares' attract emits
		 * the level's blank tiles too; the saturating counters hid it).
		 * vi85: bit 7 bias | bit 6 more than 31 | bits 4-0 zero-output
		 * records whose tile is NON-BLANK in the bank (m_main.c tv_real),
		 * expected 0 wherever SH-2 ROM reads are sound. */
		uint8_t p0 = 0;
		uint16_t tw = *(volatile uint16_t*)0xFFA1EC;
		uint8_t p2 = (uint8_t)(0x80
			| (((tw >> 14) & 1) << 6)
			| ((tw >> 8) & 0x1F));
#elif defined(BOOT_CONSV)
		/* LOOP29 148: the consumes' span, V at cons.mark (0xFFB0B6) minus
		 * V at cons.entry (0xFFB0B0), in lines */
#define VLINE3(v) ((uint8_t)((v) < 0xE0 ? (v) + 38 : ((v) <= 0xEA ? (v) - 0xE0 : (v) - 0xE5 + 11)))
		uint8_t p0 = VLINE3((uint8_t)(*(volatile uint16_t*)0xFFB0B0 >> 8));
		uint8_t p2 = VLINE3((uint8_t)(*(volatile uint16_t*)0xFFB0B6 >> 8));
#elif defined(BOOT_ENTRYV) || defined(BOOT_POSTV)
		/* LOOP29 146: the 68K's own timeline on hardware, in lines from
		 * vblank start: ENTRYV = the vint handler's consume entry stamp
		 * (0xFFB0B0), POSTV = the V at post (0xFFA0A0). */
#define VLINE2(v) ((uint8_t)((v) < 0xE0 ? (v) + 38 : ((v) <= 0xEA ? (v) - 0xE0 : (v) - 0xE5 + 11)))
		uint8_t p0 = 0;
#ifdef BOOT_ENTRYV
		uint8_t p2 = VLINE2((uint8_t)(*(volatile uint16_t*)0xFFB0B0 >> 8));
#else
		uint8_t p2 = VLINE2((uint8_t)(*(volatile uint16_t*)0xFFA0A0 >> 8));
#endif
#elif defined(BOOT_VALUE_SEL)
		/* the SELECTION phase alone: entry -> selection/compare done.
		 * Under FB_XPORT the whole push read 217 lines against the FIFO
		 * route's 99, while the 20-word ship itself is ~1 — so the
		 * question is whether selection got more expensive when the
		 * push moved ahead of the post. */
		uint8_t p0 = (uint8_t)(*(volatile uint16_t*)0xFFA0B4 >> 8);
		uint8_t p2 = (uint8_t)(*(volatile uint16_t*)0xFFA0B6 >> 8);
#elif defined(BOOT_VALUE_TOTAL)
		/* whole push: entry 0xFFA0B4 -> records+tail shipped 0xFFA0BC */
		uint8_t p0 = (uint8_t)(*(volatile uint16_t*)0xFFA0B4 >> 8);
		uint8_t p2 = (uint8_t)(*(volatile uint16_t*)0xFFA0BC >> 8);
#else
		uint8_t p0 = (uint8_t)(*(volatile uint16_t*)0xFFA0B6 >> 8);
		uint8_t p2 = (uint8_t)(*(volatile uint16_t*)0xFFA0B8 >> 8);
#endif
		uint8_t d  = (uint8_t)(p2 - p0);
#if defined(BOOT_BURN_W) || defined(BOOT_BURN_R)
		/* bias off zero: an unbiased 0 floods black, which is also what
		 * a probe that never ran looks like (the trap from LOOP27 67) */
		d = (uint8_t)(0x80 | (d > 127 ? 127 : d));
#endif
		uint16_t col = (uint16_t)((((d >> 6) & 3) << 9)
		                        | (((d >> 3) & 7) << 5)
		                        | (( d       & 7) << 1));
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_PUSHDELAY
	/* IS THE STALL CONTENTION WITH THE MASTER? (2026-09-08, LOOP27 63.)
	 * The first ~20 words are expensive and the ~32 after them are free
	 * (61, 62), which points at the master still being busy when the 68K
	 * starts pushing — ARMGATE's echo says ARMED, not IDLE.
	 * Wait a deliberate 8 scanlines before starting, then measure the
	 * regs stage with the same buckets. If the stage SHRINKS, contention
	 * at push start is confirmed and the lever is WHEN we push. If it
	 * does not, the stall is intrinsic to the first FIFO writes.
	 * (8 lines is cheap next to a 32-80 line stage; if it helps, the
	 * right delay can be tuned or replaced by a real ready signal.) */
	{
		uint8_t v_wait = (uint8_t)((*(volatile uint16_t*)0xC00008) >> 8);
		uint8_t target = (uint8_t)(v_wait + 8);
		uint16_t guard = 20000;
		while ((uint8_t)((*(volatile uint16_t*)0xC00008) >> 8) != target
		       && --guard) ;
	}
#endif
#ifndef FB_XPORT
	*ctrl = 4;                           /* DREQ enable (FIFO route only) */
#endif
#ifdef BOOT_SETUP
	/* SETUP OR WORDS? (2026-09-08, LOOP27 62.) Cutting the packet
	 * 52 -> 20 words did NOT speed the game on hardware (61), yet the
	 * regs stage (selection-done -> regs-shipped) is the biggest thing
	 * in the push. Those are only compatible if the cost is a FIXED
	 * OVERHEAD at the start of the push rather than per-word. Between
	 * those two stamps sit exactly three things: the length-register
	 * write (0xA15110 = tw), the DREQ enable (*ctrl = 4), and then the
	 * 20 words. Stamp right here, after the enable, to split them. */
	PSTAMP(0xFFA180);
#endif
	{
		uint8_t ok = 1;
		/* PER-WORD POLL RESTORED (Mike's play pass, 2026-08-22):
		 * the 4-word burst discipline LOST WORDS under real play
		 * load — state_health: dreq misaligned 124/2684 (4.6% of
		 * packets torn = a sprite-freeze frame every ~22 = the
		 * stutter). ares DROPS writes to a full FIFO (trace-dreq
		 * 'miss' events), it does not stall them, and the burst
		 * probe's "never full" verdict was measured on attract
		 * load, not play load. Not-full guarantees ONE free slot,
		 * so one write per poll is the only overflow-proof
		 * discipline. Fidelity buys the ~6 handler lines back. */
#ifdef FB_XPORT
/* no FIFO to poll and no word can be dropped: the FB is memory */
#define R60G() do { } while (0)
#define R60P(w) do { FBX_DST[fbx_i++] = (uint16_t)(w); } while (0)
#else
#define R60G() do { while (*ctrl < 0 && --spin) ;                      if (!spin) ok = 0; } while (0)
#define R60P(w) do { R60G(); if (ok) fifo[0] = (w); } while (0)
#endif
		const uint16_t *lr = (const uint16_t*)0xFF8000 + 0x740;
#ifdef BOOT_NOPOLL
		/* IS IT THE POLL OR THE WRITE? (2026-09-08, LOOP27 58)
		 * Shipping 20 words costs ~100 lines on hardware and 2-3 on ares
		 * (56), and the FIFO is NEVER FULL on either machine — spin
		 * residual 2600 both sides (57). So the 68K is not waiting for
		 * the master; each ACCESS is slow. Per word the code does TWO
		 * 32X register accesses: a READ of *ctrl (0xA15107) in R60G()
		 * and a WRITE to fifo[0] (0xA15112).
		 * Drop the per-word poll — sound here precisely because the FIFO
		 * is provably never full — and ship the 20 words with writes
		 * only. If the stage roughly HALVES, the poll READ is half the
		 * cost and 68K reads of 32X registers are the expensive thing.
		 * If it does not move, the WRITES are.
		 * MEASUREMENT ONLY: the per-word poll exists because a 4-word
		 * burst lost words under play load (Mike's 2026-08-22 pass), and
		 * that risk is unchanged. Do not ship this. */
		{
			const uint16_t *lp = (const uint16_t*)0xFF8000 + 0x740;
			R60G();
			for (uint16_t g = 0; ok && g < 20; g++) fifo[0] = lp[g];
		}
#elif defined(PAL_DELTA)
		if (r60_ship_words(lr, 20)) ok = 0;
#else
		for (uint16_t g = 0; ok && g < 5; g++) {
			R60G();
			if (ok) {
				R60P(lr[0]); R60P(lr[1]);
				R60P(lr[2]); R60P(lr[3]);
			}
			lr += 4;
		}
#endif
		PSTAMP(0xFFA0B8);                /* regs shipped (20 words) */
#ifdef BOOT_PUSHCUT
		/* DOES PACKET SIZE ACTUALLY BUY SPEED ON HARDWARE? (LOOP27 61.)
		 * Entry 60: every 68K->32X access costs ~2-4 scanlines, so words
		 * are the lever — but r60_push's own comment records that
		 * halving the packet did NOT move the span ON ARES, and ares is
		 * not per-access bound. Settle it on hardware instead of
		 * arguing: ship the 20 reg words and ABANDON the rest.
		 * The picture will be wrong — the master gets no records, no
		 * palette. That is fine; the only thing being read is the
		 * GAME-FRAME WHEEL (BOOTMOTIONGAME). If the wheel speeds up
		 * sharply, packet size is the lever and the fix is bounded.
		 * If it does not move, words are NOT the cost and entry 60's
		 * per-access reading is wrong.
		 * MEASUREMENT ONLY, never ship. */
		ok = 0;                          /* skip the remaining stages */
#endif
		if (ok) {
			volatile uint16_t *bm = (volatile uint16_t*)0xFFB9FE;
			/* word 20: bits 0-12 dirty pages, bit 15 display-on, bits
			 * 13-14 a PUSH SEQUENCE (lost-push belt v3): the master
			 * echoes BAD1 on a gap, so a push that landed NOTHING is
			 * re-marked one vint late from the two-deep id history. */
			{
				static uint8_t push_seq;
				push_seq = (uint8_t)((push_seq + 1) & 3);
#ifdef BOOT_PKTCHK
				bm_pushed = (uint16_t)(*bm | ((IO_MISC & 0x20) ? 0x8000u : 0u));
#endif
				R60P((uint16_t)(*bm | ((IO_MISC & 0x20) ? 0x8000u : 0u)
				                | ((uint16_t)push_seq << 13)));
			}
			*bm = 0;
			/* tag: bit10 = rowscroll present; bits 9..0 = EXACT
			 * packet length in words (max 924 fits 10 bits). The
			 * harvest requires landed == this. Without it an
			 * INTERIOR FIFO drop of a multiple of 8 words
			 * VALIDATES: order is preserved, the magic still sits
			 * at landed-2, and only the %8 arithmetic can object.
			 * Mike's blue-white player (2026-08-24): sprite blocks
			 * 32/33 took a shifted payload exactly this way —
			 * marks cleared, no BAD1, wedged for the session. */
			R60P((uint16_t)((K ? ((K << 11) | 0x8000) : 0)
			                | (rs_ship ? 0x0400 : 0)
			                | (tw & 0x3FF)));
		}
		if (ok && rs_ship) {
			const uint16_t *rs = (const uint16_t*)0xFF8000 + 0x7C0;
#ifdef PAL_DELTA
			if (r60_ship_words(rs, 60)) ok = 0;
#else
			for (uint16_t g = 0; ok && g < 15; g++) {
				R60G();
				if (ok) {
					R60P(rs[0]); R60P(rs[1]);
					R60P(rs[2]); R60P(rs[3]);
				}
				rs += 4;
			}
#endif
		}
		if (ok && K) {
#ifdef PAL_DELTA
			R60P((uint16_t)(palw + pal_pad));   /* v3 length word */
#endif
			for (uint16_t g = 0; ok && g < 4; g++) {
				uint16_t j2 = (uint16_t)(g * 2);
				uint16_t hi = (j2 * 2 < K) ? ids[j2 * 2] : 0xFF;
				uint16_t lo = (j2 * 2 + 1 < K) ? ids[j2 * 2 + 1] : 0xFF;
				uint16_t w0 = (uint16_t)((hi << 8) | lo);
				hi = ((j2 + 1) * 2 < K) ? ids[(j2 + 1) * 2] : 0xFF;
				lo = ((j2 + 1) * 2 + 1 < K) ? ids[(j2 + 1) * 2 + 1] : 0xFF;
				R60G();
				if (ok) {
					R60P(w0);
					R60P((uint16_t)((hi << 8) | lo));
				}
			}
#ifdef PAL_DELTA
			/* v3: raw blocks ship the v2 32-word form; delta blocks
			 * ship 2 mask words + changed words. The ship-twice
			 * transition already ran at selection. Mirror reads are
			 * stable here — the game is suspended for the whole
			 * handler. */
			for (uint16_t j = 0; ok && j < K; j++) {
				const uint16_t *p = (const uint16_t*)0xFF9000
					+ ((uint16_t)(ids[j] & 0x7F) << 5);
				if (ids[j] & R60_PAL_RAW) {
					if (r60_ship_words(p, 32)) ok = 0;
				} else {
					uint16_t m = dmask[j][0];
					R60P(m); R60P(dmask[j][1]);
					for (uint16_t i = 0; ok && m; i++, m >>= 1)
						if (m & 1) R60P(p[i]);
					m = dmask[j][1];
					for (uint16_t i = 16; ok && m; i++, m >>= 1)
						if (m & 1) R60P(p[i]);
				}
			}
			for (uint16_t j = 0; ok && j < pal_pad; j++)
				R60P(0);
		}
		PSTAMP(0xFFA0BA);                /* rowscroll+pal shipped */
#else
			for (uint16_t j = 0; ok && j < K; j++) {
				const uint16_t *p = (const uint16_t*)0xFF9000
					+ ((uint16_t)ids[j] << 5);
				for (uint16_t g = 0; ok && g < 8; g++) {
					R60G();
					if (ok) {
						R60P(p[0]); R60P(p[1]);
						R60P(p[2]); R60P(p[3]);
					}
					p += 4;
				}
				/* ship-twice discipline (the LOOP8 thunk race):
				 * freshly-dirty ships now AND next frame; a
				 * retry-only ship clears the retry. */
				{
					uint8_t by = (uint8_t)(ids[j] >> 3);
					uint8_t bit = (uint8_t)(1u << (ids[j] & 7));
					if (pd[by] & bit) {
						pd[by] &= (uint8_t)~bit;
						pal_retry[by] |= bit;
					} else {
						pal_retry[by] &= (uint8_t)~bit;
					}
				}
			}
		}
		PSTAMP(0xFFA0BA);                /* rowscroll+pal shipped */
#endif
#ifndef FB_SPR_READ
#ifdef PAL_DELTA
		if (ok && nrec && r60_ship_words(s, (uint16_t)(nrec * 8u)))
			ok = 0;
#else
		for (uint16_t i = 0; ok && i < nrec; i++) {
			const uint16_t *p = s + i * 8;
			R60G();
			if (ok) {
				R60P(p[0]); R60P(p[1]);
				R60P(p[2]); R60P(p[3]);
			}
			R60G();
			if (ok) {
				R60P(p[4]); R60P(p[5]);
				R60P(p[6]); R60P(p[7]);
			}
		}
#endif
#endif
		PSTAMP(0xFFA17E);                /* fine: before tail */
		if (ok) { R60G(); PSTAMP(0xFFA180); if (ok) { R60P(0xA55A); PSTAMP(0xFFA182); R60P(0x5AA5); } }
#ifdef FB_XPORT
		/* PUBLISH, and only now. Every packet word is in the FB; this
		 * word says so. The 68000 completes writes in order, so a
		 * publish written last cannot precede its own payload — the
		 * same guarantee md_consume relies on in the other direction.
		 * A master that reads a stale sequence simply keeps last
		 * frame's records, which is the existing no-packet path. */
		if (ok) {
#ifdef FBX_STAGE
			/* Nothing reaches the framebuffer here: the packet is in
			 * WRAM and r60_blast() moves it at the tail, at FM=0.
			 * Handing over the length is the whole handover — a
			 * non-zero fbx_stage_n IS the "a packet is ready" flag. */
			fbx_stage_n = fbx_i;
#else
			volatile uint16_t *pub = (volatile uint16_t*)FBX_PUB_MD;
			pub[1] = fbx_i;              /* exact word count */
			fbx_seq_pub++;
			pub[0] = (uint16_t)(FBX_MAGIC | fbx_seq_pub);
#ifdef FLIP_CENSUS
			/* what the 68K believes it published, for the delivery
			 * census: count, last sequence, last length, and the
			 * publish word READ BACK through the same window */
			(*(volatile uint16_t*)0xFFA190)++;
			*(volatile uint16_t*)0xFFA192 = (uint16_t)(FBX_MAGIC | fbx_seq_pub);
			*(volatile uint16_t*)0xFFA194 = fbx_i;
			*(volatile uint16_t*)0xFFA196 = pub[0];   /* read-back */
#endif
#endif
		}
#endif
		PSTAMP(0xFFA0BC);                /* records+tail shipped */
#ifdef BOOT_SETUP
	/* Two buckets in one flood: which half of the regs stage is it?
	 *   0xFFA0B6 selection-done -> 0xFFA180 DREQ enabled  = SETUP
	 *   0xFFA180 -> 0xFFA0B8 regs shipped                 = 20 WORDS
	 *   GREEN  both small (<8)
	 *   RED    SETUP dominates   -> the length write / DREQ enable
	 *   BLUE   WORDS dominate    -> the per-word FIFO writes
	 *   YELLOW comparable */
	{
		uint8_t p0 = (uint8_t)(*(volatile uint16_t*)0xFFA0B6 >> 8);
		uint8_t p1 = (uint8_t)(*(volatile uint16_t*)0xFFA180 >> 8);
		uint8_t p2 = (uint8_t)(*(volatile uint16_t*)0xFFA0B8 >> 8);
		uint8_t su = (uint8_t)(p1 - p0), wd = (uint8_t)(p2 - p1);
		uint16_t col;
		if (su < 8 && wd < 8)              col = 0x00E0;   /* GREEN  */
		else if (su > (uint8_t)(wd * 2))   col = 0x000E;   /* RED    */
		else if (wd > (uint8_t)(su * 2))   col = 0x0E00;   /* BLUE   */
		else                               col = 0x00EE;   /* YELLOW */
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_COMMTIME
	/* 20 writes to COMM2, same buckets as the regs stage (LOOP27 60). */
	{
		uint8_t c0 = (uint8_t)(*(volatile uint16_t*)0xFFA17C >> 8);
		uint8_t c1 = (uint8_t)(*(volatile uint16_t*)0xFFA17E >> 8);
		uint8_t d  = (uint8_t)(c1 - c0);
		uint16_t col = (d <  8) ? 0x00E0
					 : (d < 32) ? 0x00EE
					 : (d < 80) ? 0x006E
					            : 0x000E;
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_REGLEN
	/* THE REGS STAGE, IN LINES (2026-09-08, LOOP27 59). Shipping 20
	 * words: 0xFFA0B6 selection-done -> 0xFFA0B8 regs-shipped.
	 * ares = 2-3 lines. Run this WITH and WITHOUT BOOTNOPOLL to split
	 * the poll READ from the FIFO WRITE by magnitude, not ranking.
	 *   GREEN <8   YELLOW <32   ORANGE <80   RED >=80 */
	{
		uint8_t a2 = (uint8_t)(*(volatile uint16_t*)0xFFA0B6 >> 8);
		uint8_t a3 = (uint8_t)(*(volatile uint16_t*)0xFFA0B8 >> 8);
		uint8_t d  = (uint8_t)(a3 - a2);
		uint16_t col = (d <  8) ? 0x00E0
					 : (d < 32) ? 0x00EE
					 : (d < 80) ? 0x006E
					            : 0x000E;
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_SPIN
		/* SPIN RESIDUAL (2026-09-08, LOOP27 57). R60G() spins while
		 * *ctrl < 0 — DREQ FIFO FULL — before EVERY word, decrementing
		 * `spin` from 2600. So the residual counts how much of the push
		 * was spent waiting for the master to drain the FIFO.
		 * r60_push's own comment records ares showing residual 2600,
		 * i.e. NEVER FULL. Hardware spends ~100 lines shipping 20 words
		 * (LOOP27 56, BLUE). If the residual is low here, FIFO-full
		 * stalling is confirmed as the mechanism and the fault is the
		 * MASTER'S DRAIN, not the 68K. */
		*(volatile uint16_t*)0xFFA17A = spin;
#endif
#ifdef SHIM_BURN
		{	/* sensitivity probe: burn ~SHIM_BURN lines of 68K time */
			PSTAMP(0xFFA1A0);
			volatile uint16_t bi;
			for (bi = 0; bi < (uint16_t)(SHIM_BURN * 40u); bi++) ;
			PSTAMP(0xFFA1A2);
			shim_burn_rom();                 /* same loop, ROM-resident */
			PSTAMP(0xFFA1A4);
		}
#endif
		if (!ok) (*(volatile uint16_t*)0xFFB0E0)++;
#ifdef PAL_DELTA
		/* v3 abort corner: transitions ran at SELECTION, so an abort
		 * that landed ZERO words (no BAD1 echo — the echo needs
		 * landed>0) would leave cleared marks over a shadow that
		 * claims the words shipped. Re-mark + force-raw locally;
		 * torn-but-landed pushes heal through the BAD1 echo as
		 * before (double-marking is harmless). */
		if (!ok) {
			for (uint16_t j6 = 0; j6 < K; j6++) {
				uint8_t idb = (uint8_t)(ids[j6] & 0x7F);
				pd[idb >> 3] |= (uint8_t)(1u << (idb & 7));
				pal_force[idb >> 3] |= (uint8_t)(1u << (idb & 7));
			}
		}
#endif
		*(volatile uint16_t*)0xFFA0AE = spin;   /* residual: 2600 = never
		                                         * waited on FULL at all */
#undef R60P
#undef R60G
	}
}
#ifdef FBX_STAGE
/* THE BLAST (LOOP28 89). The only part of the packet path that must run
 * at FM=0, and the only part that touches the framebuffer. It is a
 * straight copy of what r60_push staged in WRAM plus the publish word,
 * so its cost is the packet's length and nothing else: ~150 words on a
 * play frame, under 2 scanlines.
 *
 * Ordering is the same contract the FIFO route had. The 68000 completes
 * writes in program order, so the publish written last cannot precede
 * its own payload, and a master that reads a stale sequence keeps last
 * frame's records — the existing no-packet path.
 *
 * Longs, not words: the packet is word-aligned by construction and the
 * FB window takes long writes, which halves the bus transactions. An
 * odd trailing word is copied on its own. */
__attribute__((section(".data"), noinline))
static void r60_blast(int bump) {
	uint16_t n = fbx_stage_n;
	if (!n) return;                      /* no packet staged this vint */
#ifdef FBX_BOTH
	/* THE PACKET LIVES IN THE FRAMEBUFFER, AND THE FRAMEBUFFER SWAPS
	 * (LOOP28 91). Measured: with flips running, the master's lift finds
	 * an already-seen sequence on 1180 of 2921 windows — one lost packet
	 * per flip — because the bank it reads is not the bank the blast
	 * wrote. So write BOTH: once at the tail, once before the next post,
	 * with a flip possibly in between. The second write carries the SAME
	 * sequence, so a master that already lifted it correctly skips it;
	 * what it buys is that whichever bank the master reads carries the
	 * latest packet. Two ~2-line copies instead of one. */
	if (!bump) {
		if (!fbx_stage_live) return;     /* nothing worth repeating */
	} else {
		fbx_stage_live = 1;
	}
#else
	(void)bump;
	fbx_stage_n = 0;
#endif
	{
		const uint32_t *sp = (const uint32_t*)fbx_stage;
		volatile uint32_t *dp = (volatile uint32_t*)FBX_PKT_MD;
		uint16_t nl = (uint16_t)(n >> 1);
		while (nl--) *dp++ = *sp++;
		if (n & 1)
			((volatile uint16_t*)FBX_PKT_MD)[n - 1] = fbx_stage[n - 1];
	}
	{
		volatile uint16_t *pub = (volatile uint16_t*)FBX_PUB_MD;
		pub[1] = n;                      /* exact word count */
		if (bump) fbx_seq_pub++;
		pub[0] = (uint16_t)(FBX_MAGIC | fbx_seq_pub);
#ifdef FLIP_CENSUS
		(*(volatile uint16_t*)0xFFA190)++;
		*(volatile uint16_t*)0xFFA192 = (uint16_t)(FBX_MAGIC | fbx_seq_pub);
		*(volatile uint16_t*)0xFFA194 = n;
		*(volatile uint16_t*)0xFFA196 = pub[0];   /* read-back */
#endif
	}
	*(volatile uint16_t*)0xFFA186 =
		*(volatile uint16_t*)0xC00008;   /* V after the blast */
}
#endif
#endif /* R60 */
#if defined(K2_FREE) && defined(IDLE_TOKEN)
#error K2_FREE claims COMM4 for the arm echo; IDLE_TOKEN also lives there
#endif


#ifdef POST_LATE
static uint8_t r60_late, r60_late_v;
__attribute__((section(".data"), noinline))
void r60_late_post(void)
{
	if (!r60_late) return;
	r60_late = 0;
	*(volatile uint16_t*)0xA15100 |= 0x8000;
	*mars_comm2 = BANK_SHADOW;
	*mars_comm12 = (uint16_t)(0xD000 | r60_late_v);
	#ifdef MD_ROUND
		/* LOOP29 193: carry the GAME'S ROUND in COMM10 bits 13-15. The
		 * low 13 are the tile-dirty mask and the SH-2 masks with 0x1FFF
		 * (187), so the top three are free -- three bits for five
		 * rounds. 0xFFF142 is the game's own scene variable
		 * (LOOP-DECOMPILE 66: `move.b $FFF142,d0 ; the scene index`).
		 * The SH-2 cannot read 68K wram, so this is the only way it can
		 * know which round's palette table to install. */
		/* LOOP29 222: MASK the dirty word. It is 16 regions wide and
		 * regions 13-15 (the actor lines) are dirty whenever a sprite
		 * palette moves, so the round in bits 13-15 read as round|dirt
		 * on the SH-2 -- a wrong table installed on the same-round
		 * return (vi66b: black rectangles in level 1's tree row). */
		*mars_comm10 = (uint16_t)((*(volatile uint16_t*)0xFFB9FE & 0x1FFFu)
			| ((uint16_t)(*(volatile uint8_t*)0xFFF142 & 7) << 13));
#else
		*mars_comm10 = *(volatile uint16_t*)0xFFB9FE;
#endif
	*mars_comm4 = 0;
	*mars_comm0 = 0x2020;
	*(volatile uint16_t*)0xFFA0A0 = *(volatile uint16_t*)0xC00008;   /* V at post */
	(*(volatile uint16_t*)0xFFB0F2)++;
	*(volatile uint16_t*)0xFFA0AA = *(volatile uint16_t*)0xC00008;
	r60_push();
	*(volatile uint16_t*)0xFFA0AC = *(volatile uint16_t*)0xC00008;
	*(volatile uint16_t*)0xFFA09E = *(volatile uint16_t*)0xC00008;
}
#endif
#if defined(FBX_PEND) && defined(FBX_STAGE)
/* GAME CONTEXT (LOOP29 137): reached from the shared FM-gate spin the
 * moment FM reads 0, every register saved by the caller. FM can only
 * rise again from our own vint handler, so a blast that starts at FM=0
 * completes at FM=0 provided no vint fires inside it: skip the last ~31
 * active lines and vblank itself, and the pre-post slot picks those up. */
__attribute__((section(".data"), noinline))
void fbx_late_blast(void)
{
	uint8_t vv = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);
	if (vv >= 0xC0) return;
	if (*(volatile uint16_t*)0xA15100 & 0x8000) return;
	fbx_pend = 0;
	r60_blast(1);
	(*(volatile uint16_t*)0xFFA0FC)++;
}
#endif
/* RAMCODE (2026-09-06): the whole vint shim executed from the cart
 * window (nm: 0x8c1150) under SH-2 contention; only r60_push had been
 * moved. 2.4KB of .data. */
__attribute__((section(".data"), noinline))
void shim_vblank(void) {
#ifdef BOOT_PRECONSUME
	/* WHERE DOES THE TIME GO BEFORE THE CONSUME? (2026-09-08, LOOP27 53)
	 * s16_span2 on hardware: the consume itself takes 8-24 lines (YELLOW,
	 * some GREEN) against <8 on ares. NOT frame-eating. But entry 42
	 * measured it ENDING deep in the visible picture — so it must be
	 * STARTING late. Stamp V at the very top of the handler, before any
	 * gate or announce work, so the gap to the consume's own entry stamp
	 * (0xFFB0B0) can be bucketed. */
	*(volatile uint16_t*)0xFFA178 = *(volatile uint16_t*)0xC00008;
#endif
/* A/B WRITER PROBE, 68K half: CUT 2026-09-08. The write below —
 * 68K paints GREEN into FB rows 16-23 at vint top with FM=0, after
 * forcing the 32X mode on — BLACKS THE SCREEN ON ARES, where the game
 * plainly runs without it (bisected: master half alone shows the bar
 * over a live game; this half alone is black). It is unsound the same
 * way s16_fbpix and the no-flip probes were, and an unsound probe sent
 * to the FPGA costs a round trip and teaches nothing. The 68K-side
 * positive control already exists and already passed on hardware:
 * rom/s16_68kdraw.32x. Use that. Do not resurrect this without finding
 * out what the write actually lands on first. */
#ifdef BOOT_GAMERATE
	/* GAME FRAMES PER 64 VINTS, AS A NUMBER (2026-09-08, LOOP27 68).
	 * The wheel (BOOTMOTIONGAME) needs a wristwatch and a person; this
	 * is the same question read exactly, off the same source — the
	 * game's own scene timer at WRAM 0xFFF02A, one tick per game frame.
	 * Sample it every 64 vints and report the delta:
	 *     64 = the game is running at vint rate (60 Hz)
	 *     32 = half
	 *      3 = the ~95% miss the DREQ FIFO was costing us
	 * Biased into bit 7 so a dead machine cannot read as a number. */
	{
		static uint16_t gr_base, gr_vc;
		static uint8_t  gr_val;
		/* LOOP29 140: IRQ4 COMPLETIONS run at vint rate on every build
		 * (a missed frame takes IRQ4's short path and still completes),
		 * so this read 64 everywhere. Use the game's own missed-frame
		 * counter instead: value = 64 - misses per 64 vints = game frames
		 * per 64 vints, the same number gameplay_speed.py reports. */
#ifdef GAME_GATE
		/* under GAMEGATE the game never overruns; its rate is the
		 * release count (0xFFA0F6) -- report releases per 64 vints */
		uint16_t t = (uint16_t)(0 - *(volatile uint16_t*)0xFFA0F6);   /* negated: the
		                     * subtraction below yields 64 - (-releases)... see gr_val */
#else
		uint16_t t = *(volatile uint16_t*)0xFFF144;
#endif
		(void)*(volatile uint16_t*)0xFFA18E;          /* was: game IRQ4
		                     * completions, counted in md_start.s at
		                     * fmgate_ret — the game's own scene timer
		                     * runs at scene-dependent rates and in both
		                     * directions, and comparing two builds that
		                     * had reached DIFFERENT attract scenes was
		                     * measuring the scene, not the port. */
		if (++gr_vc >= 64) {
			/* the scene timer runs in either direction depending on the
			 * scene (the first read clamped at 127 on the ship line —
			 * a countdown, not a fast game); magnitude is the rate */
			uint16_t sd = (uint16_t)(t - gr_base);        /* misses in 64 vints */
#ifdef GAME_GATE
			sd = (uint16_t)(0 - sd);                      /* releases in 64 vints */
			gr_val = (uint8_t)(sd > 64 ? 64 : sd);
#else
			gr_val = (uint8_t)(sd > 64 ? 0 : 64 - sd);    /* game frames in 64 vints */
#endif
			gr_base = t;
			gr_vc = 0;
		}
		*(volatile uint16_t*)0xFFA188 = (uint16_t)(0xF000 | 0x80 | gr_val);
	}
#endif
#ifdef BOOT_MOTION
	/* ARE WE ACTUALLY STREAMING, AND HOW FAST? (2026-09-08, LOOP27 37.)
	 * Mike, fairly: "nothing ever shows actual moving streaming frames."
	 * Every hardware probe this arc has been a STATIC verdict colour, and
	 * the one cadence datum we have is his "VERY VERY VERY VERY SLOW".
	 * The rate has never been measured on hardware at all.
	 *
	 * Flood the whole MD palette with a colour that steps every 8 vints
	 * through 8 distinct colours, so ONE FULL CYCLE IS 64 VINTS — almost
	 * exactly one second at 60 Hz. No instrumentation to read: count the
	 * cycles against a clock.
	 *   ~1 cycle per second  -> vints are arriving at 60 Hz.
	 *   ~1 per 2 seconds     -> 30 Hz, the ship-line cadence.
	 *   much slower / stuck  -> the vint chain itself is starved on
	 *                           hardware, and every palette and
	 *                           framebuffer question so far has been
	 *                           downstream of a much bigger problem.
	 * MD CRAM flooded at vint top: the one reporter proven to cover the
	 * screen on this rig (BOOTMDPAL). */
	{
		static const uint16_t wheel[8] = {
			0x000E, 0x00EE, 0x00E0, 0x0EE0,
			0x0E00, 0x0E0E, 0x0EEE, 0x0006
		};
#ifdef BOOT_MOTION_GAME
		/* GAME-FRAME WHEEL (LOOP27 39). Same instrument, driven off the
		 * GAME's own scene timer (WRAM 0xFFF02A, one tick per game frame)
		 * instead of the vint count. One wheel step per 8 game frames, so
		 * a full cycle is 64 GAME FRAMES:
		 *   ~1 cycle/second  -> 60 game-frames/s, full speed
		 *   ~2 seconds       -> 30 Hz
		 *   ~4 seconds       -> 15 Hz
		 * Against the vint wheel's measured 1 cycle/second (entry 38),
		 * the ratio of the two cycle times IS the hardware miss rate —
		 * the number the whole 60 Hz arc rests on, never taken on
		 * silicon. */
		uint16_t mtick = *(volatile uint16_t*)0xFFF02A;
#else
		static uint16_t mtick;
		mtick++;
#endif
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		{
			uint16_t col = wheel[(mtick >> 3) & 7];
			for (int q = 0; q < 64; q++)
				*(volatile uint16_t*)0xC00000 = col;
		}
	}
#endif
#ifdef BOOT_PALPEEK
	/* PALETTE-DATA VERDICT, painted where it is actually visible: flood
	 * all 64 MD CRAM entries at vint top with the colour stashed by the
	 * consume-site peek. BOOTMDPAL proved a flood here covers the whole
	 * screen on hardware. A flat colour is also unmistakable against a
	 * stale capture (three so far, md5 72f2caf6 under three rom names) —
	 * a stale frame shows the graveyard, a live one shows flat colour.
	 * RED = the 48 palette words are all zero; YELLOW = all identical;
	 * GREEN = varied, a plausible palette. Black/no flood = the consume
	 * never ran, which is itself the answer. */
	{
		uint16_t col = *(volatile uint16_t*)0xFFA168;
		if (col) {
			*(volatile uint32_t*)0xC00004 = 0xC0000000u;
			for (int q = 0; q < 64; q++)
				*(volatile uint16_t*)0xC00000 = col;
		}
	}
#endif
#ifdef BOOT_MDPAL
	/* WHICH LAYER AM I LOOKING AT? (2026-09-08, LOOP27 29). Neither the
	 * vblank drain nor hammered direct 32X CRAM stores change the
	 * magenta/green picture on the MiSTer. So test the assumption every
	 * probe since entry 18 has rested on: that the picture is the 32X
	 * layer at all. THE SHIP LINE IS MDBGALL — the background and FG
	 * cat-0 are drawn by the MEGA DRIVE VDP, not the 32X, and they take
	 * MD CRAM colours, which the 68K owns and which no 32X palette work
	 * can touch.
	 * Paint the WHOLE MD palette RED every vint:
	 *   picture turns red  -> what we see is the MD PLANE, and the
	 *     entire 32X palette arc has been aimed at the wrong layer;
	 *     the magenta/green is a wrong MD CRAM, uploaded from FB
	 *     staging by the 68K.
	 *   stays magenta/green -> it really is the 32X layer and MD CRAM
	 *     is not involved. */
	{
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int i = 0; i < 64; i++)
			*(volatile uint16_t*)0xC00000 = 0x000E;   /* red */
	}
#endif
#ifdef BOOT_TAGBLUE
	/* ROM IDENTITY TAG (2026-09-08, LOOP27 21). s16_abdraw_on and
	 * s16_palvbl_on produced BYTE-IDENTICAL MiSTer screenshots from
	 * DIFFERENT roms, which should not be possible with a live game. So
	 * before reading one more colour off that screen, prove which code
	 * is actually running: the 68K paints the MEGA DRIVE backdrop BLUE
	 * every vint. MD CRAM, so FM cannot gate it and the 32X display gate
	 * cannot hide it. Blue on screen = THIS rom is running. Not blue =
	 * the machine is still running older code and every reading taken
	 * from it is about that older code. */
	*(volatile uint32_t*)0xC00004 = 0xC0000000u;
	*(volatile uint16_t*)0xC00000 = 0x0E00;      /* blue */
#endif
#ifdef BOOT_ABREAD
	/* A/B READBACK PROBE (2026-09-08, LOOP27 15). s16_abdraw came back
	 * NO BAR on the MiSTer: the master's in-game FB write never reaches
	 * the display, while the 68K's does (s16_68kdraw) and the master's
	 * own BOOT write does (s16_fliptest, green bar). Two live causes:
	 *   A. the master's in-game write does not land at all;
	 *   B. it lands in memory the display never shows.
	 * This splits them WITHOUT a mailbox: the 68K reads the master's bar
	 * bytes straight out of the framebuffer at FM=0 (the window at
	 * 0x840000 is ours to read here, same as the packet consume) and
	 * paints the verdict on the MEGA DRIVE backdrop, which FM cannot
	 * gate and which shows even with the 32X output off — the reporter
	 * every one-colour 32X verdict this arc got wrong.
	 *   GREEN backdrop = the 68K FINDS the master's bar. The write
	 *     landed; the fault is on the display side (cause B).
	 *   RED backdrop   = the 68K does NOT find it. Either the write is
	 *     lost in-game, or master and 68K see different banks — and
	 *     with s16_abdraw's no-bar that means the master's writes land
	 *     where neither the 68K nor the display can see them.
	 * Honest limit: this cannot by itself tell "lost" from "bank the 68K
	 * cannot see". It does cleanly kill one of the two. */
	if (!(*(volatile uint16_t*)0xA15100 & 0x8000)) {
		const volatile uint32_t *bar = (const volatile uint32_t *)
			(0x840000u + 0x200u + 8u * 320u);
		uint16_t col = (bar[0] == 0x01010101u || bar[100] == 0x01010101u)
			? 0x00E0        /* GREEN: the master's bar is in the FB */
			: 0x000E;       /* RED:   it is not */
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_MDMODE
	/* HARDWARE PROBE / candidate fix: the 68K asserts the 32X display mode
	 * every vint (FM=0 here at entry, before the belt). If this shows the
	 * game on hardware, the SH-2's mode write is being lost and the 68K
	 * mirroring it is the fix. */
	if (!(*(volatile uint16_t*)0xA15100 & 0x8000))
		*(volatile uint16_t*)0xA15180 = 0x8000 | 0x0080 | 0x0001;
#endif
#ifdef BOOT_VPULSE
	/* HARDWARE PROBE: alternate 32X CRAM 0 every vint once the game runs
	 * (FM must be 0 here; a raised FM just drops the write). A pulsing
	 * screen = the vint chain is alive; solid = the 68K is hung. Bit 2
	 * of the count: ~8-vint blocks so the eye can follow it. */
	{
		static uint16_t vp;
		vp++;
		if (game_running)
			*(volatile uint16_t*)0xA15200 = (vp & 8) ? 0x001F : 0x7C00;
		else
			*(volatile uint16_t*)0xA15200 = (vp & 8) ? 0x03E0 : 0x03FF;
	}
#endif
	static uint16_t busy;
#ifdef MD_BG
	{
		static uint8_t painted;
		if (!painted) { painted = 1; md_bg_palette(); }
	}
#endif

	(*(volatile uint16_t*)0xFFB0F0)++;   // diagnostics: handler entries
	// GAME-SLACK RING (2026-09-06): the interrupted game PC (0xFFB0F8,
	// stamped by _vblank) into a 128-long ring at 0xFFA200 (free:
	// 0xFFA200-0xFFA3FF). A vint that lands in the game's idle loop
	// (arcade 0x3984-0x3988, ours +0x900000) means the game finished its
	// frame; anything else = the game is still working = a lost frame.
	// The arcade idles at 95% of vblanks in the attract (MAME census).
#ifndef PC_SAMP
	{
		volatile uint32_t *ring = (volatile uint32_t*)0xFFA200;   /* 64 longs */
		uint16_t ri = *(volatile uint16_t*)0xFFB0F0;
		uint32_t ipc = *(volatile uint32_t*)0xFFB0F8;
		ring[ri & 63] = ipc;
		/* entry V | bit 15 = game was idle (its handler had finished) */
		((volatile uint16_t*)0xFFA380)[ri & 63] = (uint16_t)
			((*(volatile uint16_t*)0xC00008 >> 8)
			 | (((uint16_t)(*mars_comm14 & 0x7F)) << 8)    /* vblank count at entry */
			 | ((ipc >= 0x903980u && ipc <= 0x903990u) ? 0x8000u : 0u));
		((volatile uint16_t*)0xFFA300)[ri & 63] = 0xFFFF;   /* end pending */
	}
#endif

	// ITER5 TAIL PROBE: V at TRUE handler entry (before the window/ack-spin)
	// so the span includes the window ack-wait — the part that scales with
	// SH-2 speed (the MAME vs ares divergence the post-window probe missed).
	uint8_t v_entry = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);

	// ITER5: the DREQ push only fires on vints whose window was ACCEPTED
	// (posted + acked) — those are the vints the master re-armed the DMA
	// on. On gate-rejected vints (65%) the master never runs, so pushing
	// would fill the undrained FIFO and block the 68K. Set below.
	uint8_t window_ok = 0;
#ifdef TAIL_PROBE
	uint8_t dreq_span = 0, palscan_span = 0;
#endif
#ifdef FM_GATE
	uint8_t fmg_k2old = 0;
	(void)fmg_k2old;
	fmgate_wcmd = 0;
	/* OVERRUN BELT: the game's vint upload is DELIBERATELY ungated (it
	 * is vint-only and part B raises after it), so it must see FM=0.
	 * A window that overran into this vint gets waited out here —
	 * bounded, counted, and the only spin left in the shim. The SH-2
	 * drops FM just before its COMM0 ack, so after this both are
	 * clear and the consume/push below run exactly as before. */
	if (*(volatile uint16_t*)0xA15100 & 0x8000) {
		uint32_t belt = 4000000UL;
		fmgate_belt++;
#ifdef BOOT_FMCHK
		/* HARDWARE PROBE: acked window but FM still up for ~200k polls ->
		 * tell the master (COMM10 = 0xDEAD); it paints and retries. */
		while ((*(volatile uint16_t*)0xA15100 & 0x8000) && --belt)
			if (belt == 3800000UL && *mars_comm0 == 0)
				*mars_comm10 = 0xDEAD;
#else
		while ((*(volatile uint16_t*)0xA15100 & 0x8000) && --belt) ;
#endif
	}
#endif
#ifdef BOOT_PKTCHK
	/* HARDWARE PROBE v16 (FB pixels vs palette — decisive): let the game
	 * run normally to vint 240 (many clean composed+flipped frames), THEN
	 * halt and, every iteration while FM is down, force the 32X mode on
	 * and paint a diagnostic palette: CRAM 0 = OPAQUE blue (no through, so
	 * the MD plane can NOT show through), CRAM 1..255 = grey ramp. The
	 * displayed bank is a real, completed game frame.
	 *   solid blue      = the 32X frame is all pixel 0 (compose/blit did
	 *                     not land on the displayed bank) -> a blit/flip
	 *                     bank-parity bug (ours).
	 *   grey shapes     = the frame HAS pixels; the earlier black was the
	 *                     game's palette (CRAM) never being applied -> the
	 *                     master apply_cram / palette path on hardware. */
	{
		static uint16_t vp2;
		if (game_running && ++vp2 == 240) {
			for (;;) {
				if (!(*(volatile uint16_t*)0xA15100 & 0x8000)) {
					*(volatile uint16_t*)0xA15180 = 0x0081;      /* mode 256 + 32X prio */
					volatile uint16_t *c32 = (volatile uint16_t*)0xA15200;
					c32[0] = 0x7C00;                             /* opaque blue, no through */
					for (uint16_t k = 1; k < 256; k++) {
						uint16_t v = (uint16_t)((k >> 3) & 31);
						c32[k] = (uint16_t)((v << 10) | (v << 5) | v);
					}
				}
			}
		}
	}
#endif
#ifdef BOOT_VISRCHK
	/* HARDWARE PROBE: after 120 vints of game, did the master's V-ISR
	 * ever run? (FM is 0 here, after the belt.) WHITE = yes, RED = never;
	 * then halt so nothing repaints the answer. */
	{
		static uint16_t vc;
		if (game_running && ++vc == 120) {
			*(volatile uint16_t*)0xA15200 = *(volatile uint16_t*)0xA1512A ? 0x7FFF : 0x001F;
			for (;;) ;
		}
	}
#endif
#ifdef TXT_WRAM
#if !TXT_WRAM_ON
#error fmgate_tab.h generated without TXTWRAM (stale header - rebuild)
#endif
	/* LOOP 27 q4 — TOP-OF-PASS TEXT STAGING. The writers in txtw[]
	 * (credit line, health bar) now store into the WRAM text mirror;
	 * each mark thunk left a dirty byte (and, for the credit line, the
	 * byte offset it used). Copy exactly the dirty footprints into FB
	 * text staging here, at FM=0 before this vint's raise, so the SH-2's
	 * pre-flip text capture sees them in the same window a direct FB
	 * write would have reached. ~0.4-0.8 lines per dirty writer. */
	for (uint8_t ti = 0; txtw[ti].words; ti++) {
		volatile uint8_t *dirty = (volatile uint8_t*)(0xFF0000u | (txtw[ti].slot + 2u));
		if (!*dirty)
			continue;
		uint16_t toff = txtw[ti].off;
		if (toff == 0xFFFF)
			toff = *(volatile uint16_t*)(0xFF0000u | txtw[ti].slot);
		else if (txtw[ti].sel && *(volatile uint8_t*)(0xFF0000u | txtw[ti].sel))
			toff = txtw[ti].off2;                /* the other player's footprint */
		toff &= 0x0FFE;
		const uint16_t *cs = (const uint16_t*)(0xFF8000u + toff);
		volatile uint16_t *cd = (volatile uint16_t*)(0x85F000u + toff);
		for (uint16_t ci = 0; ci < txtw[ti].words; ci++)
			cd[ci] = cs[ci];
		*dirty = 0;
		(*(volatile uint16_t*)0xFFB0CC)++;       /* diag: txtw copies */
	}
#endif

	// RENDER WINDOW. SH-2 framebuffer writes are blocked while RV=1 (they
	// work only at RV=0), but the game needs RV=1 to fetch its ROM code.
	// This handler runs from WORK RAM, so the 68K needs no ROM here — drop
	// RV to 0, tell the SH-2 to draw the whole frame, wait for its ack, then
	// restore RV=1 before returning to the game's IRQ handler.
	//
	// Runs FIRST in the handler: the SH-2s' flip pair must land inside
	// vblank (H-int fires at its start), where FBCTL latches immediately
	// even on deferred-latch hardware — mid-frame flips cost up to a
	// whole frame of latch wait per edge on ares.
	// Only every SECOND entry: the full compose+blit window (~2 frames) is
	// longer than a frame, so back-to-back windows leave a pending H-int at
	// every rte and the game never gets cycles (proven: palette shadow
	// frozen at 1 entry). Alternating window/no-window entries gives the
	// game the whole gap after each cheap entry. Display updates at ~20-30
	// fps until the compose is split across both SH-2s.
	// THREE-phase window cadence, one short window per H-int: the
	// COMPOSE window (0x2100, sprites/text/copies, no flips), then TWO
	// BLIT windows (0x2000 top half, 0x2010 bottom half). The full-
	// frame blit pair measured ~2.5ms > the 2.4ms vblank, so its flip-
	// back missed blanking and ares deferred it a frame — raw staging
	// on screen ("just flashing"). Each half-blit window is ~1ms:
	// both flip edges land safely inside vblank. Longest 68K stall
	// stays the compose window (~7ms).
#ifdef R60
	/* REBUILD — ONE IDENTICAL FRAME EVERY VINT. Replaces the whole
	 * window/cadence machine below (window_ok stays 0, so the legacy
	 * push block downstream is dormant). Order per vint:
	 * consume both MD-plane packets (FM=0, belt-guaranteed) ->
	 * gates (entry-V / master-still-open / game-mid-span) ->
	 * announce (ISR arms) -> raise -> post 0x2020 -> push (the ONE
	 * R60 packet) -> flip-hold (K2FREE echo, usually already done).
	 * No spins on the ack, no k phases, no idle beats. */
	{
		/* gates FIRST (cheap), then ANNOUNCE (the ISR arms and starts
		 * its post-wait), then the consumes (~10 lines, FM=0), then
		 * raise+post — the post lands ~14 lines after vblank, inside
		 * the ISR's widened 1100-tick window. */
		uint8_t r60_go = 0;
		if (v_entry < 0xDF || v_entry > 0xE8) {
			(*(volatile uint16_t*)0xFFB0FC)++;       /* entry reject */
		} else if (*mars_comm0) {
			fmgate_defer++;                          /* master still open */
		} else {
			uint32_t ipc = *(volatile uint32_t*)0xFFB0F8;
			const uint32_t *sp2 = fmgate_spans;
			uint8_t midspan = 0;
			while (*sp2) {
				if (ipc >= sp2[0] && ipc <= sp2[1]) { midspan = 1; break; }
				sp2 += 2;
			}
			if (midspan)
				fmgate_defer++;
			else
				r60_go = 1;
		}
#ifdef BOOT_GATECHK
		/* HARDWARE PROBE: why is the 68K not posting? Painted every vint
		 * the gate declines (FM is 0 here, after the belt), two shades
		 * alternating every 8 vints so a live 68K reads as a flicker and a
		 * hung one as a steady colour. RED = entry V outside the gate,
		 * BLUE = master still open (COMM0 live), GREEN = game mid-span. */
		{
			static uint16_t gc; gc++;
			if (!r60_go) {
				uint16_t col;
				if (v_entry < 0xDF || v_entry > 0xE8)      col = (gc & 8) ? 0x001F : 0x000C;
				else if (*mars_comm0)                      col = (gc & 8) ? 0x7C00 : 0x3000;
				else                                       col = (gc & 8) ? 0x03E0 : 0x0180;
				*(volatile uint16_t*)0xA15200 = col;
			}
		}
#endif
		if (r60_go) {
#ifdef ARM_GATE
			*mars_comm4 = 0;                         /* any 0xA001 after this is THIS vint's arm */
#endif
			*mars_comm6 = 0xB101;                    /* announce: ISR arms */
		}
		/* TORN-PACKET FEEDBACK consume: the master's harvest posts
		 * 0xBAD1 on COMM8 when a real landing tore (landed>0, packet
		 * rejected). Re-mark everything that push carried: pal ids
		 * back into the dirty bitmap, rowscroll prev invalidated so
		 * it re-ships. Regs and records ride every push anyway. */
		if (*mars_comm8 == 0xBAD1) {
			volatile uint8_t *pdq = (volatile uint8_t*)0xFFBA00;
			/* both the last and the previous push (lost-push belt) */
			for (uint8_t h = 0; h < 2; h++) {
			volatile uint8_t *lp = (volatile uint8_t*)(h ? 0xFFA044 : 0xFFA0C0);
			uint8_t lk = lp[0];
			if (lk <= 15) {
				for (uint8_t j5 = 0; j5 < lk; j5++) {
					uint8_t idb = lp[1 + j5];
					if (idb < 64) {
						pdq[idb >> 3] |= (uint8_t)(1u << (idb & 7));
#ifdef PAL_DELTA
						/* the torn push updated the shadow for these
						 * blocks; a delta re-ship would diff against
						 * a lie. Force raw. */
						pal_force[idb >> 3] |= (uint8_t)(1u << (idb & 7));
#endif
					}
				}
				if (lp[1 + lk])
					*(uint16_t*)0xFFA400 = 0xFFFF;  /* rs prev poisoned */
			}
			}
			*mars_comm8 = 0;
			(*(volatile uint16_t*)0xFFA0B2)++;   /* re-mark events */
		}
		/* HEAL CHANNEL (PALSTATIC v1.1a): the SH-2 posts 0xBAD2 with
		 * every scene load. An SH-2-side PAL_SH load is INVISIBLE to
		 * the PALDELTA shadow — zero heal deltas, so a wrong load
		 * stands forever (the v1 pull). Consume: re-mark ALL 64 pal
		 * blocks + force raw. 64 dirty trips the storm clamp
		 * (kcap=15) -> full raw re-ship, shadow re-synced, in ~9
		 * vints. A RIGHT load eats the same storm push once per
		 * scene cut — invisible, same class as the load-in storm.
		 * No clobber race with 0xBAD1: per vint the order is strict
		 * (consume -> announce -> push -> landing -> harvest ->
		 * post) and the two post sites are exclusive branches of one
		 * harvest. */
		else if (*mars_comm8 == 0xBAD2) {
			volatile uint8_t *pdq = (volatile uint8_t*)0xFFBA00;
			for (uint8_t j6 = 0; j6 < 8; j6++) {
				pdq[j6] = 0xFF;
#ifdef PAL_DELTA
				pal_force[j6] = 0xFF;
#endif
			}
			*mars_comm8 = 0;
			(*(volatile uint16_t*)0xFFA0D6)++;   /* heal posts consumed */
		}
#ifdef BOOT_FBXFER
		/* the FB-transport verdict rides COMM8 like every other master
		 * message and is CLEARED here. v1 parked 0xBBxx on the channel
		 * permanently instead, and the hardware screen went black:
		 * COMM8 never reads 0 again, so the master's pended posts (and
		 * with them the arm echo the push gates on) stop. Latch it into
		 * the value instrument's slot and free the channel. */
		else if ((*mars_comm8 & 0xFF00) == 0xBB00) {
			*(volatile uint16_t*)0xFFA186 =
				(uint16_t)(0xF000 | (*mars_comm8 & 0xFF));
			*mars_comm8 = 0;
		}
#endif
#ifdef GLOW_MASK
		else if (*mars_comm8 == 0xBAD3) {
			glow_live = 0;                       /* animator yielded */
			*mars_comm8 = 0;
		}
		else if (*mars_comm8 == 0xBAD4) {
			glow_live = 1;                       /* animator running */
			*mars_comm8 = 0;
			(*(volatile uint16_t*)0xFFA0D8)++;   /* grants (diag) */
		}
#endif
#ifdef MDSPR
		/* PER-SCENE SPRITE ART (docs/design/BOSSFIGHT.md): 0xBA50|scene = start
		 * the chunked cart->VRAM re-upload of that scene's blob.
		 * The SH-2 suspends claims for 30 vints; ~11 chunks of 512
		 * words at ~8 lines each ride the vint during the cut. */
		else if ((*mars_comm8 & 0xFFFE) == 0xBA50) {
			uint8_t s9 = (uint8_t)(*mars_comm8 & 1);
			if (s9 < MDSPR_NSCENES) {
				mdspr_up_woff = mdspr_scene_blob[s9][0];
				mdspr_up_left = mdspr_scene_blob[s9][1];
				mdspr_up_voff = 0;
				(*(volatile uint16_t*)0xFFA0DA)++;   /* uploads (diag) */
			}
			*mars_comm8 = 0;
		}
#endif
#ifdef FB_SPR_READ
		/* S1 STRIKE: records -> FB_SPR at vint top, where FM=0 is
		 * guaranteed. The game's own upload cannot do this - it runs
		 * inside the master's FM=1 span and ares discards those FB
		 * writes (the LOOP24 grave, revisited and re-measured).
		 * Copy through the terminator; the pre-flip snapshot reads
		 * this same vint. FM=1 here (overrun) = skip: the FB keeps
		 * last frame's coherent list - stale beats torn. */
		if (!(*(volatile uint16_t*)0xA15100 & 0x8000)) {
			const uint16_t *sp2 = (const uint16_t*)0xFF7000;
			volatile uint16_t *dp = (volatile uint16_t*)0x85E000;
			for (uint16_t i = 0; i < 64; i++) {
				uint16_t r2 = sp2[2];
				dp[0] = sp2[0]; dp[1] = sp2[1];
				dp[2] = r2;     dp[3] = sp2[3];
				dp[4] = sp2[4]; dp[5] = sp2[5];
				dp[6] = sp2[6]; dp[7] = sp2[7];
				if (r2 & 0x8000)
					break;
				sp2 += 8; dp += 8;
			}
		}
#endif
#ifdef MD_BG
		/* phase stamps: beam line at each boundary (last-value
		 * telemetry, 0xFFA080..88) — the post-delay budget autopsy */
		/* deferred palette block (see md_consume): first thing at vint
		 * top, from the WRAM hold, before any name-table work */
		if (*(volatile uint16_t*)0xFFA160) {
			uint32_t src = 0xFFA100uL >> 1;
			*(volatile uint16_t*)0xFFA160 = 0;
			*(volatile uint16_t*)VDP_CTRL_PORT = 0x9330;
			*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
			*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | (src & 0xFF));
			*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((src >> 8) & 0xFF));
			*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((src >> 16) & 0x7F));
			*vdp_ctrl_wide = ((uint32_t)(0xC000u | 32u) << 16) | 0x80u;
		}
#if defined(TWO_POST) && !defined(TP_CONSUME_FIRST)
		if (!r60_go)                         /* posting first: the consumes
		                                      * run after the flip echo */
#endif
		{
		*(volatile uint16_t*)0xFFA080 = *(volatile uint16_t*)0xC00008;
		md_consume(0x851A00uL);
		*(volatile uint16_t*)0xFFA082 = *(volatile uint16_t*)0xC00008;
		/* (md_stage_play retired here: R60 consumes are direct-DMA;
		 * 0xFFA400 now holds the rowscroll prev-ship copy) */
		*(volatile uint16_t*)0xFFA084 = *(volatile uint16_t*)0xC00008;
		md_consume(0x85E800uL);
		*(volatile uint16_t*)0xFFA086 = *(volatile uint16_t*)0xC00008;
#ifdef MDSPR
		mdspr_consume();                     /* P3: SAT + sprite pal */
		mdspr_upload_pump();                 /* per-scene art chunks */
#endif
		}
#endif
#ifdef POST_LATE
		/* POST LATE (2026-09-06, the 60Hz fix): the post/push move to
		 * r60_late_post(), called from the game IRQ4's rte path
		 * (md_start.s fmgate_ret). The game's vint upload then runs at
		 * FM=0 and never spins on its FM gates; the ISR flip becomes the
		 * body's deferred flip (one frame of constant latency). */
		if (r60_go) { r60_late = 1; r60_late_v = v_entry; }
#else
#ifdef GATE_FREE
		/* LOOP29 181. GAMEGATE's release lives INSIDE `if (r60_go)`, the
		 * window path, so it can only fire on a window vint -- which is
		 * why GAMEGATEWAIT=1 still measured the game at 50% of vints
		 * (entry 180 claimed it decoupled them; it did not). Release on
		 * a vint with NO window too, and the game's logic and input run
		 * on vblank as the arcade's do, at 60 Hz, whatever rate the
		 * display manages underneath. Mike, 2026-09-11: "we get our
		 * player missing frames, but we dont slow down gameplay to catch
		 * up. so this is the right progression." */
		if (!r60_go) {
			*(volatile uint8_t*)0xFFA0F5 = 1;
			(*(volatile uint16_t*)0xFFA0F6)++;
			(*(volatile uint8_t*)0xFFA0F4)++;    /* counted as a fallback */
		}
#endif
		if (r60_go) {
			{
#ifdef BOOT_FBX_A
				/* CAN THE 68K CARRY THE PACKET THROUGH THE FB?
				 * (2026-09-08, HANDOFF-DREQ job 1.) A 68K word into the
				 * DREQ FIFO costs ~2.4 lines on hardware, into the FB
				 * ~0.1 — but the FB probe that measured that never READ
				 * THE DATA BACK, and the sentinel probe that tried lost
				 * 4 of 4 regions (suspected bank parity across the ISR
				 * flip at the post).
				 * Write the same 13-word test packet TWICE, in the two
				 * places the real transport could sit:
				 *   A at 0x12000, HERE, at FM=0 and BEFORE the post/flip
				 *   B at 0x12040, in r60_push, at FM=1 and AFTER it
				 * word[0] is a per-vint sequence, words[1..12] = q, so
				 * the master can tell a FRESH write from last window's
				 * (constant values cannot — that is what would have made
				 * a naive readback lie). */
				fbx_seq++;
				{
					volatile uint16_t *fa = (volatile uint16_t*)0x852000;
					PSTAMP(0xFFA182);
					fa[0] = fbx_seq;
					for (uint16_t q = 1; q < 20; q++) fa[q] = q;
					PSTAMP(0xFFA184);
				}
#endif
#if defined(FBX_STAGE) && defined(FBX_BOTH)
				/* THE OTHER BANK (LOOP28 91). FM is still down here —
				 * it goes up on the next line — so the framebuffer is
				 * ours, and a flip may have swapped banks since the
				 * tail write. Same packet, same sequence. */
				r60_blast(0);
#endif
#if defined(FBX_STAGE) && defined(FBX_PEND)
				/* FBXPEND: the tail found FM up and left the packet
				 * staged; FM is 0 here (the consumes above needed it),
				 * so this is the one blast that packet gets. */
				if (fbx_pend) { fbx_pend = 0; r60_blast(1); }
#endif
#if defined(FB_XPORT) && !defined(FBX_TAIL) && !defined(FBX_STAGE)
				/* THE PUSH MOVES AHEAD OF THE POST (LOOP27 67). It has
				 * to: the 68K cannot reach the framebuffer at FM=1.
				 * Push-before-post was tried and reverted in August
				 * because the push was ~90 lines of FIFO writes and the
				 * flip then never made vblank — through the FB it is
				 * ~2, so the objection is gone with the FIFO.
				 *
				 * It is NOT ~2 lines: the ~56-line BUILD is in here too,
				 * and that is what drives V-at-post to 43 and the flip
				 * to 0.3 Hz. FBX_STAGE takes the build out of this
				 * window entirely (LOOP28 89) and adds nothing ahead of
				 * the post. */
				r60_push();
#endif
				*(volatile uint16_t*)0xA15100 |= 0x8000;
				#ifdef TXT_MASK
				/* mask in the high byte; every 8th vint force it full */
				*mars_comm2 = (uint16_t)(BANK_SHADOW
				              | ((uint16_t)(txt_mask | ((*(volatile uint16_t*)0xFFB0F0 & 7) ? 0 : 0xFF)) << 8));
#else
				*mars_comm2 = BANK_SHADOW;
#endif
				*mars_comm12 = (uint16_t)(0xD000 | v_entry);
				#ifdef MD_ROUND
		/* LOOP29 193: carry the GAME'S ROUND in COMM10 bits 13-15. The
		 * low 13 are the tile-dirty mask and the SH-2 masks with 0x1FFF
		 * (187), so the top three are free -- three bits for five
		 * rounds. 0xFFF142 is the game's own scene variable
		 * (LOOP-DECOMPILE 66: `move.b $FFF142,d0 ; the scene index`).
		 * The SH-2 cannot read 68K wram, so this is the only way it can
		 * know which round's palette table to install. */
		/* LOOP29 222: MASK the dirty word. It is 16 regions wide and
		 * regions 13-15 (the actor lines) are dirty whenever a sprite
		 * palette moves, so the round in bits 13-15 read as round|dirt
		 * on the SH-2 -- a wrong table installed on the same-round
		 * return (vi66b: black rectangles in level 1's tree row). */
		*mars_comm10 = (uint16_t)((*(volatile uint16_t*)0xFFB9FE & 0x1FFFu)
			| ((uint16_t)(*(volatile uint8_t*)0xFFF142 & 7) << 13));
#else
		*mars_comm10 = *(volatile uint16_t*)0xFFB9FE;
#endif
#ifdef ARM_GATE
				/* COMM4 was cleared at the announce, so a 0xA001 here is
				 * THIS vint's arm (the ISR usually arms during our
				 * consumes). No stale-echo clear: it wiped that echo and
				 * a previous vint's echo could pass for a fresh one. */
				uint8_t armed_ok = (*mars_comm4 == 0xA001);
#else
				*mars_comm4 = 0;                     /* stale echo clear */
#endif
				/* (push-before-post TRIED AND REVERTED same-day: the
				 * push is ~90 lines of 68K bus writes — CPU-bound, not
				 * drain-bound — so the post waited ~100 lines and the
				 * flip NEVER made vblank: 8 flips in 1602 vints. The
				 * push must overlap the flip span; the real fix is a
				 * smaller packet.) */
				*mars_comm0 = 0x2020;                /* post: ISR flips */
				*(volatile uint16_t*)0xFFA0A0 =
					*(volatile uint16_t*)0xC00008;   /* V at post */
				(*(volatile uint16_t*)0xFFB0F2)++;
				/* (hold-for-F103-then-push TRIED AND REVERTED: handler
				 * 89->107 lines and the push did not speed up — the
				 * per-word cost is the 68K's own two adapter accesses,
				 * not FIFO-drain starvation.) */
				*(volatile uint16_t*)0xFFA0AA =
					*(volatile uint16_t*)0xC00008;   /* V pre-push */
#ifdef FM_LATE
				/* FM LATE (2026-09-06, the 60Hz lever): game code must never
				 * run with FM up (its gated text/tile writers spin until the
				 * master's ack). So: wait for the ISR's restore-done (F103,
				 * or a declined flip F1FF), drop FM, push at FM=0 (the DREQ
				 * landing needs no FB), raise FM for the master's blit +
				 * publish, wait for its ack HERE, then the game's IRQ4. */
#define FML_V()  ((uint8_t)(*(volatile uint16_t*)0xC00008 >> 8))
#define FML_PAST(line)  ({ uint8_t _v = FML_V(); (_v < 0xDF && _v >= (line)); })
				/* both waits bounded by the BEAM (an unarmed pipeline never
				 * echoes; an iteration bound hung the boot for seconds) */
				/* LOOP 27 entry 7 — ARM GATE: never push into an unarmed
				 * DMA. The ISR (or the master's ack site) echoes 0xA001
				 * after arming; without it the words land on the previous
				 * transfer's counter (torn, displaced). Bounded: no echo
				 * = no packet this vint (a stale frame, not a tear + belt
				 * storm). */
#ifdef ARM_GATE
				if (!armed_ok) {
					uint16_t ga = 600;               /* ~1.5 lines: the body services a late announce within a line */              /* late arm (ack-site or a slow ISR) */
					while (*mars_comm4 != 0xA001 && --ga) ;
					if (ga)
						armed_ok = 1;
					else
						(*(volatile uint16_t*)0xFFB0CE)++;   /* pushes skipped: unarmed */
				}
				if (armed_ok)
#endif
#ifndef FB_XPORT
				r60_push();
#else
				(void)0;                 /* FB route: shipped at FM=0 */
#endif
#ifdef FBX_STAGE
				/* THE BUILD, OFF THE FM=0 WINDOW (LOOP28 89). Every
				 * word goes to WRAM, so FM=1 does not apply and this
				 * overlaps the master's blit, which we wait out below
				 * either way. r60_blast() moves it to the framebuffer
				 * at the tail.
				 *
				 * OUTSIDE the arm gate on purpose: arming is a DREQ
				 * concern and the FB route has no DMA to arm. The
				 * FBXPORT line builds unconditionally today and this
				 * must not quietly start dropping packets on a vint
				 * the ISR was slow to echo. */
#ifndef TWO_POST
				r60_push();                      /* TWO_POST: after post B */
#endif
#endif
				/* (FM_LATE arm: selection overlaps the ISR span; the
				 * F103 wait + FM drop sit before its ship) */
				*(volatile uint16_t*)0xFFA0AC =
					*(volatile uint16_t*)0xC00008;   /* V post-push */
				while (*mars_comm0 != 0 && !FML_PAST(0x60)) ;   /* master ack: FM down */
				/* no ack in time (unarmed pipeline, overrun): drop FM ourselves —
				 * the game's IRQ4 upload gate spins at level 4 on FM=1 and no
				 * vint can ever come to end it (the boot deadlock, measured) */
				*(volatile uint16_t*)0xA15100 &= 0x7FFF;
				*(volatile uint16_t*)0xFFA184 =
					*(volatile uint16_t*)0xC00008;   /* V at ack */
#else
				/* LOOP 27 entry 7 — ARM GATE: never push into an unarmed
				 * DMA. The ISR (or the master's ack site) echoes 0xA001
				 * after arming; without it the words land on the previous
				 * transfer's counter (torn, displaced). Bounded: no echo
				 * = no packet this vint (a stale frame, not a tear + belt
				 * storm). */
#ifdef ARM_GATE
				if (!armed_ok) {
					uint16_t ga = 600;               /* ~1.5 lines: the body services a late announce within a line */              /* late arm (ack-site or a slow ISR) */
					while (*mars_comm4 != 0xA001 && --ga) ;
					if (ga)
						armed_ok = 1;
					else
						(*(volatile uint16_t*)0xFFB0CE)++;   /* pushes skipped: unarmed */
				}
				if (armed_ok)
#endif
#ifndef FB_XPORT
				r60_push();
#else
				(void)0;                 /* FB route: shipped at FM=0 */
#endif
#ifdef FBX_STAGE
				/* THE BUILD, OFF THE FM=0 WINDOW (LOOP28 89). Every
				 * word goes to WRAM, so FM=1 does not apply and this
				 * overlaps the master's blit, which we wait out below
				 * either way. r60_blast() moves it to the framebuffer
				 * at the tail.
				 *
				 * OUTSIDE the arm gate on purpose: arming is a DREQ
				 * concern and the FB route has no DMA to arm. The
				 * FBXPORT line builds unconditionally today and this
				 * must not quietly start dropping packets on a vint
				 * the ISR was slow to echo. */
#ifndef TWO_POST
				r60_push();                      /* TWO_POST: after post B */
#endif
#endif
				*(volatile uint16_t*)0xFFA0AC =
					*(volatile uint16_t*)0xC00008;   /* V post-push */
#endif
				while (*mars_comm4 != TP_ECHO_OK
				       && *mars_comm4 != TP_ECHO_NO) {   /* flip-hold tail */
					uint8_t vv = (uint8_t)
						(*(volatile uint16_t*)0xC00008 >> 8);
					if (vv > 0xF8 || vv < 0xDF)
						break;
				}
				*(volatile uint16_t*)0xFFA09E =
					*(volatile uint16_t*)0xC00008;   /* V at hold exit */
				*(volatile uint16_t*)0xFFA0A2 = *mars_comm4;
#ifdef TXT_MASK
				{	/* the master captured with this post's mask: start afresh.
					 * No echo (bailed vint) = keep accumulating. */
					uint16_t c4m = *mars_comm4;
					if (c4m == 0xF102 || c4m == 0xF103 || c4m == 0xF1FF
					    || c4m == 0xF104 || c4m == 0xF1FE) txt_mask = 0;
				}
#endif
#ifdef GAME_GATE
				/* THE GO TOKEN (LOOP29 141): one game frame per presented
				 * frame. F102 = the ISR flipped this vint. The fallback
				 * keeps loads, blanks and a stalled compose from freezing
				 * the game. 0xFFA0F6 counts releases, 0xFFA0F4 fallbacks. */
				{
					static uint8_t gg_wait;
					uint16_t c4 = *mars_comm4;   /* F102 = flipped; F103 = flipped
					                              * and restored (the ISR writes both) */
					uint8_t flipped = (c4 == 0xF102 || c4 == 0xF103 || c4 == 0xF104);
					if (flipped || ++gg_wait >= GAMEGATE_MAXWAIT) {
						if (!flipped) (*(volatile uint8_t*)0xFFA0F4)++;
						*(volatile uint8_t*)0xFFA0F5 = 1;
						(*(volatile uint16_t*)0xFFA0F6)++;
						gg_wait = 0;
					}
				}
#endif
#ifdef TWO_POST
				/* TWO-POST (LOOP29 149): the master flipped on post A and
				 * dropped FM. Consumes and the packet blast here, at FM=0;
				 * then raise again and post B for the window. If the body
				 * flipped instead and FM is still up, md_consume skips
				 * itself and the blast goes pending (late blast / next
				 * pre-post), exactly the FBXPEND paths. */
#ifndef TP_CONSUME_FIRST
				*(volatile uint16_t*)0xFFA080 = *(volatile uint16_t*)0xC00008;
				md_consume(0x851A00uL);
				md_consume(0x85E800uL);
#ifdef MDSPR
				mdspr_consume();
				mdspr_upload_pump();
#endif
				*(volatile uint16_t*)0xFFA086 = *(volatile uint16_t*)0xC00008;
#endif
#ifdef FBX_STAGE
				fbx_pend = 1;                        /* blast in game context
				                                      * (the gate spin), not here:
				                                      * post B must not wait ~12
				                                      * lines of FB writes */
#endif
				*(volatile uint16_t*)0xA15100 |= 0x8000;
				*mars_comm0 = 0x2020;                /* post B: the window */
				r60_push();                          /* the build, after post B (WRAM
				                                      * only; the window runs under
				                                      * it). Blasted next vint in the
				                                      * consume window, or late. */
#endif
#if defined(FBX_STAGE) && !defined(TWO_POST)
				/* THE BLAST, AT THE TAIL (LOOP28 89). Same window
				 * FBX_TAIL uses and for the same reason — the master
				 * dropped FM at its ack, so the 68K can reach the
				 * framebuffer here, and it costs the post nothing
				 * because the post already happened.
				 *
				 * What is different from FBX_TAIL is what runs here.
				 * FBX_TAIL put the whole ~56-line build in this window
				 * and the game's IRQ4 waited for all of it: 49.1%
				 * against 60.7 without. Only the copy is here now. */
#ifdef FBX_PEND
				/* FBXPEND: a 68K FB write at FM=1 is DROPPED (ares
				 * bus-external.cpp:45, FPGA IF.sv:946), and FM can only
				 * RISE from this CPU, so a blast that starts at FM=0
				 * completes at FM=0. Blast now if we can; otherwise hold
				 * the staged packet for the pre-post slot next vint. */
				if (*(volatile uint16_t*)0xA15100 & 0x8000)
					fbx_pend = 1;
				else
					r60_blast(1);
#else
				r60_blast(1);
#endif
#endif
#ifdef FBX_TAIL
				/* PUSH AT THE TAIL, NOT BEFORE THE POST (LOOP27 75).
				 * Measured: pushing before the post moves V-at-post from
				 * 240 (inside vblank, where the ISR can still flip) to 34
				 * (line 34 of active display), and the flip rate falls
				 * 27.3 Hz -> 1.2. The push has to be at FM=0, but this
				 * is FM=0 too — the master dropped it at its ack — and
				 * it costs the post nothing. The packet then waits in the
				 * framebuffer for the next window, one vint of latency,
				 * which the harvest already tolerates (a stale publish
				 * yields landed = 0 and last frame's records stand). */
				r60_push();
#endif
			}
		}
#endif
	}
#else  /* legacy */
	static uint16_t wskip;
	{
		uint32_t spin2;
		uint16_t wcmd;
		// 3-phase cycle, NO dedicated compose vint: every window blits
		// one 75-row slice inside vblank, then composes the next
		// frame's sprites/text into already-shipped rows (row-following
		// pipeline; tiles fill in concurrently between windows on the
		// SDRAM cache). Full frame ships every 3 vints = 20Hz display.
#ifdef WIN_TWO
		/* 2-WINDOW CYCLE (LOOP16): post k1, k2, then one IDLE vint —
		 * no window, no push, the game keeps the whole vint (the
		 * master composes all three bands in the gap). Rejects retry
		 * their slot; the idle beat follows an ACCEPTED k2 only. */
		uint16_t next = wskip + 1;
		if (next >= 4) next = 1;
#ifdef CUT_30
		/* LOOP 17 4b: NO IDLE BEAT — k1,k2,k1,k2 = 2 vints/cycle =
		 * 30Hz display, half the animation step. The game loses the
		 * vint it used to keep whole, so this trades 68K time for
		 * cadence; the falsifier is skips/rejects climbing and bands
		 * shipping stale. */
		if (next == 3) next = 1;
#else
		if (next == 3) {
			wskip = 3;                       /* idle beat consumed */
			goto window_done;
		}
#endif
		wcmd = (uint16_t)(0x2000 | (next << 4));
#else
		uint16_t next = wskip + 1;
		if (next >= 3) next = 0;
		wcmd = (uint16_t)(0x2000 | (next << 4));
#endif
#if defined(K2_FREE) && defined(MD_BG)
		/* LOOP 24 K2FREE — the MD-plane consumes live HERE, at k1
		 * ENTRY, pre-raise. FM=0 is guaranteed (the previous window's
		 * FM fell at the SH-2's ack mid-frame; a still-live overrun was
		 * belt-waited above and md_consume's own FM guard no-ops the
		 * residue). Consume is STAGED — it reads the FB into the 0xFFA400
		 * buffer, no VDP port writes — so it needs no vblank; the
		 * md_stage_play right after this vint's post flushes it
		 * in-vblank. stage_play between the two consumes: the staging
		 * holds one packet (the v8 collision rule). The V BUDGET bounds
		 * how far this delays the k1 raise/post (v3's grave was a
		 * ~200-line displacement; this is <=~20 lines, and the ISR's
		 * arming wait was widened to match): past 0xF6 — or wrapped out
		 * of the E-range entirely — packet B waits for the next k1.
		 * A deferred B may be OVERWRITTEN by the SH-2's next publish
		 * (one lost MD-plane packet): tolerated, the nt builder's
		 * rotating force-full row heals losses; counted at 0xFFB0EE
		 * (free in K2FREE builds — IDLE_TOKEN owns it otherwise). */
		if ((wcmd & 0x00F0) == 0x0010) {
			/* PRE-ANNOUNCE k1 on COMM6 (boot-heartbeat register, free
			 * at runtime) BEFORE the consumes: the k1 post lands
			 * ~12-55 lines late behind them, far past any sane ISR
			 * wait, and a missed arm kills that cycle's packet (first
			 * flip-hold run: k1 armed only 56% of cycles, and the c10
			 * insurance recaptures fed the k2 drain — flip-late 23.5%).
			 * Arming needs no post; the ISR consumes the announce
			 * (clears COMM6) and echoes 0xA001 for the push gate. */
			*mars_comm6 = 0xB101;
			md_consume(0x851A00uL);
			md_stage_play();
			{
				/* the V-budget was protecting the ISR's arm timing —
				 * the COMM6 pre-announce already solved that, so the
				 * tight 0xF6 bound only STARVED the pipeline: on cold
				 * boot the fat tile packets blew it every cycle
				 * (consume-B deferred 55%, publishes and builds
				 * deferred behind it) and the splash stayed BLACK at
				 * a 1/5-rate fill until reset. Consume-B now always
				 * runs unless the vint is pathologically deep into
				 * the frame (~96 lines — never seen; worst measured
				 * consume span is 51). The k1 post shifts by the
				 * consume span; blits are hidden-bank (vblank-free)
				 * and the compose launches absorb it — MAME band
				 * skips gate the claim. */
				uint8_t vv = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);
				if (vv >= 0xDF || vv < 0x40)
					md_consume(0x85E800uL);
				else
					(*(volatile uint16_t*)0xFFB0EE)++;
			}
		}
#endif
		// EARLY-VBLANK GATE for blit phases: this vint fires at vblank
		// start ONLY when the previous window didn't overrun the frame.
		// If it did, the pending vint fires at rte MID-FRAME — and a
		// mid-frame FBCTL flip is catastrophic on deferred-latch
		// hardware (ares collapses blind toggles; the display lands on
		// the staging bank = the perpetual flashing). Never touch FBCTL
		// outside vblank: require the MD VDP's live V counter to sit in
		// the first ~7 lines of vblank (0xE0-0xE6 NTSC V28) and RETRY
		// the same blit phase at the next vint otherwise. The V counter
		// (not the 32X VBLK bit) because MAME sets its VBLK flag in a
		// 32X callback that can run AFTER the 68K enters this handler —
		// the bit reads 0 at vint entry there and gated every blit.
		// Compose windows don't flip — ungated.
		if (wcmd != 0x2100) {
			uint16_t hv = *(volatile uint16_t*)0xC00008;
			uint8_t v = hv >> 8;
			*(volatile uint16_t*)0xFFB0FE = hv;      // diag: HV at vint
#ifdef K2_FREE
			/* THE GATE READS ENTRY V, NOT LIVE V. The k1-entry
			 * consumes above burn ~11-20 lines BEFORE this check;
			 * live V then reads past E8 and the gate rejected its
			 * own window — first K2FREE ares run: rejects 40.6%,
			 * cadence 3.48, the reject-retry lock. The delay is
			 * SELF-INFLICTED (the vint entered on time) and the k1
			 * window has no vblank-bound work (blits write the
			 * hidden bank; the flip is k2's, undelayed): gate on
			 * the V this vint ENTERED with. */
			v = v_entry;
#endif
			// on-time vint: V reads 0xDF (counter not yet stepped past
			// line 223 at IRQ time — MAME-measured). Upper bound is
			// TIGHT (0xE2, ~4 lines in): 75-row slices starting at
			// V=0xE5-0xE6 left only ~1.7ms of vblank and missed the
			// restore on ares ~0.5% of frames (black frame each time,
			// field-measured 11/2063). A late start now retries next
			// vint instead of gambling the flip-back.
#ifdef FM_GATE
			/* LOOP 23: the game's main loop RUNS now, including its
			 * own IRQ-masked critical sections — vint entry lands
			 * past E2 ~10% of vints (V state: rejects 10.6%, cadence
			 * 2.29). The E2 bound protected pres-1.0's 75-row blit
			 * slices; pres-2.0 needs only the flip inside vblank and
			 * tolerates a deferred latch. Accept through E8: the k2
			 * flip still lands in vblank at the consume mean, and the
			 * flip-late-latch counter prices the tail. */
			if (v < 0xDF || v > 0xE8) {
#else
			if (v < 0xDF || v > 0xE2) {
#endif
				(*(volatile uint16_t*)0xFFB0FC)++;   // diag: gate skips
				goto window_done;
			}
		}
		wskip = next;
#ifdef IDLE_TOKEN
		// LOOP 11 — POLL AND SKIP (Knuckles' Chaotix, per-frame path):
		// `tst.w COMM0 / beq take-it / rts`. If the SH-2 is not parked and
		// ready, DO NOT raise FM and spin — return and try the next vint.
		// Chaotix's reasoning applies directly: a skipped update costs one
		// frame of staleness, a blocking wait costs a frame of game logic,
		// and ~79 of our ~210-line window/ack span is FM held while the
		// master has not even started.
		// STARVATION GUARD: the master is legitimately busy for long
		// stretches (build_maps is ~4ms and uninterruptible), so after
		// IDLE_SKIP_MAX consecutive skips take the window anyway and eat
		// the stall. Without this a busy master freezes the display.
		{
			static uint16_t idle_skips = 0;
#ifdef IDLE_GRACE
			// GRACE WINDOW. Pure poll-and-skip forfeits a WHOLE window
			// to a master that is usually one strip away from ready —
			// measured 546 skips of 5392 vints, and on ares that reads
			// as speed bought with chop. So wait, but only while the
			// flip is STILL LEGAL: V<=0xE2 is the same bound the vblank
			// gate above enforces, so a grace poll can never produce an
			// illegal flip, and a master that lands one line late costs
			// one line instead of a whole frame. FM is still 0 through
			// all of this — 68K time, not held FM, which is the whole
			// distinction the Chaotix protocol rests on. Compose
			// windows (0x2100) do not flip and have no V bound, so a
			// plain counter caps those.
			{
				uint32_t g = 40000UL;
				while (*mars_comm4 != 0x0EAD && --g) {
					if (wcmd != 0x2100 &&
					    (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8) > 0xE2)
						break;
				}
			}
#endif
			if (*mars_comm4 != 0x0EAD && idle_skips < 3) {
				idle_skips++;
				// 0xFFB0EE, NOT 0xFFB0FA: 0xFFB0F8 is a LONG (the
				// interrupted game PC, md_start.s:254), so it owns
				// 0xFFB0FA and overwrites it every vint. The first
				// attempt's skip counter read pure garbage.
				(*(volatile uint16_t*)0xFFB0EE)++;   // diag: idle-token skips
				goto window_done;
			}
			idle_skips = 0;
		}
#endif
#ifdef FM_GATE
		/* LOOP 23 v2: k1 raises AT ITS OLD VINT-ENTRY POSITION but
		 * does not spin. Two designs died first, both probe-caught:
		 * post-game-vint k2 (the flip missed vblank — heartbeats read
		 * V=0x04..0xF3) and post-game-vint k1 (the mid-frame window
		 * slipped the compose/blit launch deadlines — garbled sbuf
		 * rows even in lenient MAME; clean the moment k1 was skipped
		 * entirely). So the WINDOW TIMING is untouchable; only the
		 * WAIT goes. The game (vint handler + main loop) runs
		 * concurrent with the k1 window; its main-loop FB writers hit
		 * the entry gates; its VINT stores (sprite list + text regs)
		 * land during FM=1 and are SACRIFICIAL on hardware — they are
		 * rewritten every vint and consumed at 30Hz (k2 captures), so
		 * losing the k1-vint copies costs nothing the display could
		 * have shown. The k2 window keeps the old spin until the
		 * flip-span split (next step). */
#ifdef K2_FREE
		/* LOOP 24 K2FREE: EVERY vint takes the k1-shaped no-spin path —
		 * defer-if-live, MID-SPAN DEFER (now mandatory for k2 too: the
		 * game RUNS during the k2 window and a suspended mid-span FB
		 * writer would lose its resumed stores exactly as at k1), raise,
		 * post, return. The SH-2 owns FM's fall (it clears before its
		 * ack); the 68K never spins and never touches FM after raising.
		 * The V-ISR sees the post and runs the flip span at k2. */
		if (1) {
#else
		if ((wcmd & 0x00F0) != 0x0020) {
#endif
			if (*mars_comm0) {           /* previous window still live */
				fmgate_defer++;
				goto window_done;
			}
			{	/* span defer: a mid-loop FB writer suspended by this
				 * vint must finish before the SH-2 takes the bus —
				 * ares would drop its resumed stores, and captures
				 * would read half-written pages */
				uint32_t ipc = *(volatile uint32_t*)0xFFB0F8;
				const uint32_t *sp2 = fmgate_spans;
				while (*sp2) {
					if (ipc >= sp2[0] && ipc <= sp2[1]) {
						fmgate_defer++;
						goto window_done;
					}
					sp2 += 2;
				}
			}
			/* raise AT ENTRY — v3's raise-after-consume slipped the
			 * window by the consume span (mean 10.6 lines, MAX 70)
			 * and re-broke the compose deadlines. The k1 packet is
			 * consumed at the NEXT vint's pre-window call instead. */
			*(volatile uint16_t*)0xA15100 |= 0x8000;
			*mars_comm2 = BANK_SHADOW;
#ifdef K2_FREE
			/* heartbeat = ENTRY V, same reasoning as the gate above:
			 * the k1 raise sits after the consumes, and a live-V
			 * heartbeat past 0xEA would make the SH-2's skip gate
			 * drop the k1 BLITS (scmd bit 3 -> the slave skips its
			 * half too). k2 is undelayed, entry V is live V there. */
			*mars_comm12 = (uint16_t)(0xD000 | v_entry);
#else
			*mars_comm12 = (uint16_t)(0xD000
				| (*(volatile uint16_t*)0xC00008 >> 8));
#endif
			#ifdef MD_ROUND
		/* LOOP29 193: carry the GAME'S ROUND in COMM10 bits 13-15. The
		 * low 13 are the tile-dirty mask and the SH-2 masks with 0x1FFF
		 * (187), so the top three are free -- three bits for five
		 * rounds. 0xFFF142 is the game's own scene variable
		 * (LOOP-DECOMPILE 66: `move.b $FFF142,d0 ; the scene index`).
		 * The SH-2 cannot read 68K wram, so this is the only way it can
		 * know which round's palette table to install. */
		/* LOOP29 222: MASK the dirty word. It is 16 regions wide and
		 * regions 13-15 (the actor lines) are dirty whenever a sprite
		 * palette moves, so the round in bits 13-15 read as round|dirt
		 * on the SH-2 -- a wrong table installed on the same-round
		 * return (vi66b: black rectangles in level 1's tree row). */
		*mars_comm10 = (uint16_t)((*(volatile uint16_t*)0xFFB9FE & 0x1FFFu)
			| ((uint16_t)(*(volatile uint8_t*)0xFFF142 & 7) << 13));
#else
		*mars_comm10 = *(volatile uint16_t*)0xFFB9FE;
#endif
#ifdef K2_FREE
			if ((wcmd & 0x00F0) == 0x0020)
				*mars_comm4 = 0;     /* k2 ONLY: a stale 0xF102 from
				                      * the previous k2 would satisfy
				                      * the flip-hold instantly. At k1
				                      * this CLEARED the ISR's fresh
				                      * 0xA001 arm echo and the push
				                      * gate aborted every k1 push —
				                      * 1718 aborts, sprites frozen,
				                      * caught by the B0E0 counter. */
#endif
			*mars_comm0 = wcmd;
#ifdef MD_BG
			md_stage_play();
#endif
#ifdef K2_FREE
			if ((wcmd & 0x00F0) == 0x0020) {
				/* HOLD THROUGH THE FLIP (the design's "spin shrinks
				 * to ~the flip span"). First cut released the game at
				 * post+3 lines and the game's cart fetches ran under
				 * the master's cart-resident flip span: ISR span 82.9
				 * -> 124 lines, flip-late 26%, overrun-stale 276 (the
				 * old full spin was a bus-quiet guarantee nobody had
				 * written down). This spin polls a REGISTER from WRAM
				 * — zero cart traffic — and ends at the FBCTL write
				 * echo (0xF102), not the ack: mean ~15-25 lines vs the
				 * 68.2 it replaces. V-bounded: past 0xF8 (or wrapped
				 * out of vblank entirely) the ISR declined this vint's
				 * flip — release and let the body fallback decide. */
				while (*mars_comm4 != TP_ECHO_OK
				       && *mars_comm4 != TP_ECHO_NO) {
					/* 0xF1FF = the ISR DECLINED the flip (edge
					 * guard: too late in vblank — drop, not tear);
					 * release the game immediately either way */
					uint8_t vv = (uint8_t)
						(*(volatile uint16_t*)0xC00008 >> 8);
					if (vv > 0xF8 || vv < 0xDF)
						break;
				}
			}
#endif
			fmg_k2old = 2;               /* k1 accepted, spin-free */
			goto fmg_raised;
		}
#ifndef K2_FREE
		fmg_k2old = 1;
		/* v6: the flip gate reads the HEARTBEAT'S V, so write it at
		 * entry (truthfully: this vint was on time), consume the k1
		 * window's packet — its only FM=0 shim slot — and only then
		 * raise. The flip lands a few lines later but still inside
		 * vblank at the mean consume span; the worst case rides the
		 * deferred-latch path pres-2.0 already tolerates (counted in
		 * flip-late-latches). Skipping the consume here instead
		 * halved the packet rate and speckled the nametable with
		 * mixed generations (probe-measured, v5). */
		*mars_comm12 = (uint16_t)(0xD000
			| (*(volatile uint16_t*)0xC00008 >> 8));
#endif
		while (*mars_comm0) ;                    // drain any pending stream batch
		// (unpair model: RV is 0 permanently — no toggle here)
		// FM=1: hand the VDP (FB/CRAM) to the SH-2 for the window; FM
		// stays 0 outside so the GAME's staged writes land.
		*(volatile uint16_t*)0xA15100 |= 0x8000;
		*mars_comm2 = BANK_SHADOW;               // tile bank 1 value for renderer
		// Publish a live V-counter heartbeat (tag 0xD0xx): the master
		// decides flip-vs-skip from THIS, not from the 32X VBLK bit —
		// the only clock that's trustworthy on both MAME and ares when
		// a straggling tile third delays command pickup past the gate
		// check. MUST be written BEFORE the command is posted: the
		// previous window's final heartbeat is mid-frame stale, and the
		// master may read COMM12 the instant it sees COMM0 (the race
		// skipped nearly every blit — black bands + palette-drifted
		// stale slices).
#ifndef FM_GATE
		*mars_comm12 = (uint16_t)(0xD000
			| (*(volatile uint16_t*)0xC00008 >> 8));
#endif	/* FM_GATE: entry-V heartbeat written pre-consume above — the
		 * gate must see the on-time V, not the post-consume one */
		// PRESENTATION 2.0 — LIVE TILE-DIRTY WORD on COMM10 (free
		// post-boot; the pad publish it was named for was never built).
		// The DREQ word-80 copy of this bitmap is harvested post-window
		// and applied one window LATE; at the master's k2 flip that
		// skew would make its pre-flip truth capture read gap-written
		// pages from the wrong bank. Published BEFORE the post, so the
		// master reads a value complete through this vint (the game is
		// stalled until the ack). NOT cleared here — the DREQ push
		// below still owns harvest-and-clear.
		#ifdef MD_ROUND
		/* LOOP29 193: carry the GAME'S ROUND in COMM10 bits 13-15. The
		 * low 13 are the tile-dirty mask and the SH-2 masks with 0x1FFF
		 * (187), so the top three are free -- three bits for five
		 * rounds. 0xFFF142 is the game's own scene variable
		 * (LOOP-DECOMPILE 66: `move.b $FFF142,d0 ; the scene index`).
		 * The SH-2 cannot read 68K wram, so this is the only way it can
		 * know which round's palette table to install. */
		/* LOOP29 222: MASK the dirty word. It is 16 regions wide and
		 * regions 13-15 (the actor lines) are dirty whenever a sprite
		 * palette moves, so the round in bits 13-15 read as round|dirt
		 * on the SH-2 -- a wrong table installed on the same-round
		 * return (vi66b: black rectangles in level 1's tree row). */
		*mars_comm10 = (uint16_t)((*(volatile uint16_t*)0xFFB9FE & 0x1FFFu)
			| ((uint16_t)(*(volatile uint8_t*)0xFFF142 & 7) << 13));
#else
		*mars_comm10 = *(volatile uint16_t*)0xFFB9FE;
#endif
#if defined(CMD_PROBE) || defined(CMD_INT)
		// LOOP 11 — assert CMD INT to the primary SH-2 (d32xr src-md/
		// crt0.s:3143, `move.w #0x0001,0xA15102`). Purely additive: the
		// master still picks the window up by polling COMM0 exactly as
		// before, and the ISR only timestamps.
		// ORDER MATTERS AND THE FIRST VERSION HAD IT BACKWARDS: raised
		// AFTER the COMM0 post, the master (fast on MAME) had already
		// polled and picked the window up before the 68000 reached this
		// write, so the stamp it read was the PREVIOUS vint's and every
		// sample came out ~one frame (262 lines). Raise FIRST so the
		// timestamp precedes the signal it is timing.
		*(volatile uint16_t*)0xA15102 = 0x0001;
#endif
		*mars_comm0 = wcmd;
#ifdef MD_BG
		/* STAGED PLAYBACK — after the post (the SH-2's V-gate reads
		 * the heartbeat written above; the DMA halts only the 68K),
		 * inside vblank. See md_stage_play. */
		md_stage_play();
#endif
		spin2 = 8000000UL;
#ifdef FM_GATE
		/* no live comm12 refresh: a post-post refresh racing the SH-2's
		 * single gate read would replace the on-time entry V with the
		 * true (later) one and skip the flip */
		while (*mars_comm0 && --spin2) ;
#else
		while (*mars_comm0 && --spin2)
			*mars_comm12 = (uint16_t)(0xD000
				| (*(volatile uint16_t*)0xC00008 >> 8));
#endif
		*(volatile uint16_t*)0xA15100 &= 0x7FFF; // FM=0: game owns FB staging
#endif  /* !K2_FREE — the old k2 pre-raise/drain/spin/clear path; under
         * K2FREE every vint went through the k1-shaped branch above */
#ifdef FM_GATE
fmg_raised: ;
#endif
#ifdef MD_BG
#ifdef FM_GATE
		/* v8 — DOUBLE-BUFFERED PACKET: k1 publishes to A (0x11A00),
		 * k2 to B (0x1E800, the 2KB FB hole PAL32 couldn't use).
		 * BOTH consume HERE, post-flip, deadline-free — the pre-raise
		 * consume delayed the k2 flip, late flips extended the SH-2
		 * latch wait, those vints overran the frame and the NEXT
		 * vint's entry was rejected: the 8.6%-rejects / 2.27-cadence
		 * class (W state; gate-widening didn't move it, which is what
		 * named the overrun). stage_play between the two — it self-
		 * clears — so the stagings don't collide; V here is still
		 * vblank. On k1 vints FM=1 and both calls no-op. */
#ifndef K2_FREE
		md_consume(0x851A00uL);
		md_stage_play();
		md_consume(0x85E800uL);
#endif  /* K2FREE: consumes moved to k1 ENTRY (pre-raise, FM=0 there;
         * consume is STAGED so it needs no vblank) — here FM is still
         * 1 (the SH-2 owns its fall) and both calls would no-op. */
#else
		md_consume(0x851A00uL);
#endif
#endif
		// PRESENTATION 2.0: the FS-home wait is RETIRED, not rewritten.
		// It guarded the flip pair's restore edge, which could latch a
		// frame late (out-of-vblank deferral) with the game's staging
		// bank deselected. FS now moves exactly once per cycle, inside
		// the k2 window, in-vblank, and the MASTER verifies the latch
		// readback before it acks — by the time COMM0 clears, FS is
		// final for the cycle. Predicting the flip here instead
		// (fs_home^1 after k2) was considered and rejected: a master-
		// side V-gate skip means no flip, and the mispredicted wait
		// would burn its whole 200000-spin guard (~0.8s of 68K) every
		// time. The tail below still stores live FS in 0xFFB0F6 as the
		// diagnostic record.
		// (unpair model: RV stays 0 — the game fetches through 0x900000)
#ifdef FM_GATE
		/* k1-deferred vints DO NOT PUSH. The push/arm pairing is
		 * "every push lands on the re-arm its own window just made";
		 * a deferred k1's push would run BEFORE its window and land
		 * in an exhausted channel — it aborted after FPUSH(*bm) had
		 * already cleared the tile marks, and the lost marks were the
		 * garbled-tile corruption this comment replaces. Skipping is
		 * free under PKTSLIM: the k1 packet is only bitmap+tag+tail —
		 * marks accumulate in the OR-bitmap and ship on k2's push,
		 * and the SH-2's k1 re-reads the previous landing (all its
		 * applies are idempotent). */
#ifdef K2_FREE
		/* LOOP 24: every POSTED vint pushes — k1 carries records+prefix,
		 * k2 carries prefix+pal — because the V-ISR armed a PER-K landing
		 * buffer the instant it saw the post (the PKTSLIM-era "k1 drains
		 * into the k2 channel" hazard is structurally gone). Deferred and
		 * rejected vints jumped past this line: no post, no arm, no push. */
		window_ok = 1;
		(*(volatile uint16_t*)0xFFB0F2)++;
#else
		window_ok = (uint8_t)(fmg_k2old == 1);   /* k1 (==2) MUST NOT
		                              * push: its 4 words drain into the
		                              * still-armed k2 channel and bump
		                              * the k2-tail landing to 76 words
		                              * — whitelist-rejected WHOLE, pal
		                              * marks already consumed: the
		                              * black-sprite state (U_fmgate,
		                              * PAL_SH zeros, mirror full).
		                              * fmg_k2old is a 3-state, not a
		                              * bool — the first cut truthed it. */
		if (window_ok)
			(*(volatile uint16_t*)0xFFB0F2)++;
#endif
#else
		(*(volatile uint16_t*)0xFFB0F2)++;       // diagnostics: windows completed
		window_ok = 1;                           // master re-armed the DMA
#endif
window_done: ;
#ifdef K2_FREE
		/* a k1 announce whose window got rejected/deferred must not
		 * outlive its vint — a stale 0xB101 at the next (k2) vint
		 * makes the ISR arm k1 and skip the flip */
		*mars_comm6 = 0;
#endif
	}
#endif  /* !R60 — the legacy window/cadence machine */

	// ITER5 TAIL PROBE: V at the start of the per-vint tail (post-window).
	uint8_t v_win = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);

	// STAGED-PALETTE TRACER (temporary): border reports what the 68K sees
	// in the FB-staged palette at handler entry (FM=0, access bank):
	//   GREEN  = tile AND sprite palette halves nonzero (healthy)
	//   YELLOW = tile half ok, SPRITE half (0x85F800+) reads empty
	//   RED    = tile half empty too
	//   MAGENTA (sticky) = access-bank parity broke (bank tearing)
	{
		uint16_t fs = *(volatile uint16_t*)0xA1518A & 1;   // FM=0 here: readable
		// (fs/torn tracer retired; 0xFFB0F4 repurposed for the ITER5 TAIL
		// PROBE — max shim-handler span in scanlines, see below.)
		*(volatile uint16_t*)0xFFB0F6 = fs;  // steady FS: the render window's
		                                     // exit gate waits for this value
	}


	// MCU main-loop half: screen-sync handshake + sound mailbox pump
	if (!busy && MCU_BUSY)
		busy = 1;
	else if (busy && !TEXT_SYNC)
		busy = 0;

	uint8_t cmd = MCU_SNDCMD;
	if (cmd != 0xFF) {
		*mars_comm14 = 0x5000 | cmd;    // log sound command (no Z80 yet)
		MCU_SNDCMD = 0xFF;
	}

	// LOOP 8 — PALETTE DIRTY WORD (0xFFB9FC), one bit per 128-word region
	// of the 2048-word mirror. The game's own palette writes set the bits
	// (patch_game.py PAL_DIRTY_SITES: 45 write sites become jsr into MD-RAM
	// thunks that OR their region mask in, then run the displaced
	// instruction); the DREQ push below clears a bit as it ships that
	// region. This REPLACED a 512-word-per-vint diff scan of the mirror
	// against a 4KB sent-copy — 45 of the handler's 92 tail scanlines,
	// spent on 1024 MD-RAM reads that in steady state found nothing.
	// LOOP 6 negatives 3-5 closed every cheaper option (not division-
	// bound; long compares are free on a 16-bit bus; ablating the loop
	// body took the span 45.1 -> 0.1, so the loop WAS the whole cost).
	// (the word is read, and the shipped bit cleared, in the DREQ push
	// below — a region must only be cleared when it has actually gone.)

	// SPRITE LIST over DREQ FIFO: the game's own vint upload writes
	// sprite RAM through the FB window (remap 0x85E000) exactly while
	// the SH-2 blit owns the FB — ares/hardware DISCARD those writes
	// (savestate-proven 40/64 torn records: the "utterly broken"
	// sprites). The one reliable bulk channel is the DREQ FIFO. Walk
	// the game's order table (0xEC80) over its record buffer (0xF800)
	// — the exact 0x2B1E upload semantics — and push the ordered list
	// + terminator, padded to exactly 512 words (the DMAC's fixed
	// TCR). Chaotix protocol: length -> A15110, 68S via A15107=4,
	// 4-word groups gated on the FIFO-full sign bit. Bounded spins:
	// if the SH-2 side isn't armed (boot, mskip), abort and retry
	// next vint — the SH-2 keeps last frame's coherent list.
#ifdef K2_FREE
	if (window_ok) {
		/* ARM-ECHO GATE (LOOP 24): push only into a channel the V-ISR
		 * confirmed it armed for THIS post. Unarmed pushes wedge MAME's
		 * 68K (defer_access on a full FIFO nothing drains) and drop
		 * words uncounted on ares. The echo normally lands within a few
		 * hundred ticks — the ISR was already spinning on COMM0 when we
		 * posted; the belt below is for its ~1% nopost misses. */
		uint16_t want = (uint16_t)(0xA000 | wskip);
		uint16_t g2 = 2000;
		while (*mars_comm4 != want
		       && !(wskip == 2 && (*mars_comm4 == 0xF102
		                           || *mars_comm4 == 0xF1FF)) && --g2) ;
		/* k2: the flip echo (0xF102) OVERWRITES the arm echo and
		 * implies it — the ISR arms before it flips */
		if (!g2) {
			window_ok = 0;
			(*(volatile uint16_t*)0xFFB0E0)++;   /* push abort family */
		}
	}
#endif
	if (window_ok) {
		// Source: the game's own STAGED, ORDERED list — its vint
		// upload (0x2B1E) now lands in the MD RAM mirror at 0xFF7000
		// (patch_game sprite remap; the order table at 0xEC80 is
		// consumed by that upload and reads 0xFF afterward, so it
		// can't be walked here). This handler runs BEFORE the game's
		// IRQ code, so the pushed list is last vint's — coherent,
		// one frame stale, consistent.
		volatile uint16_t *fifo = (volatile uint16_t*)0xA15112;
		volatile int8_t  *ctrl = (volatile int8_t*)0xA15107;
		const uint16_t *s = (const uint16_t*)0xFF7000;
		// TOTAL spin budget for the whole push, not per group: a
		// slow-draining FIFO (emulator DMA service timing) could cost
		// up to 128x400 polls per vint WITHOUT ever timing out —
		// several ms of 68K time inside every vint = the game itself
		// running slow. ~800 total polls ≈ 0.1ms hard ceiling; an
		// exhausted budget aborts and retries next vint (the SH-2
		// keeps last frame's coherent list). 0xFFB0F2 counts aborts
		// (savestate-readable).
		// ~800 total polls ≈ 0.1ms hard ceiling; an exhausted budget aborts
		// and retries next vint (the SH-2 keeps last frame's coherent list).
		// LOOP 7b: raised 1200 -> 2600 (≈0.33ms). The packet grew 10% but
		// ares' dreq_incomplete grew FIVE-fold (9.1% -> 47.2% of cycles),
		// so the old budget was already marginal there and the growth
		// pushed it over. We just freed the 68K ~55 lines/vint; spending up
		// to 0.33ms of that to make the packet actually LAND is the trade.
		// Aborts count at 0xFFB0E0 — NOT 0xFFB0F2, which is the
		// windows-completed counter (they collided, so every abort figure
		// read before LOOP 7b was meaningless; iteration 1a found this same
		// collision once already).
		uint16_t spin = 2600;
		uint8_t ok = 1;
		static uint16_t txt_dma_base;
		// LOOP 7g — THE PACKET IS SPLIT, BY WINDOW PHASE. push_aborts has
		// read 0 for three ares passes running while dreq_incomplete sat at
		// 14-21% of cycles: the 68K pushes every word and the DMA still
		// fails to drain, so the transfer is simply too big. Splitting is
		// the fix the kickoff doc named ("if it climbs, SPLIT the packet
		// rather than grow it") and it is close to free, because 512 of the
		// 852 words were being THROWN AWAY two pushes in three — the sprite
		// list is only harvested at window k==1.
		//
		// The two layouts share an 82-WORD PREFIX so the SH-2 needs
		// almost no branching to decode them (and the added code has to
		// fit: .ramtext counts toward the 0x19000 region guard, which had
		// 72 bytes of headroom):
		//   0..19 regs | 20..79 rowscroll | 80 bitmap | 81 text base
		//   after w0    -> SPRITE, 596: 82..593 list      | 594..595 pad
		//   after w1/w2 -> TEXT,   340: 82..337 chunk     | 338..339 pad
		//
		// Mean payload 852 -> 425 words. Both lengths are multiples of 4
		// (149 and 85 groups): the FIFO drains in 4-word bursts and a
		// non-aligned count leaves the tail un-drained -> TE never sets.
		// Text at word 82 = byte 164 keeps the SH-2's longword copy aligned.
		//
		// The regs+rowscroll prefix is IDENTICAL in both, so the ordering
		// fix from LOOP 7b still holds: the 80 words the compose cannot
		// fake are the first 80 pushed, whatever the phase, and the SH-2
		// applies whatever fully landed (see the TCR0 read in m_main).
		//
		// NO TAG WORD. The master knows the layout from the phase it last
		// ran: pushes follow accepted windows 1:1 (window_ok is set only
		// after the ack), so remembering the previous k is exact and costs
		// nothing. A tag would also have to survive truncation to be worth
		// anything, and it would break the longword alignment above.
#ifdef R60
		uint16_t kk = 2;                      /* legacy push is dormant
		                                       * (window_ok stays 0); this
		                                       * only satisfies the dead
		                                       * code's references */
#else
		uint16_t kk = wskip;                  // the k just posted+acked
#endif
#ifdef SNAP_ONE
#define SPRK 1   /* sprites pushed after k1, land at k2 (frame snapshot) */
#else
#define SPRK 0
#endif
		// LOOP 8 — the palette rides in the TEXT packet AHEAD of the text
		// chunk: an aligned PAIR of 128-word regions takes words 82..337
		// and the full 256-word text chunk follows, so a TEXT push is 596
		// words when anything is dirty and 340 when nothing is.
		//
		// IT TOOK THREE SHAPES TO GET HERE, and the two rejects are the
		// reason this one is right:
		//  - ONE region, packet held at 340, taking HALF the text chunk.
		//    No length change at all, which looked safest given 7g split
		//    the packet precisely because dreq_incomplete said it was too
		//    big. It cost text refresh: 22.09 -> 23.57, spread exactly
		//    like LOOP 7a's text-latency signature (demo 48.7 -> 50.9,
		//    the INSERT COIN block).
		//  - ONE region ALONGSIDE a full text chunk, packet 468. Title
		//    went back to pixel-exact (2.43%) and the transport was fine
		//    (dreq_incomplete still 0), but scream went 37.6 -> 52.9 with
		//    the ALTERED BEAST logo rendering WHITE instead of red.
		//    tools/pal_probe.lua named the cause: regions 0 and 1 — the
		//    colour-cycling tile/text sets — were out of sync with the
		//    SH-2 shadow in 66% and 82% of samples, while every other
		//    region sat at 0%. One region per push is ~0.67 regions/vint
		//    against a cycle that rewrites those sets EVERY vint, so the
		//    hot regions could never converge. The old COMM stream kept
		//    up because it shipped the CHANGED WORDS (8 batches x 5);
		//    a region channel has to make up for that in bulk.
		// Shipping the aligned PAIR fixes it for one tag bit and no extra
		// state: regions 0 and 1 are pair 0, so both hot sets go on every
		// push. 82 + 256 + 256 + 2 = 596 words — the exact size of the
		// sprite push that has landed every cycle since 7g, so this asks
		// nothing new of the DMA.
		//
		// THE TAG LIVES IN THE PREFIX (word 81), not in the pad. A tag
		// after the payload is worthless under truncation — the master
		// would read a short transfer's missing tag as "no palette" and
		// copy palette words into text RAM. txt_dma_base is a multiple of
		// 256, so bits 0-10 hold it and the top bits are free:
		//   bit 15     = a palette pair is present
		//   bits 13-11 = which pair (regions 2p and 2p+1)
		//
		// EVERY DIRTY REGION SHIPS TWICE (pal_retry). The thunks have an
		// inherent race that cannot be closed at the ~17 LOOP-BASE sites:
		// the thunk marks its region and only THEN does the loop run its
		// stores, so a vint landing in between ships the region, clears
		// the bit, and the stores that follow are never marked again — a
		// permanently wrong colour, which is the one failure class this
		// port refuses. The window is a few instructions wide, so the
		// cheap fix is to ship each freshly-marked region a second time
		// one push later: the loop has certainly finished by then. Costs
		// two words of state and no MD-RAM reads at all, which is why the
		// scan does not need to survive as a backstop sweep.
		//
		// SELECTION IS ROUND-ROBIN, NOT LOWEST-BIT-FIRST. Lowest-first
		// STARVES: the attract colour-cycles run in regions 0-7 (the
		// tile/text half) and re-dirty them every frame, so regions 8-15
		// — the entire SPRITE palette — never came up. Measured with
		// tools/pal_rate.lua: regions 8-15 dirty 99.6% of 2000 frames and
		// never once shipped, which cost every scene on the scoreboard
		// (33.47% mean, title 2.4 -> 50.9). Rotating the start point
		// bounds each region's wait at 16 pushes.
#ifdef PAL32
		// LOOP 22 — dirty 32-word BLOCKS, up to 4 per push (measured
		// mean 1.63 dirty blocks/frame, max 7: worst case clears in
		// two pushes). Same round-robin + ship-twice retry discipline
		// as the pair channel below; the bitmap is BYTE-addressed
		// (bset in the thunks), so the scan is byte-wise too.
		static uint8_t pal_retry32[8];
		static uint8_t pal_next;              // rotor, block 0..63
#define MD_PAL_KMAX PKT_PAL_KMAX      /* LOOP25: KMAX 7 REVERTED —
		                              * wide packets broke the FIFO
		                              * (see the FPUSH verdict); the
		                              * storm ships via the FB flush */
		uint8_t pal_blk[4];
		uint16_t pal_k = 0;
		if (kk != SPRK) {
			volatile uint8_t *pd = (volatile uint8_t*)0xFFBA00;
#ifdef PAL_STORM
			/* LOOP 25 — PALETTE STORM PROBE (block 0xFFA060; grep'd
			 * free — A040 window sum is the nearest neighbour, u32
			 * ending A043; staging starts A400. NEVER SHIP: 64-bit
			 * popcounts per k2 vint). LOOP22 sized KMAX=4 from a
			 * steady-state census (max 7 dirty blocks/frame); this
			 * measures the STORMS — transform smoke stuck black,
			 * transition frames rendering torn-generation CRAM.
			 *   A060 u16 backlog before shipping (dirty|retry)
			 *   A062 u16 max backlog
			 *   A064 u32 sum newly-dirtied   A068 u32 sum shipped
			 *   A06C u16 storm vints (newly-dirtied >= 8)
			 *   A06E u16 vints leaving backlog > KMAX (torn-CRAM
			 *            proxy: something waits another cycle)
			 *   A070 u16 max tile-half backlog (blocks 0-31)
			 *   A072 u16 max sprite-half backlog (32-63)
			 *   A074 u16 probe vints sampled */
			{
				static const uint8_t nib[16] =
					{0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};
				static uint16_t ps_prev_after;
				uint16_t bt = 0, bs = 0;
				for (uint16_t i2 = 0; i2 < 8; i2++) {
					uint8_t v2 = (uint8_t)(pd[i2] | pal_retry32[i2]);
					uint8_t c2 = (uint8_t)(nib[v2 & 15] + nib[v2 >> 4]);
					if (i2 < 4) bt += c2; else bs += c2;
				}
				uint16_t bl = (uint16_t)(bt + bs);
				uint16_t nd = (uint16_t)(bl - ps_prev_after);
				if ((int16_t)nd < 0) nd = 0;
				*(volatile uint16_t*)0xFFA060 = bl;
				if (bl > *(volatile uint16_t*)0xFFA062)
					*(volatile uint16_t*)0xFFA062 = bl;
				*(volatile uint32_t*)0xFFA064 += nd;
				if (nd >= 8) (*(volatile uint16_t*)0xFFA06C)++;
				if (bt > *(volatile uint16_t*)0xFFA070)
					*(volatile uint16_t*)0xFFA070 = bt;
				if (bs > *(volatile uint16_t*)0xFFA072)
					*(volatile uint16_t*)0xFFA072 = bs;
				(*(volatile uint16_t*)0xFFA074)++;
				/* shipped this vint = the selection below, filled in
				 * after it runs; prev_after approximates the bits the
				 * push will clear */
				ps_prev_after = (uint16_t)(bl > MD_PAL_KMAX
				                           ? bl - MD_PAL_KMAX : 0);
			}
#endif
			uint8_t r = pal_next;
			for (uint16_t n = 0; n < 64 && pal_k < MD_PAL_KMAX; n++) {
				if ((pd[r >> 3] | pal_retry32[r >> 3]) & (1u << (r & 7)))
					pal_blk[pal_k++] = r;
				r = (uint8_t)((r + 1) & 63);
			}
			pal_next = r;
#ifdef PAL_STORM
			*(volatile uint32_t*)0xFFA068 += pal_k;
			if ((uint16_t)(*(volatile uint16_t*)0xFFA060) > pal_k
			    && *(volatile uint16_t*)0xFFA060 - pal_k > MD_PAL_KMAX)
				(*(volatile uint16_t*)0xFFA06E)++;
#endif
		}
		uint16_t pal_pr = 0;                  // pair channel disabled
		(void)pal_pr;
#else
		static uint16_t pal_retry;
		static uint8_t pal_next;
		uint16_t pal_pr = 0;                  // 1 + pair index, 0 = none
		if (kk != SPRK) {
			uint16_t sel = (uint16_t)(*(volatile uint16_t*)0xFFB9FC
						  | pal_retry);
			if (sel) {
				uint8_t r = pal_next;
				while (!(sel & (1u << r)))
					r = (uint8_t)((r + 1) & 15);
				pal_next = (uint8_t)((r + 2) & 15);
				pal_pr = (uint16_t)((r >> 1) + 1);
			}
		}
#endif
		// Length must be published BEFORE the session starts, so the
		// pair is chosen here rather than at the point it is pushed.
#ifdef SPR_TRUNC
		// LIVE-LIST TRUNCATION (LOOP 17). Push up to and INCLUDING the
		// list terminator and publish that length. The master arms the
		// MAX and reads landed = armed - TCR0, which ares reports
		// truthfully for a partial transfer (DRQPROBE, 233 of 234 short
		// pushes read their true length). The terminator rides inside
		// the shipped words, so compose stops exactly where it did.
		// 84 + 8*nrec is always a multiple of 4 — the FIFO drains in
		// 4-word bursts and a non-aligned count leaves the tail
		// un-drained, so TE would never set.
		uint16_t nrec = 64;
#ifdef FB_SPR_READ
		// LOOP 20 STEP 3 — THE PRIZE. The SH-2 reads the sprite list in
		// place at FB staging (the game's upload was remapped back by
		// patch_game; ares-verified intact), so the sprite payload of
		// this push is dead weight — and worse, 0xFF7000 is no longer
		// written, so the terminator scan above would walk stale RAM.
		// Push the 82-word prefix (regs/rowscroll/bitmap/text-base —
		// still load-bearing) plus ONE junk record and the magic tail:
		// 92 words, the exact SPRSHORT_OK minimum the SH-2 already
		// accepts, keeping the tail at landed-2 as the protocol
		// requires. The SH-2's FB_SPR_READ build never reads the
		// record. Sprite-window pushes drop 596 -> 92 words.
		if (kk == SPRK)
			nrec = 1;
#else
		if (kk == SPRK) {
#ifdef SPR_FULL
			/* MAME pixel-gate arm (LOOP 24): full-length pushes so
			 * MAME's DREQ (which reads partial landings as 0) sees a
			 * complete TE'd transfer. nrec stays 64. */
#else
			for (uint16_t i = 0; i < 64; i++) {
				if (s[i * 8 + 2] & 0x8000) {
					nrec = (uint16_t)(i + 1);
					break;
				}
			}
#endif
		}
#endif
#define SPRLEN (uint16_t)(84 + 8 * nrec)
#define SPRGRP (uint16_t)(nrec * 2)
#elif defined(DRQ_PROBE)
		/* LOOP 17 PROBE: every 16th sprite packet goes ONE RECORD SHORT
		 * (588 = 82 prefix + 63 records + 2 magic tail), published as
		 * 588. Record 63 is always past the list terminator (measured
		 * live max 21 of 64), so no sprite depends on it. The question
		 * this answers is whether the master can READ a partial landing
		 * out of TCR0 — see the Makefile. */
		static uint16_t probe_n;
		uint16_t probe_short = 0;
		if (kk == SPRK && (++probe_n & 15) == 0)
			probe_short = 1;
#define SPRLEN (probe_short ? 588 : 596)
#define SPRGRP (probe_short ? 126 : 128)
#else
#define SPRLEN 596
#define SPRGRP 128
#endif
#ifdef PKT_SLIM
		/* the k1 junk record went with the prefix: bitmap+tag+tail */
#undef SPRLEN
#undef SPRGRP
#define SPRLEN PKT_K1_LEN
#define SPRGRP 0
#endif
#ifdef WIN_TWO
#ifdef FB_TEXT_READ
		/* LOOP 20 HARVEST: the SH-2 reads text (and the layer regs it
		 * carries) in place from FB staging, so the text chunks are
		 * dead weight — drop both from the packet. TEXT = 82-word
		 * prefix + optional 256-word pal pair + 2 magic tail. The
		 * prefix words 0-79 are junk under FBTEXT (0xFF8000 is no
		 * longer written) but keep the layout: the bitmap at word 80
		 * and the pal tag at 81 are still load-bearing, and moving
		 * them re-plumbs every offset on the SH-2. 512 words/cycle
		 * saved is the harvest; the 160 junk words are a later trim. */
#ifdef PAL32
		/* lengths, offsets and the SH-2 whitelist all compile from
		 * packet_fmt.h — see the whitelist bug written up there */
#ifdef K2_FREE
		*(volatile uint16_t*)0xA15110 = (kk == SPRK) ? SPRLEN
					      : (uint16_t)(pal_k ? K2F_K2_LEN(pal_k) : K2F_K2_BARE);
#else
		*(volatile uint16_t*)0xA15110 = (kk == SPRK) ? SPRLEN
					      : (uint16_t)(pal_k ? PKT_K2_LEN(pal_k) : PKT_K2_BARE);
#endif
#else
		*(volatile uint16_t*)0xA15110 = (kk == SPRK) ? SPRLEN
					      : (pal_pr ? 340 : 84);
#endif
#else
		/* v8 rebalance: sprites standard 596 after k1; the k2-tail
		 * packet = regs + pal(optional) + BOTH text chunks. */
		*(volatile uint16_t*)0xA15110 = (kk == SPRK) ? SPRLEN
					      : (pal_pr ? 852 : 596);
#endif
#else
		*(volatile uint16_t*)0xA15110 = (kk == SPRK) ? SPRLEN
					      : (pal_pr ? 596 : 340);
#endif
		*ctrl = 4;                            // 68S: session start
#if defined(NT_WRAP) && !defined(K2_FREE)
		/* PER-WORD PUSH (LOOP15, wrap bundle): poll the full flag
		 * before EVERY word. The per-group form admits a 4-word burst
		 * into a 6/8-full FIFO and ares drops the burst's tail words
		 * uncounted (the magic-tail poisoned class: 4% pre-wrap ->
		 * 13-17% once the shorter consume moved the push into the
		 * DMAC's busy phase). Costs ~13 lines of 68K window — paid
		 * from the ~32 the wrap protocol freed. (The master-side idle
		 * pad was tried first and REVERTED: the master had no slack —
		 * it cost dropped frames, Mike's "jitter VERY high".) */
#ifdef K2_FREE
/* the write is GUARDED on spin: FPUSH used to write unconditionally
 * after exhaustion — dropped on ares (harmless), defer_access'd
 * FOREVER in MAME. Never hit before K2FREE because the old protocol's
 * DMA always kept pace; now the DMAC cycle-steals against the
 * master's ISR span, the FIFO backs up, and exhaustion is a real
 * path (the abort machinery handles it — the write must not fire). */
#define FPUSH(w) do { while (*ctrl < 0 && --spin) ;                       if (spin) fifo[0] = (w); } while (0)
#else
#define FPUSH(w) do { while (*ctrl < 0 && --spin) ; fifo[0] = (w); } while (0)
#endif
#else
/* LOOP25 FIFO VERDICT (two failed fixes, measured): under K2FREE the
 * push runs DURING the master's ISR/window span, where the DMAC
 * drains slowly. Per-group polling there races the full FIFO (drop:
 * misaligned 22-30 -> 101 when the pal packets widened, palettes
 * shifting, sprites skipping animation rounds); per-word polling
 * there burns the spin budget on the slow drain (handler 53 -> 80,
 * incomplete 30.5%). Z4's equilibrium — per-group + SMALL packets —
 * is the only combination field-proven playable, so packets must
 * STAY small and bulk palette moves through the FB (the LOOP25
 * storm flush), not the FIFO. */
#define FPUSH(w) do { if (spin) fifo[0] = (w); } while (0)
#endif
#ifndef PKT_SLIM
		// Two loops, not one with an index test: a per-group branch here
		// costs ~1 scanline of tail, and the master's window-pickup slack
		// is only 2-5 lines — enough to flip MAME's blit-skip regime.
		// (PKT_SLIM: words 0..79 are dead under FBSPR+FBTEXT — regs ride
		// the text capture — so the packet starts at the bitmap word.)
		// (K2_FREE: prefix rides k1 ONLY — the k2 packet is the slim
		// family, regs at 30Hz as they always were.)
#ifdef K2_FREE
		if (kk == SPRK)
#endif
		{
			const uint16_t *lr = (const uint16_t*)0xFF8000 + 0x740;
			for (uint16_t lg = 0; lg < 5; lg++) {
				while (*ctrl < 0 && --spin) ;
				if (!spin) { ok = 0; break; }
				FPUSH(lr[0]); FPUSH(lr[1]);
				FPUSH(lr[2]); FPUSH(lr[3]);
				lr += 4;
			}
		}
		if (ok
#ifdef K2_FREE
		    && kk == SPRK
#endif
		    ) {
			const uint16_t *rs = (const uint16_t*)0xFF8000 + 0x7C0;
			for (uint16_t rg = 0; rg < 15; rg++) {
				while (*ctrl < 0 && --spin) ;
				if (!spin) { ok = 0; break; }
				FPUSH(rs[0]); FPUSH(rs[1]);
				FPUSH(rs[2]); FPUSH(rs[3]);
				rs += 4;
			}
		}
#endif
		if (ok) {                             // 80 bitmap, 81 text base+tag
			while (*ctrl < 0 && --spin) ;
			if (spin) {
				volatile uint16_t *bm = (volatile uint16_t*)0xFFB9FE;
				FPUSH(*bm);
				*bm = 0;
#ifdef PAL32
				/* tag: bit 15 = blocks present, bits 13-11 = count
				 * (1..4); the ids ride words 82..85 AHEAD of the
				 * payload, same truncation rule as the pair tag */
				FPUSH(pal_k
					? (uint16_t)(txt_dma_base | (pal_k << PKT_PAL_KSHIFT)
						     | 0x8000)
					: txt_dma_base);
#else
				FPUSH(pal_pr
					? (uint16_t)(txt_dma_base | ((pal_pr - 1) << 11) | 0x8000)
					: txt_dma_base);
#endif
				// Body: the sprite list after w0 (so it LANDS for w1,
				// where the harvest is — the game's vint handler rebuilds
				// the list after our window in the IRQ chain), else the
				// optional palette region followed by the rotating text
				// chunk. One loop, one source pointer, one count: the
				// phases differ only in those.
#ifdef PAL32
				if (pal_k) {
					while (*ctrl < 0 && --spin) ;
					if (!spin) ok = 0;
					else {
						FPUSH(pal_k > 0 ? pal_blk[0] : 0xFFFF);
						FPUSH(pal_k > 1 ? pal_blk[1] : 0xFFFF);
						FPUSH(pal_k > 2 ? pal_blk[2] : 0xFFFF);
						FPUSH(pal_k > 3 ? pal_blk[3] : 0xFFFF);
					}
					for (uint16_t j = 0; ok && j < pal_k; j++) {
						const uint16_t *p = (const uint16_t*)0xFF9000
							+ ((uint16_t)pal_blk[j] << 5);
						for (uint16_t g = 0; g < 8; g++) {
							while (*ctrl < 0 && --spin) ;
							if (!spin) { ok = 0; break; }
							FPUSH(p[0]); FPUSH(p[1]);
							FPUSH(p[2]); FPUSH(p[3]);
							p += 4;
						}
					}
				}
#else
				if (pal_pr) {
					const uint16_t *p = (const uint16_t*)0xFF9000
						+ ((pal_pr - 1) << 8);
					for (uint16_t g = 0; g < 64; g++) {
						while (*ctrl < 0 && --spin) ;
						if (!spin) { ok = 0; break; }
						FPUSH(p[0]); FPUSH(p[1]);
						FPUSH(p[2]); FPUSH(p[3]);
						p += 4;
					}
				}
#endif
				const uint16_t *b = (kk == SPRK)
					? s : (const uint16_t*)0xFF8000 + txt_dma_base;
#ifdef FB_TEXT_READ
				/* text chunks dropped from the packet (read in
				 * place); only the sprite groups remain here. */
				uint16_t ng = (kk == SPRK) ? SPRGRP : 0;
#else
				uint16_t ng = (kk == SPRK) ? SPRGRP : 64;
#endif
				for (uint16_t g = 0; ok && g < ng; g++) {
					while (*ctrl < 0 && --spin) ;
					if (!spin) { ok = 0; break; }
					FPUSH(b[0]); FPUSH(b[1]);
					FPUSH(b[2]); FPUSH(b[3]);
					b += 4;
				}
#ifdef WIN_TWO
				/* v8 rebalance: the k2-tail packet carries the
				 * SECOND text chunk (rotation is sequential; wrap
				 * at 2048 handled by the second pointer). */
#ifndef FB_TEXT_READ
				if (ok && kk != SPRK) {
					uint16_t nb2 = (uint16_t)(txt_dma_base + 256);
					if (nb2 >= 2048) nb2 = 0;
					const uint16_t *t2 =
						(const uint16_t*)0xFF8000 + nb2;
					for (uint16_t g = 0; g < 64; g++) {
						while (*ctrl < 0 && --spin) ;
						if (!spin) { ok = 0; break; }
						FPUSH(t2[0]); FPUSH(t2[1]);
						FPUSH(t2[2]); FPUSH(t2[3]);
						t2 += 4;
					}
				}
#endif /* !FB_TEXT_READ */
#endif
				// MAGIC TAIL (LOOP 13). The pads are pushed anyway; give
				// them known values so the master can verify the packet
				// landed word-ALIGNED. ares discards a FIFO write that
				// races a full FIFO WITHOUT counting it (io-external.cpp
				// dreq fifo: write only if !full, length decrements only
				// on accept; MAME defer_access()es instead, so it can
				// never lose one) — the per-4-word-group full check can
				// admit a group into a 6/8 FIFO under slow drain and drop
				// its tail words. The overpush dummies then BACKFILL the
				// count (lost words never decremented the armed length),
				// so TCR completes and the whole packet lands displaced
				// -N words: savestate-proven as the TEXT-RAM reg block at
				// -2 (0x73E/0x746/0x74A) and the HUD-glyph margin noise.
				// A displaced landing puts these two words off-position;
				// the master then skips the packet whole (stale beats
				// displaced) and counts it in DRQR[7].
				if (ok) { FPUSH(0xA55A); FPUSH(0x5AA5); }  // pad to 596 / 340
			} else
				ok = 0;
		}
		if (ok) {
			// Rotate the FULL 0..2047 range: the old 0..1791 restriction
			// existed only because COMM shipped the 1792..2047 regs every
			// vint and a DMAed stale snapshot fought it (dx=+24). Both come
			// from the same snapshot instant now, and the explicit reg
			// blocks are applied after the chunk, so the overlap is a
			// no-op. 256-word chunks: 512 refreshes text twice as fast and
			// rendered the TITLE near-perfectly (parity 48.3 -> 2.7%
			// mismatch), but starved the scream scene into group-1
			// red/white/blue fallback. Revisit after LOOP 7 step 2.
			// Only a TEXT packet consumed a chunk — advancing on the
			// sprite phase too would skip a third of the text RAM
			// forever (rotation and push phase are coprime by accident,
			// not by design: 3 phases, 8 chunks). Advance by what was
			// actually SENT — a full 256-word chunk either way now.
#ifdef WIN_TWO
			/* v8 rebalance: only the k2-tail packet carries text —
			 * and it carries TWO chunks, so advance by 512. */
			if (kk != SPRK) {
				txt_dma_base = (uint16_t)(txt_dma_base + 512);
				if (txt_dma_base >= 2048)
					txt_dma_base = (uint16_t)(txt_dma_base - 2048);
#else
			if (kk != SPRK) {
				txt_dma_base = (uint16_t)(txt_dma_base + 256);
				if (txt_dma_base >= 2048) txt_dma_base = 0;
#endif
				// Consume the region ONLY on a completed push. An aborted
				// push leaves both the mark and the retry alone, so the
				// region simply goes next time — the same stale-beats-
				// lost rule the sprite list follows. A region that was
				// FRESHLY marked earns one repeat (the race above); one
				// that arrived here only as a repeat is now done.
#ifdef PAL32
				/* consume ONLY on a completed push, freshly-marked
				 * blocks earn one repeat — identical discipline to
				 * the pair channel this replaces */
				for (uint16_t j = 0; j < pal_k; j++) {
					volatile uint8_t *pd =
						(volatile uint8_t*)0xFFBA00 + (pal_blk[j] >> 3);
					uint8_t bit = (uint8_t)(1u << (pal_blk[j] & 7));
					uint8_t fresh = (uint8_t)(*pd & bit);
					*pd &= (uint8_t)~bit;
					pal_retry32[pal_blk[j] >> 3] = (uint8_t)
						((pal_retry32[pal_blk[j] >> 3] & ~bit) | fresh);
				}
#else
				if (pal_pr) {
					volatile uint16_t *pdw = (volatile uint16_t*)0xFFB9FC;
					uint16_t bit = (uint16_t)(3u << ((pal_pr - 1) << 1));
					uint16_t fresh = (uint16_t)(*pdw & bit);
					*pdw &= (uint16_t)~bit;
					pal_retry = (uint16_t)((pal_retry & ~bit) | fresh);
				}
#endif
			}
		} else {
			*ctrl = 0;
			(*(volatile uint16_t*)0xFFB0E0)++;   // DREQ push aborts
		}
		// SPIN HEADROOM WATERMARK (LOOP 13 part 4): min polls LEFT of the
		// 2600 budget across completed pushes. Discriminates the 11.5%
		// dreq_incomplete in one round-trip: watermark near 0 = the budget
		// is marginal and the aborts are real (raise budget / shrink cost);
		// watermark comfortable + aborts 0 = the 68K pushed everything and
		// the DMA drain itself is the problem.
		if (ok) {
			volatile uint16_t *wm = (volatile uint16_t*)0xFFA022;
			if (spin < *wm)
				*wm = spin;
			// TAIL OVERPUSH (LOOP 13): ares measured residues of 1-4
			// words STUCK across whole frames on 8.5% of cycles — not
			// DMAC latency but the DREQ assert threshold: with fewer
			// than a burst's worth left in the FIFO, DREQ never rises
			// and the tail is never drained. landed 592-593 then misses
			// the sprite-snapshot gate (>=594) and the compose keeps a
			// STALE LIST — the purple bottom band is last frame's
			// (purple) zombie sprites at stale positions over a correct
			// grey MD walkway. Push 4 dummy words BEYOND the armed 596:
			// TCR reaches 0 on the real payload (TE sets, landed=596);
			// the extras sit in the FIFO and die at the next session's
			// 68S 0->4 reset, which clears the FIFO pointers. Verify on
			// the next ares pass: DRQR[1] must collapse.
			{
				uint16_t g2 = 200;
				for (uint16_t xw = 0; xw < 4; xw++) {
					while (*ctrl < 0 && --g2) ;
					if (!g2) break;
					fifo[0] = 0;
				}
				(*(volatile uint16_t*)0xFFA036) = g2;  // overpush polls left
			}
		}
	}

#ifdef TAIL_PROBE
	// LOOP 6 TAIL SPLIT (`make TAILPROBE=1`, NEVER shipped — see below).
	// The tail is the dominant term on BOTH machines and, unlike the
	// window, it is MAME-VISIBLE, so it can be iterated against a real
	// falsifier instead of ares round-trips. Max spans, scanlines:
	//   0xFFB0E8 = DREQ push   0xFFB0EA = palette scan   0xFFB0EC = stream
	// Measured a16d97d: total 224 / window 18 / tail 206 of 262;
	// dreq 52, palscan 85, stream 117; mean total 170, mean stream 70.
	//
	// THESE PROBES ARE NOT FREE. They add per-vint work to the very path
	// that is overloaded, which shifts V-gate outcomes and therefore
	// which frames ship: measured cost is demo 52.1 -> 54.6 and demo2
	// 20.9 -> 23.4 on the scoreboard. Diagnose with them, ship without.
	{
		uint8_t dv = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);
		uint8_t sp2 = (uint8_t)(dv - v_win);
		dreq_span = sp2;
		if (sp2 > *(volatile uint8_t*)0xFFB0E8)
			*(volatile uint8_t*)0xFFB0E8 = sp2;
	}
	uint8_t v_scan = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);
#endif

	// THE PALETTE SCAN USED TO BE HERE — 512 words of the mirror diffed
	// against a 4KB sent-copy at 0xFFA000, EVERY vint, 1024 MD-RAM reads
	// to discover in steady state that nothing had changed. 45 of the 92
	// tail scanlines. The write-thunks mark dirty regions directly now and
	// the DREQ push ships them, so there is nothing left here at all — and
	// 0xFFA000 is free MD RAM (see pal_retry for why no backstop sweep is
	// needed to keep it honest).
#ifdef TAIL_PROBE
	{
		uint8_t sv = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);
		uint8_t sp2 = (uint8_t)(sv - v_scan);
		palscan_span = sp2;
		if (sp2 > *(volatile uint8_t*)0xFFB0EA)
			*(volatile uint8_t*)0xFFB0EA = sp2;
	}
#endif

	// LOOP 8 — COMM HAS NO TENANTS LEFT, so the stream section is gone.
	// LOOP 7a moved text, layer regs and rowscroll onto the DREQ packet
	// and left the palette here as COMM's last payload; the write-thunks
	// have now moved that too. THE RATIO THAT DROVE BOTH MOVES: COMM cost
	// 70 lines for ~55 words (1.27 lines/word) against DREQ's 49 lines for
	// 772 (0.063) — 20x, because COMM's cost is an ACK ROUND-TRIP per
	// 5-word batch, not payload. Worse, that wait was ELASTIC: the 68K
	// blocked until the SLAVE serviced COMM0, so every cycle freed
	// elsewhere was reabsorbed here (which is why six iterations of
	// shaving moved nothing, and why PROBE_spin0 moved the band
	// 57.1 -> 39.4% on its own). COMM0 now carries only the window
	// command/ack, COMM12 the V heartbeat and COMM14 the sound log.
	// (STREAM_SPIN and `make SPINPROBE=N` retired with it.)
	uint8_t v_stream = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);

#ifdef TAIL_BURN
	// LOOP 7 DIAGNOSTIC ONLY (never shipped): burn back the ~55 scanlines
	// the COMM->DREQ move freed, so the handler costs what it used to.
	// Isolates "the channel changed" from "the 68K now runs more".
	{
		volatile uint16_t *hv = (volatile uint16_t*)0xC00008;
		uint8_t v0 = (uint8_t)(*hv >> 8);
		while ((uint8_t)((uint8_t)(*hv >> 8) - v0) < 55) ;
	}
#endif

	// ITER5 TAIL PROBE: shim-handler span in scanlines, entry V (0xFFB0FE,
	// written at the window phase) -> here, the END of the per-vint tail
	// (DREQ push + palette scan + text/palette stream). This tail runs on
	// EVERY vint, gate-rejected or not; if it alone overruns the frame it
	// is the steady-state 68K load that pins the V-gate reject band (the
	// window-shortening fixes never touched it). Running MAX in 0xFFB0F4:
	// a span approaching one frame (~262 lines) = the handler exceeds a
	// frame -> next H-int fires late -> reject. Small (<~120) = the tail
	// fits and the reject cause is elsewhere.
#ifdef TAIL_PROBE
	{
		uint8_t sv = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);
		uint8_t sp2 = (uint8_t)(sv - v_stream);
		if (sp2 > *(volatile uint8_t*)0xFFB0EC)
			*(volatile uint8_t*)0xFFB0EC = sp2;
	}
#else
	(void)v_stream;
#endif
	{
		static uint8_t max_total, at_win;
		uint8_t ev = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);
		uint8_t total = (uint8_t)(ev - v_entry);   // TRUE entry -> here
		uint8_t win = (uint8_t)(v_win - v_entry);  // window/ack-spin only
		if (total > max_total) { max_total = total; at_win = win; }
		// LOOP15: the handler-total MEAN promoted to ALWAYS-ON — it is
		// the number the DREQ-cadence redesign steers by (mean 68K time
		// taken from the game), `total` is already computed above, and
		// the add is ~10 cycles. state_health prints it as "68K handler
		// mean". Sum 0xFFB0D0 over 0xFFB0F0 vints (boot-zeroed).
		// 0xFFA040 = sum of the WINDOW/ack-wait spans: splits the mean
		// into "waiting on the master's FM work" vs "the 68K's own
		// tail" — the two halves point at DIFFERENT surgeries.
		*(volatile uint32_t*)0xFFB0D0 += total;
		*(volatile uint32_t*)0xFFA040 += win;
#ifdef TAIL_PROBE
		// LOOP 6 TAIL METRICS. The max span is dominated by rare worst
		// frames and barely moves under changes that cut real work by 6x
		// (gating COMM sends 64 -> 11 left the max stream span at ~120),
		// so accumulate MEANS too — sustained load is what starves the
		// game. 0xFFB0D4 = sum of stream spans, over 0xFFB0F0 vints.
		*(volatile uint32_t*)0xFFB0D4 += (uint8_t)(ev - v_stream);
		*(volatile uint32_t*)0xFFB0D8 += dreq_span;
		*(volatile uint32_t*)0xFFB0DC += palscan_span;
#endif
		// F4 = (MAX TOTAL handler span << 8) | the WINDOW span of THAT max
		// vint, scanlines. Max catches the worst handler (the accepted,
		// window-posting ones are longest); its paired window shows whether
		// the ack-spin or the tail dominates the worst case. A steady
		// eye/demo HOLD has no transitions, so its max is the steady
		// worst-case. total>=262 => handlers lap -> entry drifts -> band.
		*(volatile uint16_t*)0xFFB0F4 =
			(uint16_t)(((uint16_t)max_total << 8) | at_win);
	}

	if (busy)
		return;                          // MCU skips input/bank work while busy

	// MCU vblank half: inputs + tile bank mirror
	uint16_t p1 = read_joypad(0);
	uint16_t p2 = read_joypad(1);
	IO_P1 = md_to_arcade(p1);
	IO_P2 = md_to_arcade(p2);

	// SERVICE (active low): b0 coin1, b2 test, b3 service, b4 start1, b5 start2
	// MD X (or START+A held) = coin, START = start1
	uint8_t svc = 0;
	uint16_t start = p1 & 0x0080, aheld = p1 & 0x0040, xbtn = p1 & 0x0200;
	if (xbtn || (start && aheld)) svc |= 0x01;
	if (start && !aheld)          svc |= 0x10;
	if (p2 & 0x0080)              svc |= 0x20;
	IO_SERVICE = (uint8_t)~svc;
	MCU_COINS = svc;                     // MCU posts XOR-inverted (active high)

	// DISPLAY ENABLE (2026-09-05, Mike's littered boot/transitions): the
	// arcade hides every tilemap load behind its video-enable bit — port
	// 0xC40001 bit 5 (jts16_main.v:247 video_en = ppib_dout[4]... MAME
	// segas16b misc_io_w: set_display_enable(data & 0x20)); both cuts in
	// ref_arcade are 4 black frames then the whole scene. The game writes
	// 0xFFF018 to that port (0x80 = off, 0xA0 = on) and our mailbox kept
	// it. Mirror it into MD VDP reg 1 (bit 6) and hand it to the SH-2 in
	// bit 15 of the packet's dirty-bitmap word (13 bits used).
	{
		static uint8_t disp_last = 0xFF;
		if (md_hold_seen) md_hold = (uint8_t)(md_hold_seen == 2);
		md_hold_seen = 0;
		uint8_t disp = (uint8_t)((IO_MISC & 0x20) && !md_hold);
#ifdef BOOT_FBXFER
		/* THE PROBE MUST NOT BE READ THROUGH THE DISPLAY GATE. A blanked
		 * MD plane is black, and so is a flooded d=0 — the first three
		 * hardware runs of this probe came back black and were read as
		 * "the flood never ran", which does not follow. Force the plane
		 * on for probe builds so black means exactly one thing. */
		disp = 1;
#endif
		if (disp != disp_last) {
			*(volatile uint16_t*)VDP_CTRL_PORT = disp ? 0x8154 : 0x8114;
			disp_last = disp;
		}
	}

	BANK_SHADOW = MCU_BANKREQ;           // tile bank req -> shadow (SH-2 later)
	*mars_comm12 += 1;                   // frame heartbeat

#ifdef BOOT_SPIN
	/* Bucket the spin residual published by r60_push.
	 *   GREEN  == 2600  never FIFO-full (ares' result)
	 *   YELLOW  > 2000  mild
	 *   ORANGE  > 1000
	 *   RED    <= 1000  the push is mostly waiting on the master, and
	 *                   at 0 the packet is ABORTED (ok=0) */
	{
		uint16_t sp = *(volatile uint16_t*)0xFFA17A;
		uint16_t col = (sp >= 2600) ? 0x00E0
					 : (sp >  2000) ? 0x00EE
					 : (sp >  1000) ? 0x006E
					                : 0x000E;
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_PUSHWHERE
	/* WHICH PART OF THE PUSH? (2026-09-08, LOOP27 56)
	 * The push is 96-160 lines on hardware (55). r60_push already
	 * carries a PUSH AUTOPSY with five stamps, and its own comment
	 * records the key prior result: cutting the packet 145->52 words did
	 * NOT move the span, and the spin residual stayed 2600 = **never
	 * FIFO-full**. So the push is not transport-bound, and my
	 * "the FIFO fills and the 68K stalls" candidate is already dead.
	 *   0xFFA0B4 entry -> 0xFFA0BE rotor+compares -> 0xFFA0B6 selection
	 *   -> 0xFFA0B8 regs (20 words) -> 0xFFA0BA rowscroll+pal
	 *   -> 0xFFA0BC records+tail
	 *   WHITE   total < 16 lines
	 *   RED     entry -> rotor+compares
	 *   GREEN   rotor -> selection done   <- the palette compare pre-pass
	 *   BLUE    selection -> regs shipped
	 *   YELLOW  regs -> rowscroll+pal shipped
	 *   MAGENTA pal -> records+tail shipped
	 * GREEN or RED would confirm the frame-threshold-law note that the
	 * next lever is "the palette compare off the 68K". */
	{
		uint8_t a0 = (uint8_t)(*(volatile uint16_t*)0xFFA0B4 >> 8);
		uint8_t a1 = (uint8_t)(*(volatile uint16_t*)0xFFA0BE >> 8);
		uint8_t a2 = (uint8_t)(*(volatile uint16_t*)0xFFA0B6 >> 8);
		uint8_t a3 = (uint8_t)(*(volatile uint16_t*)0xFFA0B8 >> 8);
		uint8_t a4 = (uint8_t)(*(volatile uint16_t*)0xFFA0BA >> 8);
		uint8_t a5 = (uint8_t)(*(volatile uint16_t*)0xFFA0BC >> 8);
		uint8_t d1=(uint8_t)(a1-a0), d2=(uint8_t)(a2-a1), d3=(uint8_t)(a3-a2);
		uint8_t d4=(uint8_t)(a4-a3), d5=(uint8_t)(a5-a4);
		uint16_t col;
		if ((uint8_t)(a5-a0) < 16) {
			col = 0x0EEE;
		} else {
			uint8_t m=d1; col=0x000E;
			if (d2>m){m=d2;col=0x00E0;}
			if (d3>m){m=d3;col=0x0E00;}
			if (d4>m){m=d4;col=0x00EE;}
			if (d5>m){m=d5;col=0x0E0E;}
		}
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_PUSHLEN
	/* HOW LONG IS THE DREQ PUSH ON HARDWARE? (2026-09-08, LOOP27 55)
	 * The tail probe came back BLUE on every hardware capture: of the
	 * four tail stages, the 68K's DREQ push into the 32X FIFO is the
	 * biggest. ares measures it at 44-47 scanlines (read from WRAM) out
	 * of a ~62-line handler, and ares runs the game at ~50%. Hardware
	 * runs it at ~3%, so the push must be far longer there. This gives
	 * the number instead of the ranking.
	 *   GREEN  <48    ares-like, the push is not the problem
	 *   YELLOW <96
	 *   ORANGE <160
	 *   RED    >=160  the push alone is most of a 262-line frame */
	{
		uint8_t v2 = (uint8_t)(*(volatile uint16_t*)0xFFA0AA >> 8);  /* pre-push  */
		uint8_t v3 = (uint8_t)(*(volatile uint16_t*)0xFFA0AC >> 8);  /* post-push */
		uint8_t d  = (uint8_t)(v3 - v2);
		uint16_t col = (d <  48) ? 0x00E0
					 : (d <  96) ? 0x00EE
					 : (d < 160) ? 0x006E
					             : 0x000E;
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_TAIL
	/* WHERE DOES THE HANDLER TAIL GO? (2026-09-08, LOOP27 54)
	 * Hardware says the consume is 8-24 lines and starts within 16 of
	 * handler entry (entry 53). So the ~200 remaining lines are in the
	 * TAIL, and the tail is where the 68K WAITS ON THE MASTER.
	 * The shipping code already stamps V at four tail points:
	 *   0xFFA0A0 at post -> 0xFFA0AA pre-push -> 0xFFA0AC post-push
	 *   -> 0xFFA09E at hold exit
	 * Bucket the three gaps, flood with the colour of the biggest.
	 *   WHITE  total < 16 lines: the tail is not it either
	 *   RED    consume-end -> post   (waiting to be allowed to post)
	 *   GREEN  post -> pre-push      (the master's window)
	 *   BLUE   pre-push -> post-push (the DREQ push itself)
	 *   YELLOW post-push -> hold exit (the flip-hold echo)
	 * RED or GREEN puts it on the master; BLUE on the FIFO transport;
	 * YELLOW on the flip. */
	{
		uint8_t v0 = (uint8_t)(*(volatile uint16_t*)0xFFA176 >> 8);  /* consume end */
		uint8_t v1 = (uint8_t)(*(volatile uint16_t*)0xFFA0A0 >> 8);  /* post */
		uint8_t v2 = (uint8_t)(*(volatile uint16_t*)0xFFA0AA >> 8);  /* pre-push */
		uint8_t v3 = (uint8_t)(*(volatile uint16_t*)0xFFA0AC >> 8);  /* post-push */
		uint8_t v4 = (uint8_t)(*(volatile uint16_t*)0xFFA09E >> 8);  /* hold exit */
		uint8_t d1 = (uint8_t)(v1 - v0), d2 = (uint8_t)(v2 - v1);
		uint8_t d3 = (uint8_t)(v3 - v2), d4 = (uint8_t)(v4 - v3);
		uint8_t tot = (uint8_t)(v4 - v0);
		uint16_t col;
		if (tot < 16) {
			col = 0x0EEE;                          /* WHITE */
		} else {
			uint8_t m = d1; col = 0x000E;          /* RED    */
			if (d2 > m) { m = d2; col = 0x00E0; }  /* GREEN  */
			if (d3 > m) { m = d3; col = 0x0E00; }  /* BLUE   */
			if (d4 > m) { m = d4; col = 0x00EE; }  /* YELLOW */
		}
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_PRECONSUME
	/* Gap from handler entry (0xFFA178) to consume entry (0xFFB0B0), in
	 * MD scanlines. Vblank is 38 lines, a frame 262.
	 *   GREEN  <16   the consume starts promptly; the delay is elsewhere
	 *   YELLOW <48    up to a fifth of the frame gone before it starts
	 *   ORANGE <112
	 *   RED    >=112  most of the frame is spent BEFORE the consume, and
	 *                 that is where the 95% miss rate lives */
	{
		uint8_t ve = (uint8_t)(*(volatile uint16_t*)0xFFA178 >> 8);
		uint8_t vc = (uint8_t)(*(volatile uint16_t*)0xFFB0B0 >> 8);
		uint8_t gap = (uint8_t)(vc - ve);
		uint16_t col = (gap <  16) ? 0x00E0
					 : (gap <  48) ? 0x00EE
					 : (gap < 112) ? 0x006E
					               : 0x000E;
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_STAGEMAX
	/* WHICH STAGE OF THE CONSUME IS THE SLOW ONE? (2026-09-08, LOOP27 51)
	 * s16_span2 answers "is the consume the frame". This answers "which
	 * part of it", so the eventual hardware session is not one bit.
	 * The shipping consume already stamps the MD V-counter at four
	 * points (md_main.c 545/627/652/747 and 1074):
	 *   0xFFB0B0 entry -> 0xFFB0B2 after scroll -> 0xFFB0B4 after the
	 *   tile/span DMAs -> 0xFFB0B6 after cells -> 0xFFA176 end
	 * Bucket the four deltas, find the largest, and flood the palette
	 * with a colour naming that stage. One look, one culprit.
	 *   WHITE  = nothing is slow (total < 8 lines) — the consume is
	 *            innocent and the time goes elsewhere in the handler
	 *   RED    = entry -> scroll
	 *   GREEN  = scroll -> tile/span DMAs   <- the FB-sourced DMAs
	 *   BLUE   = spans -> cells
	 *   YELLOW = cells -> end               <- includes the palette
	 * GREEN would confirm the FB-sourced DMA suspicion of entry 47/48
	 * directly; YELLOW would put it back on the palette upload. */
	{
		uint8_t v0 = (uint8_t)(*(volatile uint16_t*)0xFFB0B0 >> 8);
		uint8_t v1 = (uint8_t)(*(volatile uint16_t*)0xFFB0B2 >> 8);
		uint8_t v2 = (uint8_t)(*(volatile uint16_t*)0xFFB0B4 >> 8);
		uint8_t v3 = (uint8_t)(*(volatile uint16_t*)0xFFB0B6 >> 8);
		uint8_t v4 = (uint8_t)(*(volatile uint16_t*)0xFFA176 >> 8);
		uint8_t d1 = (uint8_t)(v1 - v0), d2 = (uint8_t)(v2 - v1);
		uint8_t d3 = (uint8_t)(v3 - v2), d4 = (uint8_t)(v4 - v3);
		uint8_t tot = (uint8_t)(v4 - v0);
		uint16_t col;
		if (tot < 8) {
			col = 0x0EEE;                        /* WHITE  */
		} else {
			uint8_t m = d1; col = 0x000E;        /* RED    */
			if (d2 > m) { m = d2; col = 0x00E0; }/* GREEN  */
			if (d3 > m) { m = d3; col = 0x0E00; }/* BLUE   */
			if (d4 > m) { m = d4; col = 0x00EE; }/* YELLOW */
		}
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
#ifdef BOOT_SPAN
	/* WHERE IS THE 68K WHEN ITS CONSUME FINISHES? (2026-09-08, LOOP27 41)
	 * Hardware runs ~3 game-frames/s against ~60 vints/s (entry 40): the
	 * handler is overrunning massively. MDCONSUMEOFF was meant to bisect
	 * it but it changes the GAME'S BEHAVIOUR, not just its cost, so its
	 * reading was unusable.
	 * This does not perturb anything: it reads a stamp the shim ALREADY
	 * records — the MD V-counter at consume end, 0xFFA176 — and floods
	 * the palette with which part of the frame that lands in. Vblank
	 * starts at V=0xE0.
	 *   GREEN   V >= 0xE0   consume finished inside vblank (healthy)
	 *   YELLOW  V <  0x20   ran ~32 lines into the visible frame
	 *   ORANGE  V <  0x60   ~96 lines in
	 *   RED     otherwise   deep into the picture; the vint is lost
	 * ares should read GREEN or YELLOW. If hardware reads RED, the
	 * consume alone is eating the frame and the target is named.
	 * (First cut of this probe accumulated a wrap counter and latched
	 * WHITE forever — ares read WHITE at every sample, which is how it
	 * was caught before it went to hardware. Bucket a single stamp.) */
	{
		/* v2 (LOOP27 44): the first cut bucketed the consume's END
		 * POSITION, which conflates the consume's own cost with
		 * everything that ran before it in the vint. Both stamps exist —
		 * 0xFFB0B0 is V at consume ENTRY, 0xFFA176 is V at consume END —
		 * so bucket the DIFFERENCE and measure the consume itself.
		 * Caveat kept in view: both are written only when a packet was
		 * actually present (`live[0] == 0xB6B6`, md_main.c:547), so this
		 * measures CONSUMING vints, not all vints. That is the right
		 * population for this question. */
		uint8_t v0 = (uint8_t)(*(volatile uint16_t*)0xFFB0B0 >> 8);
		uint8_t v1 = (uint8_t)(*(volatile uint16_t*)0xFFA176 >> 8);
		uint8_t dl = (uint8_t)(v1 - v0);            /* lines, wraps ok */
		uint16_t col = (dl <   8) ? 0x00E0          /* GREEN  <8 lines  */
					 : (dl <  24) ? 0x00EE          /* YELLOW <24       */
					 : (dl <  64) ? 0x006E          /* ORANGE <64       */
					              : 0x000E;         /* RED    >=64      */
		*(volatile uint32_t*)0xC00004 = 0xC0000000u;
		for (int q = 0; q < 64; q++)
			*(volatile uint16_t*)0xC00000 = col;
	}
#endif
}

__attribute__((section(".data")))
void main(void) {
	// BOOT-PHASE TRACER: Genesis backdrop colour is visible in ares no matter
	// what the 32X side does (stage-A red flash proved it). BGR:
	// RED=entered main; YELLOW=master SDRAM signal; CYAN=slave alive;
	// GREEN=RV set + stash copied; game then owns the screen.
	vdp_color(0, 0x00E);                // RED: main entered

	// Wait for BOTH SH-2s to reach their SDRAM-resident code before setting
	// RV=1 — with RV set the SH-2s must never touch cart ROM (hardware rule,
	// enforced by ares; violating it kills their instruction fetch). The
	// master posts 0x600D on COMM14 from SDRAM; the slave's SDRAM loop
	// increments COMM6.
#ifdef BOOT_SHSTAGE
	while (*mars_comm14 != 0x600D)       /* probe: backdrop = blue + master's stage tint */
		vdp_color(0, (uint16_t)(0x800 | ((*mars_comm12 & 7) << 5)));
#else
	while (*mars_comm14 != 0x600D) ;
#ifdef BOOT_SLVALIVE
	/* MiSTer slave-boot beacon: GREEN if the slave heartbeat (COMM6)
	 * advances within ~4M polls, RED and halt if not. The permanent
	 * warm-up should make this green. */
	{
		uint16_t c6 = *mars_comm6; uint32_t g = 4000000UL;
		while (*mars_comm6 == c6 && --g) ;
		vdp_color(0, g ? 0x0E0 : 0x00E);
		if (!g) for (;;) ;
	}
#endif
#endif
	vdp_color(0, 0x0EE);                // YELLOW: master is SDRAM-resident
#ifdef BOOT_SHSTAGE
	/* HARDWARE PROBE: the 32X display is on now (the master's init), so
	 * the MD backdrop is hidden; paint 32X CRAM 0 from here (FM must be
	 * 0 for the 68K to reach it — the master is done with the VDP). */
#define MDSTAGE(col) do { *(volatile uint16_t*)0xA15200 = (col); } while (0)
	*(volatile uint16_t*)0xA15100 &= 0x7FFF;
	MDSTAGE(0x03FF);                    /* YELLOW: 68K saw 0x600D */
	{
		uint16_t c6 = *mars_comm6;
		uint32_t g = 4000000UL;
		static const uint16_t sst_col[8] = { 0x03FF, 0x001F, 0x03E0, 0x7C00, 0x7FE0, 0x7C1F, 0x7FFF, 0x7C1F };  /* 6 = WHITE: SDRAM stub ran */
		while (*mars_comm6 == c6 && --g)
			MDSTAGE(sst_col[*(volatile uint16_t*)0xA1512A & 7]);   /* slave stage on COMM10 */
		if (!g) { for (;;) ; }              /* colour stays: the slave's last stage */
	}
	MDSTAGE(0x7FE0);                    /* CYAN: slave alive */
#else
#define MDSTAGE(col) do { } while (0)
	{
		uint16_t c6 = *mars_comm6;
		while (*mars_comm6 == c6) ;
	}
#endif
	vdp_color(0, 0xEE0);                // CYAN: slave alive too

	// FM=0: 68K owns the FB window during gameplay so the game's remapped
	// tile-RAM writes (FB staging 0x852000) land. The SH-2 is done with its
	// VDP init (it posted 0x600D from SDRAM); from here it only touches
	// FB/CRAM inside the render window, where the shim raises FM first.
	*(volatile uint16_t*)0xA15100 &= 0x7FFF;
	MDSTAGE(0x0200);                    /* DARK GREEN: heartbeat seen, FM down */

	// UNPAIR MODEL (NOTES.md "REBASE DESIGN v2"): RV stays 0 FOREVER.
	// The game executes its REBASED copy through the banked 0x900000
	// cart window (bank 3 -> cart 0x300000, delta +0x900000): the SH-2s
	// may touch cart ROM at ANY time, concurrent with the 68K's own
	// (bus-arbitrated) instruction fetches — the standard commercial-32X
	// memory model. No RAM stash: the game's boot bytes at arcade
	// 0x400-0x807 (displaced in the LOW cart copy by the Sega security
	// blob) exist intact in the high copy and run in place at 0x900400.
	// P3: zero the sprite attribute table (VRAM 0xF000, 80 entries),
	// HERE at boot — outside every vint deadline. The first cut ran
	// this inside md_bg_palette (the first-paint VINT) and the extra
	// ~320 data-port writes skewed the boot choreography: in-ISR
	// flips collapsed to 52.7% and the FB ran phase-degraded for the
	// whole session (Mike's "only new assets update" corpus,
	// 2026-08-28). One-shot VDP maintenance NEVER goes in the vint.
	// (Old base 0xFE00 was never written by anything; real hardware
	// powers up with VRAM garbage there. Entry 0 all-zero = link 0 =
	// scan stops immediately.)
	*vdp_ctrl_wide = ((uint32_t)(0x4000u | 0x3000u) << 16) | 3u;
	for (uint16_t i = 0; i < 320; i++)
		*vdp_data_port = 0;
	MDSTAGE(0x7C0F);                    /* PURPLE: VDP clear done, before the bank-2 blob copy */
#ifdef MDSPR
	// P3 M1: mob sprite art -> VRAM 0x8000, once, before the game owns
	// the 0x900000 window. The blob sits at a FIXED cart offset
	// (mars.ld .mdsprart, 0x2F0000 = bank 2 + 0xF0000); ~5.4K words
	// through the data port costs ~14ms of boot, nothing of gameplay.
	{
		const volatile uint16_t *src = (const volatile uint16_t*)
			(0x900000ul + MDSPR_CART_WINOFF);
		*(volatile uint16_t*)0xA15104 = MDSPR_CART_BANK;
		*vdp_ctrl_wide = ((uint32_t)(0x4000u | (MDSPR_VRAM_BASE & 0x3FFFu))
		                << 16) | 2u;   /* VRAM 0x8000 write */
		for (uint16_t i = 0; i < MDSPR_BLOB_WORDS; i++)
			*vdp_data_port = src[i];
	}
#endif
	MDSTAGE(0x01FF);                    /* ORANGE: before the bank-3 switch */
	*(volatile uint16_t*)0xA15104 = 3;  // 0x900000 window -> cart bank 3
#ifdef BOOT_FBDMAHALT
	/* HARDWARE PROBE: VDP DMA with the 32X FRAMEBUFFER as the source —
	 * the MD-plane consume path. Fill 64 FB words (FM=0), DMA them to
	 * VRAM 0, read VRAM back through the data port. WHITE = identical,
	 * RED = not, then halt. */
	{
		volatile uint16_t *fb = (volatile uint16_t*)0x85E000;
		uint16_t i, bad = 0;
		for (i = 0; i < 64; i++) fb[i] = (uint16_t)(0x1234 + i * 0x0101);
		*(volatile uint16_t*)VDP_CTRL_PORT = 0x8F02;
		*(volatile uint16_t*)VDP_CTRL_PORT = 0x9340;             /* 64 words */
		*(volatile uint16_t*)VDP_CTRL_PORT = 0x9400;
		*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9500 | ((0x85E000ul >> 1) & 0xFF));
		*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9600 | ((0x85E000ul >> 9) & 0xFF));
		*(volatile uint16_t*)VDP_CTRL_PORT = (uint16_t)(0x9700 | ((0x85E000ul >> 17) & 0x7F));
		*vdp_ctrl_wide = 0x40000080ul;                           /* VRAM 0, DMA */
		*vdp_ctrl_wide = 0x00000000ul;                           /* VRAM 0 read */
		for (i = 0; i < 64; i++)
			if (*vdp_data_port != (uint16_t)(0x1234 + i * 0x0101)) bad++;
		MDSTAGE(bad ? 0x001F : 0x7FFF);
		for (;;) ;
	}
#endif
#ifdef BOOT_BANK3HALT
	/* HARDWARE PROBE: can the 68K read the game image through bank 3 of
	 * the banked window? cart 0x300400 = the arcade's bra at 0x400.
	 * WHITE = yes, RED = wrong word; then halt. */
	MDSTAGE(*(volatile uint32_t*)0x900400 == 0x6000000Cul ? 0x7FFF : 0x001F);
	for (;;) ;
#endif

	// Thunk for the one abs.w-encoded jump the rebase couldn't widen
	// (US 0x1B5C6: jmp (47E).w -> jmp (FFFFB3F0).w): jmp 0x90047E.l.
	// The target comes from the game table via game_irq.h (ABSW_JMP).
	{
		volatile uint16_t *t = (volatile uint16_t*)0xFFB3F0;
		t[0] = 0x4EF9;
		t[1] = (uint16_t)(GAME_ABSW_JMP_TARGET >> 16);
		t[2] = (uint16_t)(GAME_ABSW_JMP_TARGET & 0xFFFF);
	}
	// Dispatcher-normalization thunks (see patch_game.py): any handler
	// pointer that escaped the static rebase gets +0x900000 at call
	// time. B3A0 = object dispatcher (handler from (2,A6) -> A0);
	// B3C0 = spawn walker (handler from (8,A0) -> A1).
	{
		static const uint16_t t1[] = {   // movea.l (2,A6),A0
			0x206E, 0x0002,              // cmpa.l #0x40000,A0
			0xB1FC, 0x0004, 0x0000,      // bcc.s +6 (already high)
			0x6406,                      // adda.l #0x900000,A0
			0xD1FC, 0x0090, 0x0000,      // jmp (A0)
			0x4ED0
		};
		static const uint16_t t2[] = {   // movea.l (8,A0),A1
			0x2268, 0x0008,
			0xB3FC, 0x0004, 0x0000,      // cmpa.l #0x40000,A1
			0x6406,
			0xD3FC, 0x0090, 0x0000,      // adda.l #0x900000,A1
			0x4ED1                       // jmp (A1)
		};
		volatile uint16_t *d = (volatile uint16_t*)0xFFB3A0;
		for (unsigned i = 0; i < sizeof t1 / 2; i++) d[i] = t1[i];
		d = (volatile uint16_t*)0xFFB3C0;
		for (unsigned i = 0; i < sizeof t2 / 2; i++) d[i] = t2[i];
	}
#ifdef MD_HSCROLL_DIRECT
	// MDHSCR (docs/log/LOOP-DECOMPILE.md 23-25): the game's two horizontal
	// scroll stores now also write the MD hscroll table themselves. The
	// thunk bodies are generated by patch_game.py, because only it knows
	// what the text-RAM register remapped to under the active flag set.
	// Purely additive: the original store still happens inside the thunk,
	// so the SH-2 still sees the registers and sc[3]/sc[7] still work.
	{
		volatile unsigned short *d =
			(volatile unsigned short*)HSCR_THUNK_BASE;
		for (unsigned i = 0; i < HSCR_THUNK_WORDS; i++)
			d[i] = hscr_thunks[i];
	}
#endif
	// DATA-pointer normalization thunks: same trick for two STORED table
	// pointers whose source values live below 0x28000 (excluded from the
	// byte-harvest sweep — packed-art collisions). Caught by wpcatch.lua
	// on the poisoned low copy during the intro:
	// B340 = multi-object spawn walker's record table (0xDBA8:
	//        movea.l (0x24,A6),A4 — the intro-cast list at low 0xDD46,
	//        so Zeus/orb/rising-player spawned from poison);
	// B360 = palette-cycle streamer's script (0x30D0:
	//        movea.l (2,A5),A0 — glow/fade tables at low 0x1A78E).
	{
		static const uint16_t t3[] = {   // movea.l (0x24,A6),A4
			0x286E, 0x0024,
			0xB9FC, 0x0004, 0x0000,      // cmpa.l #0x40000,A4
			0x6406,                      // bcc.s +6 (already high)
			0xD9FC, 0x0090, 0x0000,      // adda.l #0x900000,A4
			0x4E75                       // rts
		};
		static const uint16_t t4[] = {   // movea.l (2,A5),A0
			0x206D, 0x0002,
			0xB1FC, 0x0004, 0x0000,      // cmpa.l #0x40000,A0
			0x6406,
			0xD1FC, 0x0090, 0x0000,      // adda.l #0x900000,A0
			0x4E75
		};
		volatile uint16_t *d = (volatile uint16_t*)0xFFB340;
		for (unsigned i = 0; i < sizeof t3 / 2; i++) d[i] = t3[i];
		d = (volatile uint16_t*)0xFFB360;
		for (unsigned i = 0; i < sizeof t4 / 2; i++) d[i] = t4[i];
	}
	// TAS thunks (see patch_game.py TAS_SITES): the MD bus drops the
	// TAS write phase, so every tas/bne latch re-fires forever (broke
	// the attract eye gate at 0x2268 — infinite title loop). Each TAS
	// becomes jsr here: tst.b sets TAS's exact N/Z (V/C cleared), st
	// sets the latch without touching CC, rts preserves CC.
	{
		static const uint16_t tt[] = {
			// 0xFFB380: tas $c020.w
			0x4A38, 0xC020, 0x50F8, 0xC020, 0x4E75,
			// 0xFFB38A: tas $f15a.w
			0x4A38, 0xF15A, 0x50F8, 0xF15A, 0x4E75,
			// 0xFFB394: tas (0x3E,A0)
			0x4A28, 0x003E, 0x50E8, 0x003E, 0x4E75,
		};
		static const uint16_t tt2[] = {
			// 0xFFB3F6: tas (0x3C,A6)
			0x4A2E, 0x003C, 0x50EE, 0x003C, 0x4E75,
		};
		volatile uint16_t *d = (volatile uint16_t*)0xFFB380;
		for (unsigned i = 0; i < sizeof tt / 2; i++) d[i] = tt[i];
		d = (volatile uint16_t*)0xFFB3F6;
		for (unsigned i = 0; i < sizeof tt2 / 2; i++) d[i] = tt2[i];
	}

	// Palette mirror (0xFF9000) starts zeroed: boot RAM is random and the
	// first shipped region must not be garbage. Only 4KB now — LOOP 8
	// retired the 0xFFA000 sent-copy along with the diff scan that needed
	// it, so this loop no longer runs over 8KB.
	{
		volatile uint32_t *pm = (volatile uint32_t*)0xFF9000;
		for (uint16_t i = 0; i < 1024; i++)
			pm[i] = 0;
		pm = (volatile uint32_t*)0xFF7000;    // sprite-list mirror
		for (uint16_t i = 0; i < 512; i++)
			pm[i] = 0;
	}
	// Tile dirty-bit thunks (generated: tile_thunks.h) at 0xFFB820 —
	// low word 0xB820 >= 0x8000 so the game's jsr (x).w abs.w
	// SIGN-EXTENDS into MD RAM (0x5E00.w would target low-ROM poison:
	// instant crash at the first thunked site — the "ours never boots"
	// parity run). Dirty bitmap at 0xFFB9FE starts ALL-DIRTY so the
	// SH-2's first cycles sync every page once.
	{
		volatile uint16_t *td = (volatile uint16_t*)0xFFB820;
		for (uint16_t i = 0; i < TILE_THUNK_WORDS; i++)
			td[i] = tile_thunks[i];
		*(volatile uint16_t*)0xFFB9FE = 0x1FFF;
	}
	// Palette dirty-bit thunks (generated: pal_thunks.h) at 0xFFBA00, the
	// same abs.w sign-extension rule as the tile thunks above. This block
	// ends at 0xFFBD1A and the boot stack starts at 0xFFBFF0 — they share
	// this page, but only during boot: the game runs on its own stack at
	// 0xFFFFFF00, and our vint handler is entered in the game's context.
	// The dirty word at 0xFFB9FC starts ALL-DIRTY so the whole palette
	// ships once before the game's first upload.
	{
		volatile uint16_t *pt = (volatile uint16_t*)0xFFBA00;
		for (uint16_t i = 0; i < PAL_THUNK_WORDS; i++)
			pt[i] = pal_thunks[i];
		*(volatile uint16_t*)0xFFB9FC = 0xFFFF;
	}
#if FMGATE_ON
	// LOOP 23 — FM entry-gate thunks, immediately after the pal thunks
	// (FMGATE_THUNK_ADDR is generated from the pal area's actual end;
	// the generator asserts it clears the boot stack at 0xFFBFF0).
	{
		volatile uint16_t *ft =
			(volatile uint16_t*)(0xFF0000uL | FMGATE_THUNK_ADDR);
		for (uint16_t i = 0; i < FMGATE_THUNK_WORDS; i++)
			ft[i] = fmgate_thunks[i];
#ifdef TXT_MASK
		txt_mask = 0xFF;                         /* first capture is full */
#endif
#ifdef GAME_GATE
		*(volatile uint8_t*)0xFFA0F5 = 1;        /* first frame is free */
		*(volatile uint8_t*)0xFFA0F4 = 0;
		*(volatile uint16_t*)0xFFA0F6 = 0;
#endif
#ifdef FBX_PEND
		*(volatile uint32_t*)0xFFA0F8 = (uint32_t)&fbx_late_blast;
		fbx_pend = 0;
		*(volatile uint16_t*)0xFFA0FC = 0;
#endif
	}
#endif
	*(volatile uint16_t*)0xFFB0F4 = 0;   // ITER5 tail-probe max span
	*(volatile uint16_t*)0xFFB0E0 = 0;   // LOOP 7b: DREQ push aborts (own
	                                     // address at last — 0xFFB0F2 is
	                                     // windows-completed)
	*(volatile uint16_t*)0xFFA020 = 0;      // last packet magic (relocated)
	*(volatile uint16_t*)0xFFA022 = 0xFFFF; // push spin-headroom watermark
	*(volatile uint16_t*)0xFFA024 = 0;      // cell records played -> NT A
	*(volatile uint16_t*)0xFFA026 = 0;      // cell records played -> NT B
	*(volatile uint16_t*)0xFFA028 = 0;      // last NT-A readback word
	*(volatile uint16_t*)0xFFA02A = 0;      // NT-A readback mismatches
	*(volatile uint16_t*)0xFFA02C = 0;      // wipe-recheck: prev addr (invalid)
	*(volatile uint16_t*)0xFFA02E = 0;      //   prev value
	*(volatile uint16_t*)0xFFA030 = 0;      //   recheck mismatches
	*(volatile uint16_t*)0xFFA032 = 0;      //   last recheck value
	*(volatile uint16_t*)0xFFA034 = 0;      //   rechecks performed
#ifdef IDLE_TOKEN
	*(volatile uint16_t*)0xFFB0EE = 0;   // LOOP 11a: idle-token skips
#endif
	*(volatile uint32_t*)0xFFB0D0 = 0;   // LOOP15: sum of total spans
	                                     // (ALWAYS-ON handler-mean meter)
	*(volatile uint32_t*)0xFFA040 = 0;   //   sum of window/ack-wait spans
#ifdef TAIL_PROBE
	*(volatile uint32_t*)0xFFB0D4 = 0;   //   sum of stream spans
	*(volatile uint32_t*)0xFFB0D8 = 0;   //   sum of DREQ-push spans
	*(volatile uint32_t*)0xFFB0DC = 0;   //   sum of palette-scan spans
	*(volatile uint8_t*)0xFFB0E8 = 0;    //   tail split: DREQ push
	*(volatile uint8_t*)0xFFB0EA = 0;    //   palette scan
	*(volatile uint8_t*)0xFFB0EC = 0;    //   COMM stream
#endif

	// I/O mailboxes: idle inputs, DIP defaults (DSW2 0xFD = 3 lives, normal,
	// demo sounds on; DSW1 0xFF = 1 coin / 1 credit)
	IO_MISC = 0; IO_SERVICE = 0xFF; IO_P1 = 0xFF; IO_P2 = 0xFF;
	IO_DSW2 = 0xFD; IO_DSW1 = 0xFF; IO_C43007 = 0;
	BANK_SHADOW = 0;

	// MCU-owned state
	MCU_COINS = 0; MCU_SNDCMD = 0xFF; MCU_BUSY = 0;
	// (MCU also sends sound cmd 0x40 at boot — no Z80 yet, noted)

	// Vectors are served from the cart ROM table (RV=1 makes 0x0-0x3FF the
	// cart image): exceptions -> 0xFFB408 rte stub, VBLANK -> _vblank. No
	// runtime vector writes are possible (cart is read-only under RV=1).

	// Interrupt delivery that works under RV=1: the adapter's H-int vector
	// (0x70) is WRITABLE RAM (the security blob itself writes it). Point it
	// straight at our RAM-resident handler — no adapter trampoline, no
	// 0x880000-window fetch. H-int is 68K level 4 == the arcade's IRQ4.
	{
		extern void _vblank(void);
		*(volatile uint32_t*)0x000070 = (uint32_t)&_vblank;
	}

#ifdef WRITECOST_PROBE
	/* 68K WRITE-COST PROBE (2026-09-06): the game's tile writes land in FB
	 * staging (0x852000) and its text in 0x85F000 — both across the 32X
	 * adapter bus. If those cost far more than work RAM, the game's own
	 * pass is paying for our staging choice, and moving staging to WRAM
	 * (shim DMAs dirty pages to the FB in its own window) is the lever.
	 * 1024 word writes each, beam-line timed, into 0xFFA190.. */
	{
		volatile uint16_t *st = (volatile uint16_t*)0xFFA190;
		volatile uint16_t *wr = (volatile uint16_t*)0xFF0000;
		volatile uint16_t *fb = (volatile uint16_t*)0x852000;
		uint16_t v0, v1; uint16_t i;
		v0 = *(volatile uint16_t*)0xC00008;
		for (i = 0; i < 1024; i++) wr[i] = i;
		v1 = *(volatile uint16_t*)0xC00008;
		st[0] = (uint16_t)(((v1 >> 8) - (v0 >> 8)) & 0xFF);
		v0 = *(volatile uint16_t*)0xC00008;
		for (i = 0; i < 1024; i++) fb[i] = i;
		v1 = *(volatile uint16_t*)0xC00008;
		st[1] = (uint16_t)(((v1 >> 8) - (v0 >> 8)) & 0xFF);
		/* and reads, which the game's read-modify-writes also pay */
		v0 = *(volatile uint16_t*)0xC00008;
		for (i = 0; i < 1024; i++) st[2] = wr[i];
		v1 = *(volatile uint16_t*)0xC00008;
		st[3] = (uint16_t)(((v1 >> 8) - (v0 >> 8)) & 0xFF);
		v0 = *(volatile uint16_t*)0xC00008;
		for (i = 0; i < 1024; i++) st[2] = fb[i];
		v1 = *(volatile uint16_t*)0xC00008;
		st[4] = (uint16_t)(((v1 >> 8) - (v0 >> 8)) & 0xFF);
		st[5] = 0xC057;
	}
#endif
	vdp_color(0, 0x0E0);                // GREEN: handing to the rebased game

	MDSTAGE(0x6318);                    /* GREY: uploads done, before B007 */
	*mars_comm14 = 0xB007;              // beacon: shim init complete
	// HOLD THE GAME until the master's V-ISR is armed (COMM14 = 0xB008,
	// sh_src/m_main.c): the game's blank-loaded boot card (its frames
	// 1-19) must meet a live MD-plane channel. Bounded (~4s) so a dead
	// SH-2 still boots the game for the eye.
	// The handler (H-int, level 4) must run meanwhile: the master arms
	// only after its first window, and _vblank returns before the game
	// hook while game_running == 0. Mask again before the handoff (the
	// game's boot expects reset state and enables IRQ4 itself).
	{
		uint32_t hold = 6000000UL;
		__asm__ __volatile__("move.w #0x2300,%%sr" ::: "memory");
		while ((*mars_comm14 & 0xFE00) != 0xB000 && --hold) ;   /* B008 or B1xx */
		__asm__ __volatile__("move.w #0x2700,%%sr" ::: "memory");
	}
	MDSTAGE(0x3000);                    /* DARK BLUE: hold over, entering the game */
	game_running = 1;

	// Enter the game's own boot IN PLACE in the rebased high copy
	// (function-pointer call: GCC emits a real jsr; the game boot sets
	// its own SP, discarding the pushed return).
	((void (*)(void))0x00900400)();
}
