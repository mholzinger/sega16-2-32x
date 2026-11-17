#ifndef FRONTEND_H
#define FRONTEND_H
/* One mailbox, explicit ownership: MD requests, slave quiesces, master grants,
 * MD returns settings, master applies them and releases both processors.
 * COMM6 is a boot heartbeat, then title eligibility or slave-idle acknowledgement.
 * COMM12 is otherwise idle in the shipping configuration. */
#define FE_REQUEST 0xFE00u
#define FE_GRANTED 0xFE01u
#define FE_SLAVE_IDLE 0xFE02u
#define FE_RESULT 0xFD00u
#define FE_TITLE 0xB601u
/* THE RESULT IS THREE WORDS (2026-10-30, the DIP sheet): FE_RESULT | round | region << 3,
 * then FE_DSW1 | DSW1 and FE_DSW2 | DSW2, each posted after the master answers FE_MORE1 /
 * FE_MORE2. One mailbox, strictly alternating, so neither side can read a half-written set. */
/* THE LAUNCHER OPENS THE SHEET (LAUNCHER, 2026-11-13): the primary posts FE_SHEET | region
 * instead of answering a request with FE_GRANTED. The screen then runs as the power-on menu
 * with the version row fixed to that region, and answers with the same three words. */
#define FE_SHEET 0xFE10u
/* THE LAUNCHER'S SMOKE (launcher_smoke.c): FE_SMOKE | 0 off, | 1 upload and start,
 * | 2..10 brightness 0..8. Cleared by the 68K when done. LAUNCHER_SMOKE_RAM is where the
 * launcher's 68K program puts its per-vblank part (introlab/md/smoke68k.c names it too). */
#define FE_SMOKE 0xFE20u
#define LAUNCHER_SMOKE_RAM 0x00FF4980uL
#define FE_MORE1 0xFE03u
#define FE_MORE2 0xFE04u
#define FE_DSW1 0xFB00u
#define FE_DSW2 0xFA00u
#define FE_WAITING(v) (((v) & 0xFF00u) == 0xFE00u || ((v) & 0xFF00u) == FE_RESULT \
                       || ((v) & 0xFF00u) == FE_DSW1 || ((v) & 0xFF00u) == FE_DSW2)
#ifndef __ASSEMBLER__
void frontend_boot_check(void);
int frontend_input(unsigned short pad);
void frontend_service(void);
void frontend_menu_screen(void);
extern volatile unsigned short frontend_at_boot;   /* 1: the menu was opened by holding B at power-on; 2: by the launcher */
extern volatile unsigned short frontend_launch_region;   /* at_boot 2: the region the launcher shows */
#endif
#endif
