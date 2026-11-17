#!/usr/bin/env python3
"""Host preview of the intro doors: two panels hinged at the screen's edges, the SEGA logo
lying across both as flat polygons, swinging inward onto a dark room.

    doors_preview.py   -> art/logo/doors_preview.png  (needs art/logo/logo.json)

Drawn the way the SH-2 will draw it: 320x224, flat convex polygons only, no z-buffer. A
door is drawn as its panel, then its frame line, then its share of the logo, in that fixed
order; the two doors never overlap. Rows are the two colour schemes, columns the swing."""
import json
import math
import os

from PIL import Image, ImageDraw

LAB = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(LAB, "art", "logo")
W, H = 320, 224
FOCAL_PX = 408                 # the cabinet scene's lens
DIST = 2.43                    # camera to the closed doors, in door half-widths: they just overfill the screen
DOOR_H = 1.45                  # door height in the same units
LOGO_W = 1.50                  # logo width across both doors
LOGO_Y = 0.06                  # logo centre above the middle
FRAME_IN, FRAME_T = 0.07, 0.014
SCHEMES = {
    "white doors, blue logo": dict(panel=(232, 233, 240), frame=(178, 182, 198), under=(250, 250, 255),
                                   top=(40, 110, 230), bottom=(10, 30, 140), room=(4, 4, 10)),
    "dark doors, lit logo":   dict(panel=(26, 28, 40), frame=(58, 62, 84), under=(236, 244, 255),
                                   top=(96, 200, 255), bottom=(16, 48, 200), room=(4, 4, 10)),
}
CHOSEN = "dark doors, lit logo"      # Mike, 2026-11-13; pack_doors.py packs this one
BANDS = 6                      # the logo's fill is a vertical ramp in this many steps
ANGLES = (0, 25, 50, 78)


def clip_x(poly, side):
    """The part of a convex polygon with x <= 0 (side -1) or x >= 0 (side +1)."""
    out = []
    for i, a in enumerate(poly):
        b = poly[(i + 1) % len(poly)]
        ina, inb = a[0] * side >= 0, b[0] * side >= 0
        if ina:
            out.append(a)
        if ina != inb:
            t = a[0] / (a[0] - b[0])
            out.append((0.0, a[1] + (b[1] - a[1]) * t))
    return out


def fan(poly):
    """Split a convex polygon into pieces of at most four corners, as the rom stores them."""
    if len(poly) <= 4:
        return [poly]
    return [poly[:4]] + fan([poly[0]] + poly[3:])


def door_polys(side, logo):
    """One door's flat polygons in door-pair coordinates (x -1..1, y up), back to front."""
    x0, x1 = (-1.0, 0.0) if side < 0 else (0.0, 1.0)
    h = DOOR_H / 2
    polys = [("panel", [(x0, h), (x1, h), (x1, -h), (x0, -h)])]
    a, b, c, d = x0 + FRAME_IN, x1 - FRAME_IN, h - FRAME_IN, -h + FRAME_IN
    t = FRAME_T
    for q in ([(a, c), (b, c), (b, c - t), (a, c - t)], [(a, d + t), (b, d + t), (b, d), (a, d)],
              [(a, c), (a + t, c), (a + t, d), (a, d)], [(b - t, c), (b, c), (b, d), (b - t, d)]):
        polys.append(("frame", q))
    lh = LOGO_W * logo["aspect"]
    lx = lambda v: (v - 0.5) * LOGO_W
    ly = lambda v: LOGO_Y + lh / 2 - v * LOGO_W

    def add(kind, ya, yb, xla, xra, xlb, xrb):
        part = clip_x([(lx(xla), ly(ya)), (lx(xra), ly(ya)), (lx(xrb), ly(yb)), (lx(xlb), ly(yb))], side)
        if len(part) >= 3:
            polys.extend((kind, p) for p in fan(part))
    for t in logo["underlay"]:                     # pale, under the letters: outline, hairline, gaps
        add("under", *t)
    step = logo["aspect"] / BANDS
    for ya, yb, xla, xra, xlb, xrb in logo["trapezoids"]:
        # cut at the band boundaries so each piece is one colour of the ramp
        cuts = [ya] + [k * step for k in range(1, BANDS) if ya < k * step < yb] + [yb]
        at = lambda y, a, b: a + (b - a) * (y - ya) / (yb - ya)
        for c0, c1 in zip(cuts, cuts[1:]):
            band = min(BANDS - 1, int((c0 + c1) / 2 / step))
            add(f"band{band}", c0, c1, at(c0, xla, xlb), at(c0, xra, xrb), at(c1, xla, xlb), at(c1, xra, xrb))
    return polys


def colour_of(scheme, kind):
    if kind.startswith("band"):
        k = int(kind[4:]) / (BANDS - 1)
        return tuple(a + (b - a) * k for a, b in zip(scheme["top"], scheme["bottom"]))
    return scheme[kind]


def render(scheme, logo, angle_deg):
    img = Image.new("RGB", (W, H), scheme["room"])
    d = ImageDraw.Draw(img)
    th = math.radians(angle_deg)
    lit = 0.50 + 0.50 * math.cos(th)                      # the light is in front: a door dims as it turns away
    count = 0
    for side in (-1, 1):
        for kind, poly in door_polys(side, logo):
            pts = []
            for x, y in poly:
                r = (x + 1.0) if side < 0 else (1.0 - x)   # distance from this door's hinge
                xr = (-1.0 + r * math.cos(th)) if side < 0 else (1.0 - r * math.cos(th))
                z = DIST + r * math.sin(th)                # swinging inward, away from the camera
                pts.append((W / 2 + FOCAL_PX * xr / z, H / 2 - FOCAL_PX * y / z))
            d.polygon(pts, fill=tuple(int(c * lit) for c in colour_of(scheme, kind)))
            count += 1
    return img, count


def main():
    logo = json.load(open(os.path.join(OUT, "logo.json")))
    sheet = Image.new("RGB", (len(ANGLES) * (W + 8) + 8, len(SCHEMES) * (H + 8) + 8), (48, 48, 48))
    n = 0
    for row, (name, scheme) in enumerate(SCHEMES.items()):
        for col, ang in enumerate(ANGLES):
            img, n = render(scheme, logo, ang)
            sheet.paste(img, (8 + col * (W + 8), 8 + row * (H + 8)))
    path = os.path.join(OUT, "doors_preview.png")
    sheet.save(path)
    per_door = [len(door_polys(s, logo)) for s in (-1, 1)]
    print(f"doors: {per_door[0]} + {per_door[1]} = {n} flat polygons (logo {len(logo['trapezoids'])} trapezoids before the seam split)")
    print(f"  swing angles {ANGLES} degrees -> {path}")


if __name__ == "__main__":
    main()
