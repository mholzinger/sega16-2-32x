#include "common.h"
#include "tile_thunks.h"

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

__attribute__((section(".data")))
void shim_vblank(void) {
	static uint16_t busy;

	(*(volatile uint16_t*)0xFFB0F0)++;   // diagnostics: handler entries

	// ITER5 TAIL PROBE: V at TRUE handler entry (before the window/ack-spin)
	// so the span includes the window ack-wait — the part that scales with
	// SH-2 speed (the MAME vs ares divergence the post-window probe missed).
	uint8_t v_entry = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);

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
		*mars_comm0 = wcmd;
		spin2 = 8000000UL;
		while (*mars_comm0 && --spin2)
			*mars_comm12 = (uint16_t)(0xD000
				| (*(volatile uint16_t*)0xC00008 >> 8));
		*(volatile uint16_t*)0xA15100 &= 0x7FFF; // FM=0: game owns FB staging
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

	// Dirty palette batch queue: FIFO ring. LIFO consumption starved
	// old entries forever while fades re-dirtied rows every frame
	// (zombie sprite rows queued once, buried, never sent).
	static uint16_t palq[64];
	static uint8_t pal_h, pal_t;
	#define PALQ_CNT ((uint8_t)((pal_t - pal_h) & 63))
	#define PALQ_PUSH(b) do { \
		if (((uint8_t)((pal_t + 1) & 63)) != pal_h) { \
			palq[pal_t] = (b); pal_t = (uint8_t)((pal_t + 1) & 63); } \
	} while (0)

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
	{
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
		uint16_t spin = 800;
		uint8_t ok = 1;
		*(volatile uint16_t*)0xA15110 = 516;  // 512 sprites + bitmap + pad
		*ctrl = 4;                            // 68S: session start
		for (uint16_t g = 0; g < 128; g++) {  // 128 groups of 4 words
			while (*ctrl < 0 && --spin) ;
			if (!spin) { ok = 0; break; }
			fifo[0] = s[0]; fifo[0] = s[1];
			fifo[0] = s[2]; fifo[0] = s[3];
			s += 4;
		}
		if (ok) {                             // tail: dirty-page bitmap
			while (*ctrl < 0 && --spin) ;
			if (spin) {
				volatile uint16_t *bm = (volatile uint16_t*)0xFFB9FE;
				fifo[0] = *bm;                // read...
				*bm = 0;                      // ...and clear (thunks re-OR)
				fifo[0] = 0; fifo[0] = 0; fifo[0] = 0;
			} else
				ok = 0;
		}
		if (!ok) {
			*ctrl = 0;                        // abort session; retry next vint
			(*(volatile uint16_t*)0xFFB0F2)++;
		}
	}

	// Palette dirty scan: the mirror (0xFF9000, game writes) is diffed
	// against the sent-copy (0xFFA000) one 512-word quarter per vint;
	// dirty 5-word batches queue for the COMM stream below. The FB
	// CANNOT carry palette: MD FB-window writes are silently dropped
	// when the SH-2 owns the framebuffer (ares/hardware arbitration —
	// proven by savestate: mirror populated, FB rows still zero), and
	// the vint copy overlapped the blit window near-always. COMM is
	// the one arbitration-free channel and its acked protocol already
	// delivers text reliably on ares. Full-palette convergence after a
	// scene load: <=8 vints; steady state: zero palette batches.
	{
		static uint8_t scan_q;
		const uint16_t *mir  = (const uint16_t*)(0xFF9000 + (uint32_t)scan_q * 1024);
		uint16_t *sent = (uint16_t*)(0xFFA000 + (uint32_t)scan_q * 1024);
		uint16_t qbase = (uint16_t)(scan_q * 512);
		for (uint16_t i = 0; i < 512; i += 4) {
			if (mir[i] != sent[i] || mir[i+1] != sent[i+1] ||
			    mir[i+2] != sent[i+2] || mir[i+3] != sent[i+3]) {
				uint16_t b0 = (uint16_t)((qbase + i) / 5);
				uint16_t b1 = (uint16_t)((qbase + i + 3) / 5);
				if (pal_h == pal_t ||
				    palq[(uint8_t)((pal_t - 1) & 63)] != b0)
					PALQ_PUSH(b0);
				if (b1 != b0)
					PALQ_PUSH(b1);
			}
		}
		scan_q = (uint8_t)((scan_q + 1) & 3);
	}

	// Stage C shadow streaming — independent of the MCU busy/screen-sync
	// state. Acked COMM protocol (SH-2 clears COMM0 per batch): alternating
	// TEXT batches only: the palette lives in the MD RAM mirror at
	// 0xFF9000 and is copied into FB staging above — no palette
	// COMM streaming.
	// Text RAM (2048 words) fully refreshes every ~6.4 frames.
	// ITER5 TAIL PROBE (split): V at the START of the STREAM section, to
	// separate the COMM stream cost from the DREQ push + palette scan.
	uint8_t v_stream = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);
	{
		static uint16_t txt_idx;
		uint8_t pal_sent_now = 0;

		// TOTAL ack-wait budget for the whole stream section, not per
		// batch (see NOTES: per-batch spins trickled 60+ms handler
		// entries when the SH-2s ack slowly — the freeze spiral).
		uint16_t spin = 800;

		for (uint16_t burst = 0; burst < 64; burst++) {
			if (burst) {
				while (*mars_comm0 && --spin) ;
				if (*mars_comm0)
					break;               // SH-2 busy; resume next frame
			}
			// PRIORITY: the layer-register block (0x740-0x753) AND
			// the ROWSCROLL tables (0x7C0-0x7FB) ship EVERY vint as
			// the first 16 batches. The rotating refresh gave each
			// word 0-6.4 frames of staleness — fine for static
			// scenes, but fast pans compose mismatched pages/scroll
			// (jumps, seams) and the loop-2+ attract water RIPPLE
			// (per-row xscroll) tore into displaced strips when its
			// table words lagged randomly. Rest still rotates (full
			// text refresh every ~8.5 frames).
			// batch order: layer regs + rowscroll (16) > dirty
			// palette batches > rotating text refresh
			uint16_t idx, tag = 0x4000;
			const uint16_t *t;
			if (burst < 4)
				idx = (uint16_t)(0x740 + burst * 5);
			else if (burst < 16)
				idx = (uint16_t)(0x7C0 + (burst - 4) * 5);
			else if (pal_h != pal_t && pal_sent_now < 8) {
				// cap 8 palette batches/vint: an uncapped flood
				// displaced text batches and stretched slave stream
				// servicing, starving band compose on ares (group-1
				// fallback garbage). 8/vint still converges a full
				// palette reload in ~1s; typical fades fit one vint.
				uint16_t b = palq[pal_h];      // FIFO: oldest first
				pal_h = (uint8_t)((pal_h + 1) & 63);
				idx = (uint16_t)(b * 5);
				tag = 0x4800;                  // palette batch
				pal_sent_now++;
			} else
				idx = txt_idx;
			if (tag == 0x4800) {
				const uint16_t *m = (const uint16_t*)0xFF9000 + idx;
				uint16_t *s = (uint16_t*)0xFFA000 + idx;
				s[0] = m[0]; s[1] = m[1]; s[2] = m[2];
				s[3] = m[3]; s[4] = m[4];
				t = m;
			} else
				t = (const uint16_t*)0xFF8000 + idx;
			*mars_comm2  = t[0];
			*mars_comm4  = t[1];
			*mars_comm6  = t[2];
			*mars_comm8  = t[3];
			*mars_comm10 = t[4];
			*mars_comm0  = tag | idx;
			if (tag == 0x4000 && burst >= 16) {
				txt_idx += 5;
				if (txt_idx >= 2045)
					txt_idx = 0;
			}
		}
	}

	// ITER5 TAIL PROBE: shim-handler span in scanlines, entry V (0xFFB0FE,
	// written at the window phase) -> here, the END of the per-vint tail
	// (DREQ push + palette scan + text/palette stream). This tail runs on
	// EVERY vint, gate-rejected or not; if it alone overruns the frame it
	// is the steady-state 68K load that pins the V-gate reject band (the
	// window-shortening fixes never touched it). Running MAX in 0xFFB0F4:
	// a span approaching one frame (~262 lines) = the handler exceeds a
	// frame -> next H-int fires late -> reject. Small (<~120) = the tail
	// fits and the reject cause is elsewhere.
	(void)v_stream;
	{
		uint8_t ev = (uint8_t)(*(volatile uint16_t*)0xC00008 >> 8);
		uint8_t total = (uint8_t)(ev - v_entry);   // TRUE entry -> here
		uint8_t win = (uint8_t)(v_win - v_entry);  // window/ack-spin only
		// F4 = (LAST-vint TOTAL handler span << 8) | WINDOW span, scanlines.
		// LAST-vint (not max) so a savestate during a STEADY scene reads
		// the steady operating point, not a scene-transition spike. Frame
		// = 262. total >= 262 = the handler overran -> next H-int late ->
		// V-gate reject. win = the ack-spin (SH-2-speed-bound = the ares
		// divergence the post-window probe missed).
		*(volatile uint16_t*)0xFFB0F4 =
			(uint16_t)(((uint16_t)total << 8) | win);
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

	// Palette mirror (0xFF9000) and sent-copy (0xFFA000) start zeroed
	// and equal: boot RAM is random, and the dirty scan must not
	// stream garbage before the game's first palette upload
	{
		volatile uint32_t *pm = (volatile uint32_t*)0xFF9000;
		for (uint16_t i = 0; i < 2048; i++)
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
	*(volatile uint16_t*)0xFFB0F4 = 0;   // ITER5 tail-probe max span

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
