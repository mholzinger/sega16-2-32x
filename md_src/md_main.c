#include "common.h"
#include "../sh_src/r60/r60_tile_batches.h"
volatile uint32_t r60_tb_rx[6]; /* group DMAs, group tiles, largest group, malformed records, zero hits, zero clears */
static uint8_t r60_tb_blank_resident;
void r60_tile_vram_reset(void) { r60_tb_blank_resident=0; }
static uint16_t r60_md_progress;
static uint8_t r60_md_consuming;
/* COMM4 has no runtime writer in R60 after the master releases startup.
 * Only completed work while the shim owns the packet may renew its lease. */
static inline void r60_md_completed(void)
{
    if (!r60_md_consuming) return;
    *(volatile uint16_t *)MARS_COMM4 = ++r60_md_progress;
}
#if defined(R60) && defined(NT_WRAP) && defined(PAL_FIRST)
/* A's fat consume can put B's palette-first DMA in the visible picture. Keep
 * all four lines in allocated WRAM until a safe blank, rather than painting
 * CRAM DAC dots. The legacy fixed hold at FFA100 has room for only 48 words. */
static volatile uint16_t r60_palette_hold[64], r60_palette_pending;

#endif
/* the odd-parity direct hold packet's 68K address (sh_src/r60/r60_main.c R60_PKT_A_ALT): off the
 * 32X canvas under R60PKTODDHI, where it no longer leaves its words on lines 222-223 */
#define R60_PKTODD_68K 0x851E00uL
#include "tile_thunks.h"
#include "wram_code.h"
#include "pal_thunks.h"
#include "packet_fmt.h"
/* PAL32 (LOOP 22) and the generated thunks must agree on the dirty-
 * state layout — a mismatch reads garbage as dirt or misses all of it.
 * game_body/pal_thunks.h regenerate on flag flips via FLAGSTAMP; this
 * catches a stale header from a partial build. */
#if !PAL_THUNKS_PAL32
#error pal_thunks.h generated without PAL32 (stale header - rebuild)
#endif
#include "fmgate_tab.h"
#include "game_irq.h"      /* generated: GAME_IRQ4, GAME_ABSW_JMP_TARGET, GAME_MCU_* */
#include "../sh_src/game_cfg.h"   /* generated: GAME_WRAM_* (tools/emit_game_cfg.py) */
#ifdef MDSPR
#include "md_sprart_info.h"   /* generated: tools/bake_mdspr.py */
#endif
#if defined(FM_GATE) && !FMGATE_ON
#error fmgate_tab.h generated without FMGATE (stale header - rebuild)
#endif
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
static uint8_t  fbx_echo;                /* master's last lifted sequence (packet word 5 >> 12) */
static uint8_t  fbx_age;                 /* vints since the last blast(1) */
static uint8_t  fbx_rb_age;              /* vints since the last re-blast */
static uint16_t fbx_keep_n;              /* words of the last blasted packet, kept for a re-blast */
static uint8_t  fbx_keep_live;           /* the kept packet is whole (not being rebuilt) */
static uint16_t fbx_reblasts;            /* diag: re-blasts issued */
static uint16_t fbx_stage[];             /* tentative: the packet stage (defined with FBX_STAGE) */
static uint8_t  fbx_seq_pub;             /* tentative: last published sequence */
const uint32_t fmgate_spans[] = FMGATE_SPANS;

// MD-side shim for the arcade game. Runs entirely from work RAM (.data).
//
// RV STAYS 0 FOREVER. The authority is the UNPAIR MODEL block below (search
// "RV stays 0 FOREVER", NOTES.md "REBASE DESIGN v2"): the game executes its
// REBASED copy through the banked 0x900000 cart window, which is the standard
// commercial-32X memory model and lets the SH-2s touch cart ROM at any time.
// MEASURED 2026-09-27, TESTCART T25 reads the DREQ control register back as
// 0x0000 on ares and BlastEm, and no code in this tree writes bit 0 of
// 0xA15107 at all -- every write is 0 (68S off) or 4 (68S on).
//
// SO EVERY "RV=1" COMMENT IN THIS FILE IS FROM THE SUPERSEDED DESIGN. They are
// left where they explain why something is shaped the way it is, each tagged
// [RV=1: SUPERSEDED]. Do not act on one: 32X Technical Bulletin #9 forbids
// taking any 68000 interrupt while RV=1, and this port's whole heartbeat is
// IRQ4 -- setting RV would break it, which is precisely why v2 dropped it.
//
// Stage B: replicate the i8751 MCU (see NOTES.md "MCU FULLY REVERSE-
// ENGINEERED") and run the game's own boot from its RAM copy at 0xFFB400.

static volatile uint16_t* const mars_comm0  = (uint16_t*) MARS_COMM0;
static volatile uint16_t* const mars_comm2  = (uint16_t*) MARS_COMM2;
static volatile uint16_t* const mars_comm6  = (uint16_t*) MARS_COMM6;
static volatile uint16_t* const mars_comm8  = (uint16_t*) MARS_COMM8;
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
#define TP_ECHO_OK 0xF102
#define TP_ECHO_NO 0xF1FF
#define ECHO_SEEN(c) ((c) == TP_ECHO_OK || (c) == TP_ECHO_NO)
// MCU mailboxes are per-game (game_irq.h: US 0xFFF0C0/C2/C4, JP
// 0xFFF0D2/D0/D4 — the 68K program reads only its own MCU's addresses)
#define MCU_COINS   (*(volatile uint8_t*)GAME_MCU_COINS)  // MCU posts inverted SERVICE
#define MCU_BANKREQ (*(volatile uint8_t*)GAME_WRAM_TILE_BANKREQ)  // game's tile bank request
#define MCU_SNDCMD  (*(volatile uint8_t*)GAME_MCU_SND)    // sound mailbox (0xFF = idle)
#define MCU_BUSY    (*(volatile uint8_t*)GAME_MCU_BUSY)   // screen-sync handshake
#define TEXT_SYNC   (*(volatile uint8_t*)0xFF8002)  // text RAM shadow +2

uint16_t game_running = 0;

static volatile uint16_t* const vdp_data_port = (uint16_t*) VDP_DATA_PORT;
static volatile uint32_t* const vdp_ctrl_wide = (uint32_t*) VDP_CTRL_PORT;
#include "md_palette.inc"

/* BLANK MODE (2026-09-06): the SH-2 publishes bit 13 of packet word 1
 * while its display gate holds; the MD display stays OFF for as long
 * as the game blanks OR the SH-2 holds, so the vint consume DMA keeps
 * the blank-rate VDP access (the reverted batch-40 ran at the active
 * rate because the hold left the MD display ON). 0 = no packet seen
 * this vint (keep), 1 = seen without hold, 2 = seen with hold. */
static uint8_t md_hold_seen, md_hold;
/* RAMCODE (2026-09-06): the consume ran from cart ROM under the master's
 * compose traffic (nm: 0x8c0b48) — a 4-6x fetch-stall on every
 * instruction; consume B measured 14 lines for a 280-word chunk. */
#include "md_slim.inc"
#include "md_consume.inc"
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
#endif
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
	(void)bump;
	fbx_stage_n = 0;
	{
		const uint32_t *sp = (const uint32_t*)fbx_stage;
		volatile uint32_t *dp = (volatile uint32_t*)FBX_PKT_MD;
		uint16_t nl = (uint16_t)(n >> 1);
		while (nl--) *dp++ = *sp++;
		if (n & 1)
			((volatile uint16_t*)FBX_PKT_MD)[n - 1] = fbx_stage[n - 1];
	}
	if (bump) { fbx_keep_n = n; fbx_keep_live = 1; fbx_age = 0; fbx_rb_age = 0; }
	{
		volatile uint16_t *pub = (volatile uint16_t*)FBX_PUB_MD;
		pub[1] = n;                      /* exact word count */
		if (bump) fbx_seq_pub++;
		pub[0] = (uint16_t)(FBX_MAGIC | fbx_seq_pub);
	}
	*(volatile uint16_t*)0xFFA186 =
		*(volatile uint16_t*)0xC00008;   /* V after the blast */
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
__attribute__((section(".data")))
void main(void) {
	*(volatile uint16_t*)0xFFA1D0 = 0;   /* the game-frame counter (shim_vblank); WRAM boots as 0xFFFF */
	// BOOT-PHASE TRACER: Genesis backdrop colour is visible in ares no matter
	// what the 32X side does (stage-A red flash proved it). BGR:
	// RED=entered main; YELLOW=master SDRAM signal; CYAN=slave alive;
	// GREEN=boot complete [RV=1: SUPERSEDED, nothing sets RV]; game owns the screen.
	vdp_color(0, 0x00E);                // RED: main entered

	// Wait for BOTH SH-2s to reach their SDRAM-resident code. [RV=1:
	// SUPERSEDED — this wait was written to precede an RV=1 that v2 removed;
	// it still earns its place, because the handshake it waits on is what
	// proves both SH-2s are alive before the game takes the screen.] With RV
	// set the SH-2s must never touch cart ROM (hardware rule, enforced by
	// ares; violating it kills their instruction fetch). The
	// master posts 0x600D on COMM14 from SDRAM; the slave's SDRAM loop
	// increments COMM6.
#if   defined(CART_SSF) && defined(R60_SOUND)
	{ extern void cart_bank_set(unsigned slot, unsigned page); cart_bank_set(7, 8); }   /* the music blob's page into slot 7, for good (md_sound.c; the SH-2s are in SDRAM here) */
#endif
	while (*mars_comm14 != 0x600D) ;
	vdp_color(0, 0x0EE);                // YELLOW: master is SDRAM-resident
#define MDSTAGE(col) do { } while (0)
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

#ifdef TEST_CART
	/* TEST CARTRIDGE (md_src/testcart.c): boot is complete here -- both SH-2s
	 * up, RV set, the screen ours -- which is the same slot BOOT_BANK3HALT
	 * uses. Never returns. */
	{ extern void testcart_run(void); testcart_run(); }
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
		volatile uint32_t *pm = (volatile uint32_t*)GAME_WRAM_PALETTE;
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
	// ends at 0xFFBD1A. The boot stack USED to start at 0xFFBFF0 in this
	// same page, 184 bytes above the FM-gate table's end, and the boot-time
	// vint handler overwrote the table's tail whenever its frame grew by
	// 32 bytes (2026-09-19, MAME watchpoint). It is at 0xFF3FF0 now
	// (md_start.s); nothing else may be placed in this page's tail.
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
	// the generator asserts it stays under 0xBFF0, the page's end).
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
	*(volatile uint32_t*)0xFFB0D0 = 0;   // LOOP15: sum of total spans
	                                     // (ALWAYS-ON handler-mean meter)
	*(volatile uint32_t*)0xFFA040 = 0;   //   sum of window/ack-wait spans

	// I/O mailboxes: idle inputs, DIP defaults (DSW2 0xFD = 3 lives, normal,
	// demo sounds on; DSW1 0xFF = 1 coin / 1 credit)
	IO_MISC = 0; IO_SERVICE = 0xFF; IO_P1 = 0xFF; IO_P2 = 0xFF;
	IO_DSW2 = 0xFD; IO_DSW1 = 0xFF; IO_C43007 = 0;
	BANK_SHADOW = 0;

	// MCU-owned state
	MCU_COINS = 0; MCU_SNDCMD = 0xFF; MCU_BUSY = 0;
	// (MCU also sends sound cmd 0x40 at boot — no Z80 yet, noted)

	// [RV=1: SUPERSEDED. The parenthetical below is from the old design and
	// is now BACKWARDS: RV stays 0, so 0x0-0x3FF is the ADAPTER's space, not
	// the cart image -- which is exactly why the H-int vector trick in the
	// next paragraph works at all. That trick is the operative mechanism;
	// believe it, not this line.]
	// Vectors are served from the cart ROM table (RV=1 makes 0x0-0x3FF the
	// cart image): exceptions -> 0xFFB408 rte stub, VBLANK -> _vblank. No
	// runtime vector writes are possible (cart is read-only under RV=1).

	// Interrupt delivery, and it needs RV=0: the adapter's H-int vector
	// (0x70) is WRITABLE RAM (the security blob itself writes it). Point it
	// straight at our RAM-resident handler — no adapter trampoline, no
	// 0x880000-window fetch. H-int is 68K level 4 == the arcade's IRQ4.
	{
		extern void _vblank(void);
		*(volatile uint32_t*)0x000070 = (uint32_t)&_vblank;
	}

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
		/* fold 4: the vint handler may already have taken COMM14 for
		 * the state word (tag E) -- it only does so after seeing the
		 * master's B1xx/B008, so E means armed too (vi87 cuts 1-2: this
		 * loop spun on E for the whole run, screen black) */
		while ((*mars_comm14 & 0xFE00) != 0xB000
		       && (*mars_comm14 & 0xF000) != 0xE000 && --hold) ;
		__asm__ __volatile__("move.w #0x2700,%%sr" ::: "memory");
	}
	MDSTAGE(0x3000);                    /* DARK BLUE: hold over, entering the game */

	/* THERE IS NO GAME TO ENTER. The SH-2 port is the producer.
	 *
	 * .gamehigh is absent under NO_GAMEHIGH, so the jsr below went to an address
	 * that holds nothing: the 68K left for garbage with SR = 0x2700, interrupts
	 * masked, and never came back. shim_vblank is the vblank handler
	 * (md_start.s:398) and it is what posts the master's per-frame announce, so
	 * with no vints the announce never fired and the whole renderer stayed dark.
	 *
	 * MEASURED 2026-10-03: 4 announces in 400 frames against the line's 726; 32X
	 * CRAM entirely zero against the line's 442/512 bytes. Every port-alone build
	 * before this one was black for this reason and not for anything to do with the
	 * port.
	 *
	 * So: unmask and idle. shim_vblank runs every vblank exactly as it does for the
	 * game, the master gets its announce, and the port -- which lives on the SH-2
	 * and needs nothing from the 68K -- drives the picture. The 68K's only remaining
	 * job on this build is to be the thing the vblank interrupt happens to. */
	/* AND ENABLE THE VBLANK INTERRUPT, which the GAME normally turns on.
	 * The renderer's own write is `disp ? 0x8154 : 0x8114` (md_main.c) and 0x54
	 * has bit 5 CLEAR -- that bit is IE0, the VDP's vblank interrupt enable, and
	 * md_main has never set it because the game's boot does ("the game's boot
	 * expects reset state and enables IRQ4 itself", the handoff comment above).
	 * With no game it stayed off, so unmasking the 68K alone bought nothing: there
	 * were no vblank interrupts to unmask. 0x74 = display on + IE0 + DMA. */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8174;
	/* ONE VINT SOURCE, NOT TWO. The line's vint is the H-int at line 223 (reg 0
	 * IE1, reg 10 = 0xDF; level 4) with the VDP vblank interrupt (reg 1 IE0, level
	 * 6) OFF. The port build turned IE0 on (the display gate's 0x8114 stops the
	 * H-int with the display) but left IE1 on, so every frame ran the handler
	 * TWICE, the level-6 entry landing inside the level-4 one. MEASURED rd1
	 * 2026-10-04: 0xFFB0F0 counted 840 vints in 548 frames; the 68K stack held an
	 * exception frame SR=0x2404 PC=0xFF1C6E (the handler interrupted at IPL 4);
	 * the handler stopped at frame 548, every VRAM tile slot read zero after it, and
	 * slim_rec filled with garbage pairs from two consumes walking one packet.
	 * IE1 off + IE0 on from here: the level-6 vint is the only entry. */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8004;     /* reg 0: IE1 OFF (H-int never fires) */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8174;     /* reg 1: display on, IE0 ON, DMA on */
	/* THE PLANE REGISTERS, WHICH NOTHING HAS EVER SET ON THIS BUILD. The GAME's boot
	 * programs the VDP's layout registers, and under NO_GAMEHIGH there is no game: this
	 * shim sets regs 0, 1, 11 and 15 and no others, so the plane bases and the plane
	 * size have been whatever reset left them. Plane B's nametable base read 0 -- the
	 * tile area -- which is why the MD layer showed nothing however correct the cells
	 * and the art in VRAM were (measured: 6,640 non-zero bytes of tile data and 784
	 * name-table entries at 0xE000, and a black layer).
	 * The addresses are the ones md_consume already writes to (md_main.c:2342-2412). */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8230;     /* reg 2: plane A base 0xC000 */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8407;     /* reg 4: plane B base 0xE000 */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8578;     /* reg 5: sprite table 0xF000 */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8D3F;     /* reg 13: hscroll table 0xFC00 */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x8700;     /* reg 7: backdrop = colour 0 */
	*(volatile uint16_t*)VDP_CTRL_PORT = 0x9001;     /* reg 16: plane 64 x 32 cells */
	__asm__ __volatile__("move.w #0x2000,%%sr" ::: "memory");   /* IPL 0: vints on */
#ifdef R60_IDLECONSUME
	{ extern void r60_idle_consume(void);
      extern void frontend_service(void);
      for (;;) {
          frontend_service();
          { extern void cart_bank_service(void); cart_bank_service(); }   /* md_bank.c: COMM10 0x5Bxx = page a slot */
          r60_idle_consume();
      } }   /* sw192: the span's packets taken as they land, under the hold */
#endif
	for (;;)
		__asm__ __volatile__("nop");
}

#include "r60_shim.h"
