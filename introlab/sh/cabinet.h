#ifndef INTROLAB_CABINET_H
#define INTROLAB_CABINET_H
#include <stdint.h>

/* The title's packed cabinet (art/<title>/cabinet_data.h), drawn live.
 * Angles are in ANGLE_STEPS per turn (1024). Yaw 0 looks at the cabinet head-on and
 * positive yaw moves the camera toward the side on the viewer's right; pitch raises the
 * camera. The camera always looks at the cabinet's centre from `dist` (in 1/64 inch). */

/* Once at start-up: copy the textures to SDRAM and build the room's tables. Touches no CRAM. */
void cabinet_init(void);

/* Set the room's lighting: the cabinet's palette and the wall and floor ramps at
 * level/256 of full brightness (0 = dark, 256 = lit). Call in vblank, right after a flip. */
void cabinet_light(int level);

/* Draw one frame into framebuffer page `page` (0 or 1) at `fb` (320x224 bytes). The
 * primary SH-2 calls this; it builds the frame's draw list, wakes the secondary to draw
 * the lower half, draws the upper half itself and returns when both are done. Only the
 * part of the page the cabinet covered last time, or covers now, is cleared.
 * `xc` is the screen column the cabinet's centre projects to (160 = middle) and `region`
 * picks which regional textures it wears (0..CAB_NREGION-1, see cabinet_data.h).
 * Returns the faces drawn. */
int cabinet_draw(uint8_t *fb, int page, int yaw, int pitch, int dist, int xc, int region);

/* Show the region's marquee as a flat picture with its top-left corner at (x, y), from the
 * next frame on; region < 0 removes it. It must stay above row 112. */
void cabinet_banner(int region, int x, int y);

/* Put the room's background (wall picture or floor) back on [x0,x1) x [y0,y1) of a page. */
void cabinet_restore(uint8_t *fb, int x0, int y0, int x1, int y1);

/* The secondary SH-2's share of a frame, run when the primary posts CMD_CABINET. */
void cabinet_secondary(void);

/* FRT ticks (5.56 us each) spent in the last frame: [0] geometry and sort, [1] primary
 * clear, [2] primary raster, [3] secondary clear + raster (as the primary waits for it:
 * total wall time from wake to done). */
extern volatile uint16_t cabinet_ticks[4];

#endif
