#ifndef INTROLAB_DOORS_H
#define INTROLAB_DOORS_H
#include <stdint.h>

/* The intro doors (art/logo/doors_data.h): two dark panels hinged at the screen's edges,
 * the SEGA logo across both as flat polygons, swinging inward onto a dark room.
 * `angle` is in ANGLE_STEPS per turn: 0 = closed, 256 = open edge-on and out of view. */

/* Draw one frame into `fb` (320x224 bytes). Primary SH-2; wakes the secondary for the lower half. */
void doors_draw(uint8_t *fb, int angle);

/* The doors' CRAM entries for this angle at level/256 of full brightness: a door dims as it
 * turns from the light. `shimmer` puts a highlight on the logo's fill at that band, in
 * 1/16ths of a band from the top (negative: none). Call in vblank, right after the flip
 * that shows the frame. */
void doors_light(int level, int angle, int shimmer);

/* A full pass of the highlight runs from 0 to this, in 1/16ths of a band. */
#define DOORS_SHIMMER_SPAN (6 * 16)

/* The secondary SH-2's share of a frame, run when the primary posts CMD_DOORS. */
void doors_secondary(void);

#endif
