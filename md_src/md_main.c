#include "common.h"
#include "tile_thunks.h"
#include "pal_thunks.h"

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
static volatile uint16_t* const mars_comm8  = (uint16_t*) MARS_COMM8;
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
#define MCU_COINS   (*(volatile uint8_t*)0xFFF0C2)  // MCU posts inverted SERVICE
#define MCU_BANKREQ (*(volatile uint8_t*)0xFFF095)  // game's tile bank request
#define MCU_SNDCMD  (*(volatile uint8_t*)0xFFF0C4)  // sound mailbox (0xFF = idle)
#define MCU_BUSY    (*(volatile uint8_t*)0xFFF0C0)  // screen-sync handshake
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
	for (uint16_t row = 0; row < 32; row++) {
		uint32_t a = 0xE000u + (uint32_t)row * 128u
		           + ((row < 28) ? 80u : 0u);
		uint16_t n = (row < 28) ? 24 : 64;
		*vdp_ctrl_wide = ((0x4000u | (a & 0x3FFFu)) << 16)
		               | ((a >> 14) & 3u);
		for (uint16_t c2 = 0; c2 < n; c2++)
			*vdp_data_port = 0x03FF;   /* blank slot, pal 0 */
	}
	/* A/B: full-screen hscroll (reg 11 = 00). Cell mode made the MD
	 * plane vanish per-strip on MAME while VRAM/CRAM verified correct
	 * through the data port; bisecting whether the per-strip table is
	 * the breakage. */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8B00;
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

__attribute__((section(".data")))
void shim_vblank(void) {
	static uint16_t busy;
#ifdef MD_BG
	{
		static uint8_t painted;
		if (!painted) { painted = 1; md_bg_palette(); }
	}
#endif

	(*(volatile uint16_t*)0xFFB0F0)++;   // diagnostics: handler entries

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
	static uint16_t wskip;
	{
		uint32_t spin2;
		uint16_t wcmd;
		// 3-phase cycle, NO dedicated compose vint: every window blits
		// one 75-row slice inside vblank, then composes the next
		// frame's sprites/text into already-shipped rows (row-following
		// pipeline; tiles fill in concurrently between windows on the
		// SDRAM cache). Full frame ships every 3 vints = 20Hz display.
		uint16_t next = wskip + 1;
		if (next >= 3) next = 0;
		wcmd = (uint16_t)(0x2000 | (next << 4));
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
			// on-time vint: V reads 0xDF (counter not yet stepped past
			// line 223 at IRQ time — MAME-measured). Upper bound is
			// TIGHT (0xE2, ~4 lines in): 75-row slices starting at
			// V=0xE5-0xE6 left only ~1.7ms of vblank and missed the
			// restore on ares ~0.5% of frames (black frame each time,
			// field-measured 11/2063). A late start now retries next
			// vint instead of gambling the flip-back.
			if (v < 0xDF || v > 0xE2) {
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
		*mars_comm12 = (uint16_t)(0xD000
			| (*(volatile uint16_t*)0xC00008 >> 8));
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
		spin2 = 8000000UL;
		while (*mars_comm0 && --spin2)
			*mars_comm12 = (uint16_t)(0xD000
				| (*(volatile uint16_t*)0xC00008 >> 8));
		*(volatile uint16_t*)0xA15100 &= 0x7FFF; // FM=0: game owns FB staging
#ifdef MD_BG
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
			volatile uint16_t *live = (volatile uint16_t*)0x851A00;
			volatile uint16_t *sc = live;
			(*(volatile uint16_t*)0xFFB0E0) = live[0];    // diag: last magic seen
			// SPAN-SPLIT PROBES (write-budget design): V at each stage,
			// packed per type so lua can attribute the cost.
			(*(volatile uint16_t*)0xFFB0B0) =
				*(volatile uint16_t*)0xC00008;            // V at entry
			if (live[0] == 0xB6B6) {
				(*(volatile uint16_t*)0xFFB0E2)++;        // diag: packets consumed
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
				if (sc[1] == 0) (*(volatile uint16_t*)0xFFB0E4)++;   // tile batches
				else            (*(volatile uint16_t*)0xFFB0E6)++;   // name chunks
				(*(volatile uint16_t*)0xFFB0E8) = sc[5];  // last count
				uint16_t typ = sc[1], cnt = sc[5];
				// vertical fine scroll, every window and nearly free
				// (horizontal is per-strip now: cell-mode hscroll words
				// ride each name-table chunk)
				// VSRAM addr 2 = plane B. The old 0x40000010|2 put the
				// 2 in the SECOND control word (address bits 16:14), so
				// it wrote VSRAM 0 -- PLANE A's vscroll -- all along.
				// (A/B'd during the MAME blackout hunt: not the cause.)
				*vdp_ctrl_wide = ((uint32_t)(0x4000u | 2u) << 16) | 0x10u;
				*vdp_data_port = sc[4];
				*vdp_ctrl_wide = ((uint32_t)0x4000u << 16) | 0x10u;
				*vdp_data_port = sc[6];       // VSRAM 0 = plane A (FG) vy
				(*(volatile uint16_t*)0xFFB0B2) =
					*(volatile uint16_t*)0xC00008;    // V after scroll
				if (typ == 0) {
					// each entry: slot word + 32 bytes of 4bpp planar
					volatile uint16_t *e = sc + 8;
					for (uint16_t i = 0; i < cnt; i++, e += 17) {
						uint32_t va = (uint32_t)e[0] * 32u;
						if (va + 32u > 0xB000u) continue;
						*vdp_ctrl_wide = ((0x4000u | (va & 0x3FFFu)) << 16)
						               | ((va >> 14) & 3u);
						for (uint16_t k = 1; k < 17; k++)
							*vdp_data_port = e[k];
#ifdef MD_VERIFY
						// LOOP 13 tick-row probe: read the entry back
						// (VRAM read = code 0) and compare. A lost or
						// misplaced data-port write shifts the pattern
						// tail = the sky dash. Tally at 0xFFB0EA;
						// first bad triple parked in free WRAM 0xFFA000
						// for state extraction. NEVER SHIP: doubles the
						// receiver span.
						{
							volatile uint16_t *vw =
							    (volatile uint16_t*)0xFFA000;
							*vdp_ctrl_wide = ((va & 0x3FFFu) << 16)
							               | ((va >> 14) & 3u);
							for (uint16_t k = 1; k < 17; k++) {
								uint16_t rb = *vdp_data_port;
								if (rb != e[k]) {
									vw[6]++;
									if (!vw[0]) {
										vw[0] = 1;          // valid
										vw[1] = (uint16_t)va;
										vw[2] = k;          // word idx
										vw[3] = e[k];       // wrote
										vw[4] = rb;         // read
										vw[5] = *(volatile uint16_t*)
										        0xC00008;   // HV now
									}
								}
							}
						}
#endif
					}
					(*(volatile uint16_t*)0xFFB0B4) =
						*(volatile uint16_t*)0xC00008; // V after tiles
				} else {
					// name-table cells, 40 per screen row, 64-cell stride.
					// sc[2] bit 15 = PLANE A (FG cat-0) chunk; else B.
					// ROW-MAJOR: the old per-cell `% 40` + `/ 40` were
					// two 68K DIVU's per cell (~280 cycles) — measured
					// 129 SCANLINES per chunk, the whole receiver span.
					// One division per packet now.
					volatile uint16_t *e = sc + 8;
					uint16_t cell0 = sc[2] & 0x7FFF;
					uint32_t nbase = (sc[2] & 0x8000u) ? 0xC000u : 0xE000u;
					uint16_t row = cell0 / 40u;
					uint16_t i = 0;
					for (uint16_t r = 0; r < 7 && i < cnt; r++, row++) {
						uint32_t a = nbase + (uint32_t)row * 128u;
						*vdp_ctrl_wide = ((0x4000u | (a & 0x3FFFu)) << 16)
						               | ((a >> 14) & 3u);
						for (uint16_t c = 0; c < 40 && i < cnt; c++, i++)
							*vdp_data_port = e[i];
					}
					// full-screen hscroll from the chunk's first strip
					// word: plane A word at 0xFC00, plane B at 0xFC02
					// (cell-mode per-strip stays parked)
					{
						uint32_t ha = (sc[2] & 0x8000u) ? 0x3C00u : 0x3C02u;
						*vdp_ctrl_wide = ((uint32_t)(0x4000u | ha) << 16) | 3u;
						*vdp_data_port = e[280];
					}
					(*(volatile uint16_t*)0xFFB0B6) =
						*(volatile uint16_t*)0xC00008; // V after cells
				}
				// live BG palette: 48 words at fixed offset 688 -> CRAM
				// lines 1-3 (entries 16-63); line 0 stays the grey/text
				// ramp. Rides every packet so fades track at window
				// cadence.
				// live BG palette: 48 words at fixed offset 688 -> CRAM
				// lines 1-3 (entries 16-63); line 0 stays the grey/text
				// ramp. Rides every packet so fades track at window
				// cadence. (A/B'd during the MAME blackout hunt: not
				// the cause.)
				*vdp_ctrl_wide = ((uint32_t)(0xC000u | 32u) << 16);
				for (uint16_t i = 0; i < 48; i++)
					*vdp_data_port = sc[688 + i];
				(*(volatile uint16_t*)0xFFB0B8) =
					*(volatile uint16_t*)0xC00008;    // V after CRAM
				live[0] = 0;                // consumed (the FB packet)
packet_done: ;
			}
		}
#endif
		// The SH-2's final FS restore may LATCH only at the next vblank
		// (ares/hardware defer FBCTL writes made outside vblank). The game
		// must not resume while its staging bank is deselected — hold here
		// until FS reads back at its steady value (immediate on MAME).
		{
			uint32_t g = 200000UL;
			uint16_t fs_home = *(volatile uint16_t*)0xFFB0F6;
			while ((*(volatile uint16_t*)0xA1518A & 1) != fs_home && --g) ;
		}
		// (unpair model: RV stays 0 — the game fetches through 0x900000)
		(*(volatile uint16_t*)0xFFB0F2)++;       // diagnostics: windows completed
		window_ok = 1;                           // master re-armed the DMA
window_done: ;
	}

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
		uint16_t kk = wskip;                  // the k just posted+acked
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
		static uint16_t pal_retry;
		static uint8_t pal_next;
		uint16_t pal_pr = 0;                  // 1 + pair index, 0 = none
		if (kk != 0) {
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
		// Length must be published BEFORE the session starts, so the
		// pair is chosen here rather than at the point it is pushed.
		*(volatile uint16_t*)0xA15110 = (kk == 0) ? 596
					      : (pal_pr ? 596 : 340);
		*ctrl = 4;                            // 68S: session start
		// Two loops, not one with an index test: a per-group branch here
		// costs ~1 scanline of tail, and the master's window-pickup slack
		// is only 2-5 lines — enough to flip MAME's blit-skip regime.
		{
			const uint16_t *lr = (const uint16_t*)0xFF8000 + 0x740;
			for (uint16_t lg = 0; lg < 5; lg++) {
				while (*ctrl < 0 && --spin) ;
				if (!spin) { ok = 0; break; }
				fifo[0] = lr[0]; fifo[0] = lr[1];
				fifo[0] = lr[2]; fifo[0] = lr[3];
				lr += 4;
			}
		}
		if (ok) {
			const uint16_t *rs = (const uint16_t*)0xFF8000 + 0x7C0;
			for (uint16_t rg = 0; rg < 15; rg++) {
				while (*ctrl < 0 && --spin) ;
				if (!spin) { ok = 0; break; }
				fifo[0] = rs[0]; fifo[0] = rs[1];
				fifo[0] = rs[2]; fifo[0] = rs[3];
				rs += 4;
			}
		}
		if (ok) {                             // 80 bitmap, 81 text base+tag
			while (*ctrl < 0 && --spin) ;
			if (spin) {
				volatile uint16_t *bm = (volatile uint16_t*)0xFFB9FE;
				fifo[0] = *bm;
				*bm = 0;
				fifo[0] = pal_pr
					? (uint16_t)(txt_dma_base | ((pal_pr - 1) << 11) | 0x8000)
					: txt_dma_base;
				// Body: the sprite list after w0 (so it LANDS for w1,
				// where the harvest is — the game's vint handler rebuilds
				// the list after our window in the IRQ chain), else the
				// optional palette region followed by the rotating text
				// chunk. One loop, one source pointer, one count: the
				// phases differ only in those.
				if (pal_pr) {
					const uint16_t *p = (const uint16_t*)0xFF9000
						+ ((pal_pr - 1) << 8);
					for (uint16_t g = 0; g < 64; g++) {
						while (*ctrl < 0 && --spin) ;
						if (!spin) { ok = 0; break; }
						fifo[0] = p[0]; fifo[0] = p[1];
						fifo[0] = p[2]; fifo[0] = p[3];
						p += 4;
					}
				}
				const uint16_t *b = (kk == 0)
					? s : (const uint16_t*)0xFF8000 + txt_dma_base;
				uint16_t ng = (kk == 0) ? 128 : 64;
				for (uint16_t g = 0; ok && g < ng; g++) {
					while (*ctrl < 0 && --spin) ;
					if (!spin) { ok = 0; break; }
					fifo[0] = b[0]; fifo[0] = b[1];
					fifo[0] = b[2]; fifo[0] = b[3];
					b += 4;
				}
				if (ok) { fifo[0] = 0; fifo[0] = 0; }  // pad to 596 / 340
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
			if (kk != 0) {
				txt_dma_base = (uint16_t)(txt_dma_base + 256);
				if (txt_dma_base >= 2048) txt_dma_base = 0;
				// Consume the region ONLY on a completed push. An aborted
				// push leaves both the mark and the retry alone, so the
				// region simply goes next time — the same stale-beats-
				// lost rule the sprite list follows. A region that was
				// FRESHLY marked earns one repeat (the race above); one
				// that arrived here only as a repeat is now done.
				if (pal_pr) {
					volatile uint16_t *pdw = (volatile uint16_t*)0xFFB9FC;
					uint16_t bit = (uint16_t)(3u << ((pal_pr - 1) << 1));
					uint16_t fresh = (uint16_t)(*pdw & bit);
					*pdw &= (uint16_t)~bit;
					pal_retry = (uint16_t)((pal_retry & ~bit) | fresh);
				}
			}
		} else {
			*ctrl = 0;
			(*(volatile uint16_t*)0xFFB0E0)++;   // DREQ push aborts
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
#ifdef TAIL_PROBE
		// LOOP 6 TAIL METRICS. The max span is dominated by rare worst
		// frames and barely moves under changes that cut real work by 6x
		// (gating COMM sends 64 -> 11 left the max stream span at ~120),
		// so accumulate MEANS too — sustained load is what starves the
		// game. 0xFFB0D0 = sum of total spans, 0xFFB0D4 = sum of stream
		// spans, over 0xFFB0F0 vints. Mean = sum / vints.
		*(volatile uint32_t*)0xFFB0D0 += total;
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

	BANK_SHADOW = MCU_BANKREQ;           // tile bank req -> shadow (SH-2 later)
	*mars_comm12 += 1;                   // frame heartbeat

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
	while (*mars_comm14 != 0x600D) ;
	vdp_color(0, 0x0EE);                // YELLOW: master is SDRAM-resident
	{
		uint16_t c6 = *mars_comm6;
		while (*mars_comm6 == c6) ;
	}
	vdp_color(0, 0xEE0);                // CYAN: slave alive too

	// FM=0: 68K owns the FB window during gameplay so the game's remapped
	// tile-RAM writes (FB staging 0x852000) land. The SH-2 is done with its
	// VDP init (it posted 0x600D from SDRAM); from here it only touches
	// FB/CRAM inside the render window, where the shim raises FM first.
	*(volatile uint16_t*)0xA15100 &= 0x7FFF;

	// UNPAIR MODEL (NOTES.md "REBASE DESIGN v2"): RV stays 0 FOREVER.
	// The game executes its REBASED copy through the banked 0x900000
	// cart window (bank 3 -> cart 0x300000, delta +0x900000): the SH-2s
	// may touch cart ROM at ANY time, concurrent with the 68K's own
	// (bus-arbitrated) instruction fetches — the standard commercial-32X
	// memory model. No RAM stash: the game's boot bytes at arcade
	// 0x400-0x807 (displaced in the LOW cart copy by the Sega security
	// blob) exist intact in the high copy and run in place at 0x900400.
	*(volatile uint16_t*)0xA15104 = 3;  // 0x900000 window -> cart bank 3

	// Thunk for the one abs.w-encoded jump the rebase couldn't widen
	// (0x1B5C6: jmp (47E).w -> jmp (FFFFB3F0).w): jmp 0x90047E.l
	{
		volatile uint16_t *t = (volatile uint16_t*)0xFFB3F0;
		t[0] = 0x4EF9;
		t[1] = 0x0090;
		t[2] = 0x047E;
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
	*(volatile uint16_t*)0xFFB0F4 = 0;   // ITER5 tail-probe max span
	*(volatile uint16_t*)0xFFB0E0 = 0;   // LOOP 7b: DREQ push aborts (own
	                                     // address at last — 0xFFB0F2 is
	                                     // windows-completed)
#ifdef IDLE_TOKEN
	*(volatile uint16_t*)0xFFB0EE = 0;   // LOOP 11a: idle-token skips
#endif
#ifdef TAIL_PROBE
	*(volatile uint32_t*)0xFFB0D0 = 0;   // LOOP 6: sum of total spans
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

	vdp_color(0, 0x0E0);                // GREEN: handing to the rebased game

	*mars_comm14 = 0xB007;              // beacon: shim init complete
	game_running = 1;

	// Enter the game's own boot IN PLACE in the rebased high copy
	// (function-pointer call: GCC emits a real jsr; the game boot sets
	// its own SP, discarding the pushed return).
	((void (*)(void))0x00900400)();
}
