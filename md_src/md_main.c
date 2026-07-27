#include "common.h"

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
	// TWO alternating short windows, one per H-int: the BLIT window
	// (0x2000, flip-pair + split blit, fits inside vblank where FBCTL
	// latches immediately even on deferred-latch hardware) and the
	// COMPOSE window (0x2100, sprites/text/copies, no flips). Longest
	// 68K stall drops to the compose window (~7ms).
	static uint16_t wskip;
	{
		uint32_t spin2;
		uint16_t wcmd = (++wskip & 1) ? 0x2000 : 0x2100;
		while (*mars_comm0) ;                    // drain any pending stream batch
		*(volatile uint8_t*)0xA15107 = 0;        // RV=0: SH-2 can write framebuffer
		// FM=1: hand the VDP (FB/CRAM) to the SH-2 for the window; FM
		// stays 0 outside so the GAME's staged writes land.
		*(volatile uint16_t*)0xA15100 |= 0x8000;
		*mars_comm2 = BANK_SHADOW;               // tile bank 1 value for renderer
		*mars_comm0 = wcmd;
		spin2 = 8000000UL;
		while (*mars_comm0 && --spin2) ;
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
		*(volatile uint8_t*)0xA15107 = 1;        // RV=1: game can fetch ROM again
		(*(volatile uint16_t*)0xFFB0F2)++;       // diagnostics: windows completed
	}


	// STAGED-PALETTE TRACER (temporary): border reports what the 68K sees
	// in the FB-staged palette at handler entry (FM=0, access bank):
	//   GREEN  = tile AND sprite palette halves nonzero (healthy)
	//   YELLOW = tile half ok, SPRITE half (0x85F800+) reads empty
	//   RED    = tile half empty too
	//   MAGENTA (sticky) = access-bank parity broke (bank tearing)
	{
		static uint16_t last_fs = 0xFFFF, torn;
		uint16_t fs = *(volatile uint16_t*)0xA1518A & 1;   // FM=0 here: readable
		if (last_fs != 0xFFFF && fs != last_fs)
			torn = 1;
		last_fs = fs;
		uint16_t tpal = 0, spal = 0;
		volatile uint16_t *pp = (volatile uint16_t*)0x85F000;
		for (uint16_t k = 0; k < 32; k++)
			tpal |= pp[k];
		pp = (volatile uint16_t*)0x85F800;
		for (uint16_t k = 0; k < 64; k++)
			spal |= pp[k];
		vdp_color(0, torn ? 0xE0E : (tpal ? (spal ? 0x0E0 : 0x0EE) : 0x00E));
		*(volatile uint16_t*)0xFFB0F4 = (uint16_t)((fs << 8) | (tpal ? 2 : 0) | (spal ? 4 : 0) | torn);
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

	// Stage C shadow streaming — independent of the MCU busy/screen-sync
	// state. Acked COMM protocol (SH-2 clears COMM0 per batch): alternating
	// TEXT batches only: the palette now lives in FB staging (0x85F000)
	// and is read in-window by the SH-2 — no palette streaming at all.
	// Text RAM (2048 words) fully refreshes every ~6.4 frames.
	{
		static uint16_t txt_idx;

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
			volatile uint16_t *t = (volatile uint16_t*)0xFF8000 + txt_idx;
			*mars_comm2  = t[0];
			*mars_comm4  = t[1];
			*mars_comm6  = t[2];
			*mars_comm8  = t[3];
			*mars_comm10 = t[4];
			*mars_comm0  = 0x4000 | txt_idx;
			txt_idx += 5;
			if (txt_idx >= 2045)
				txt_idx = 0;
		}
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

	*(volatile uint8_t*)0xA15107 = 1;   // RV=1: cart at 0x000000 — set from RAM,
	                                    // never from the 0x880000 window

	// Copy the game's displaced boot [0x400,0x808) + continuation jmp into
	// work RAM at 0xFFB400 (cart stash at 0x40000, readable via RV low map).
	{
		volatile uint16_t *src = (volatile uint16_t*)0x040000;
		volatile uint16_t *dst = (volatile uint16_t*)0xFFB400;
		for (int i = 0; i < 0x40C / 2; i++)
			dst[i] = src[i];
	}

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

	vdp_color(0, 0x0E0);                // GREEN: RV set, stash copied, handing to game

	*mars_comm14 = 0xB007;              // beacon: shim init complete
	game_running = 1;

	// Enter the game's own boot in its RAM copy via a function-pointer call
	// (GCC emits a real jsr; the earlier inline-asm jmp was dropped by the
	// optimizer, leaving main to fall through into bss). The game boot sets
	// its own SP at 0xFFB40E, so the pushed return is discarded.
	((void (*)(void))0x00FFB400)();
}
