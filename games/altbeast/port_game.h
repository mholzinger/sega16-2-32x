#ifndef PORT_GAME_H
#define PORT_GAME_H
/* ALTERED BEAST: what the port runtime (sh_src/port_l2.c) has to know about this title's
 * program that is a rule and not an address. Addresses are in game.py (game_cfg.h). A second
 * title supplies its own games/<title>/port_game.h with the same names. */

/* The program's cold-boot entry: it falls into the main loop and never returns. */
#define GAME_BOOT_ENTRY()  FUN_0000040e()

/* START AS COIN: a start costs 2 credits (docs/proof/altbeast.c:155 `credits < 2` refuses,
 * :160 `credits -= 2`), so START inserts a coin while the credits cannot pay for one. */
#define GAME_START_NEEDS_COIN(credits)  ((credits) < 2u)

/* THE BLINK CLOCK AS A FRAME NUMBER. n is the phase, b the down-counter inside it; the
 * phases alternate 30 and 60 frames, 90 a pair, and the clock wraps after 256 phases. */
#define GAME_BLINK_FRAME(n, b)  (((n) >> 1) * 90u + ((n) & 1u) * 30u + (((n) & 1u) ? 60u : 30u) - 1u - (b))
#define GAME_BLINK_WRAP         11520u
#endif
