/* The launcher's carpet (carpet.c): our own pattern, from numbers. */
#ifndef CARPET_H
#define CARPET_H
#define CARPET_TOP 156               /* the floor's first row: where the wall's picture ends */
void carpet_row(int y);              /* choose a screen row, CARPET_TOP..223 */
int carpet_pixel(int x);             /* that row's colour at x: 0 navy, 1-5 red..lilac */
#endif
