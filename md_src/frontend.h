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
#define FE_WAITING(v) (((v) & 0xFF00u) == 0xFE00u || ((v) & 0xFF00u) == FE_RESULT)
#ifndef __ASSEMBLER__
void frontend_boot_check(void);
int frontend_input(unsigned short pad);
void frontend_service(void);
void frontend_menu_screen(void);
#endif
#endif
