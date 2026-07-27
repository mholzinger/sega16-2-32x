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

// Palette shadow (game writes 0x840000 -> here). 2048 System-16 words.
#define PAL_SHADOW  ((volatile uint16_t*)0xFFA000)

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

	// Stage C palette streaming — independent of the MCU busy/screen-sync
	// state, so it keeps refreshing CRAM even while the game holds sync.
	// Fire-and-forget: one 5-entry batch per frame, index cycles the first
	// 256 System-16 palette words; the SH-2 applies the current batch.
	{
		static uint16_t pal_idx;
		volatile uint16_t *p = PAL_SHADOW + pal_idx;
		*mars_comm2  = p[0];
		*mars_comm4  = p[1];
		*mars_comm6  = p[2];
		*mars_comm8  = p[3];
		*mars_comm10 = p[4];
		*mars_comm0  = 0x8000 | pal_idx;
		pal_idx = (uint16_t)((pal_idx + 5) & 0xFF);
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
