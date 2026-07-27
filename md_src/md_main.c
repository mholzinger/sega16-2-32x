#include "common.h"

// MD-side shim for the arcade game. Runs entirely from work RAM (.data):
// once RV=1 the low ROM map belongs to the game, and the 0x880000 window
// must stay untouched, so nothing here may execute from or read cart ROM.
//
// Stage A: prove the relocated boot chain (blob @0x40400 -> _start @0x8C0810
// -> RV=1 -> RAM main) completes. Beacon on COMM14, heartbeat on COMM12.

static volatile uint16_t* const vdp_data_port = (uint16_t*) VDP_DATA_PORT;
static volatile uint16_t* const vdp_ctrl_port = (uint16_t*) VDP_CTRL_PORT;
static volatile uint32_t* const vdp_ctrl_wide = (uint32_t*) VDP_CTRL_PORT;
static volatile uint16_t* const mars_comm12  = (uint16_t*) MARS_COMM12;
static volatile uint16_t* const mars_comm14  = (uint16_t*) MARS_COMM14;

extern uint16_t read_joypad(uint8_t player);

__attribute__((section(".data")))
void vdp_color(uint16_t index, uint16_t color) {
	index <<= 1;
	*vdp_ctrl_wide = ((0xC000 + (((uint32_t)index) & 0x3FFF)) << 16) + (((uint32_t)index) >> 14);
	*vdp_data_port = color;
}

__attribute__((section(".data")))
void main(void) {
	uint16_t ticks = 0;

	*(volatile uint8_t*)0xA15107 = 1;   // RV=1: cart at 0x000000 — set from RAM,
	                                    // never from the 0x880000 window
	*mars_comm14 = 0xB007;          // stage-A beacon: boot chain completed

	while (1) {
		// visible MD-layer sign of life while the shim idles
		while (*vdp_ctrl_port & 8) ;
		while (!(*vdp_ctrl_port & 8)) ;
		vdp_color(0, (++ticks & 8) ? 0x00A : 0x000);
		*mars_comm12 = ticks;       // heartbeat
	}
}
