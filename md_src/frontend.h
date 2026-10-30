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
extern volatile unsigned short frontend_at_boot;   /* the menu was opened by holding B at power-on */
#endif
#endif
