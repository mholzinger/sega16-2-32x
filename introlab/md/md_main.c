/* introlab 68K side. Trimmed from sndtest/md/md_main.c (same author): the COMM0 command
 * server for the MD text plane, plus the two things the SH-2s need every frame and may
 * not fetch themselves -- pad 1 on COMM8 and the vblank count on COMM12. No sound: the
 * Z80 is parked and the PSG silenced once at boot. */
#include "common.h"
#include "smoke_sine.h"

static volatile uint16_t* const mars_comm0  = (uint16_t*) MARS_COMM0;
static volatile uint16_t* const mars_comm2  = (uint16_t*) MARS_COMM2;
static volatile uint16_t* const mars_comm8  = (uint16_t*) MARS_COMM8;
static volatile uint16_t* const mars_comm12 = (uint16_t*) MARS_COMM12;
static volatile uint16_t* const vdp_data_port = (uint16_t*) VDP_DATA_PORT;
static volatile uint16_t* const vdp_ctrl_port = (uint16_t*) VDP_CTRL_PORT;
static volatile uint32_t* const vdp_ctrl_wide = (uint32_t*) VDP_CTRL_PORT;

extern uint16_t read_joypad(uint8_t player);

uint16_t timer = 0;
uint16_t vramOffset = 0;
/* md_start.s's pad reader tests this before releasing the Z80 bus: the held grant is the park. */
uint8_t z80_parked = 1;

/* THE SMOKE LAYER (2026-11-14). The SH-2 uploads its tiles and plane-A cells with commands
 * 9 and 10 and switches it on with 11; from then on every vblank rewrites plane A's
 * per-line horizontal scroll for lines 112-223 with a travelling sine, which is all the
 * motion there is. The scroll table moves to 0xF800 for it: a per-line table at the boot
 * address 0xFC00 would run into the sprite table at 0xFE00. */
#define SMOKE_HSCROLL 0xF800u
uint16_t vramAddr = 0;
uint8_t smoke_on = 0;

__attribute__((section(".data")))
static void vram_at(uint16_t a) {
	*vdp_ctrl_wide = ((uint32_t)(0x4000 | (a & 0x3FFF)) << 16) | (a >> 14);
}

__attribute__((section(".data")))
static void smoke_wave(void) {
	*vdp_ctrl_port = 0x8F04;                            /* step one line: plane A's word only */
	vram_at(SMOKE_HSCROLL + 112 * 4);
	uint16_t ph = timer + 112;
	for (uint16_t y = 112; y < 224; y++) *vdp_data_port = (uint16_t)smoke_sine[ph++ & 127];
	*vdp_ctrl_port = 0x8F02;
}

__attribute__((section(".data")))
void do_commands(void) {
	uint16_t cmd = *mars_comm0;
	switch (cmd >> 8) {
	default: break;
	case 0: return;
	case 4:   /* clear name table B; the font in VRAM is not touched */
		for (uint16_t row = 0; row < 28; row++) {
			uint32_t ofs = (row * 64u * 2u) + 0xE000;
			*vdp_ctrl_wide = (uint32_t)(0x4000 | (ofs & 0x3FFF)) << 16 | ((ofs >> 14) | 0x03);
			for (uint16_t i = 0; i < 40; i++) *vdp_data_port = 0;
		}
		break;
	case 5:   /* set the plane offset; masked so the control word can never be a register write */
		vramOffset = (uint16_t)(*mars_comm2 & 0x1FFF);
		break;
	case 6:   /* write one tile word to plane B at the offset, then advance */
		*vdp_ctrl_wide = (((uint32_t)0x6000 + (vramOffset & 0x3FFF)) << 16) + ((vramOffset >> 14) | 0x03);
		*vdp_data_port = *mars_comm2;
		vramOffset += 2;
		break;
	case 8: { /* MD CRAM entry: index in the low byte, BGR word in COMM2 */
		uint16_t index = (uint16_t)((cmd & 0xFF) << 1);
		*vdp_ctrl_wide = ((0xC000 + ((uint32_t)index & 0x3FFF)) << 16) + ((uint32_t)index >> 14);
		*vdp_data_port = *mars_comm2;
		break;
	}
	case 9:   /* set the VRAM write address */
		vramAddr = *mars_comm2;
		break;
	case 10:  /* write one word to VRAM at the address, then advance */
		vram_at(vramAddr);
		*vdp_data_port = *mars_comm2;
		vramAddr += 2;
		break;
	case 11:  /* smoke on (1) or off (0): per-line scroll from a cleared table, or back to boot */
		smoke_on = (uint8_t)(*mars_comm2 & 1);
		vram_at(SMOKE_HSCROLL);
		for (uint16_t i = 0; i < 224 * 2; i++) *vdp_data_port = 0;
		*vdp_ctrl_port = smoke_on ? 0x8D00 | (SMOKE_HSCROLL >> 10) : 0x8D00 | (0xFC00 >> 10);
		*vdp_ctrl_port = smoke_on ? 0x8B03 : 0x8B00;
		break;
	}
	*mars_comm0 = 0;
}

__attribute__((section(".data")))
void main(void) {
	{   /* park the Z80 by holding its bus, then silence the PSG */
		volatile uint16_t *busreq = (uint16_t *)Z80_BUS_REQ;
		volatile uint16_t *reset  = (uint16_t *)Z80_RESET;
		volatile uint8_t  *psg    = (uint8_t *)0xC00011;
		uint32_t g = 200000;
		*busreq = 0x100; *reset = 0x100;
		while ((*busreq & 0x100) && --g) ;
		*psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
	}
	while (1) {
		while (*vdp_ctrl_port & 8) do_commands();
		while (!(*vdp_ctrl_port & 8)) do_commands();
		*mars_comm8  = read_joypad(0);     /* published unsolicited at every vblank start */
		*mars_comm12 = ++timer;
		if (smoke_on) smoke_wave();
	}
}
