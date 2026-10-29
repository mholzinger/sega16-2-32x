#!/usr/bin/env python3
"""THE ONE DEFINITION OF ares' ACTIVE PICTURE AREA.

A raw ares-headless screenshot is 1415x243: the emulated picture inside a
border. The picture is rows 11..234 and columns 65..1344 -- exactly 224
lines by 1280 pixels.

MEASURED, twice, independently:
  2026-09-25  tools/rig_barcode.py:44, "19 was 8 lines low"
  2026-10-11  penfix1.32x at frames 700 and 2000, every column scanned:
              lit rows 11..234 (224), lit cols 65..1344 (1280), and NO
              fully-black row anywhere inside that span on either frame.

WHY THIS FILE EXISTS. The 2026-09-25 correction was applied to three
tools and not to the other three, which kept cropping at y=19 -- eight
lines low, so their 224-row window started eight lines into the picture
and ended with eight lines of BORDER. That border reads as a solid black
band at screen rows 216-223, and on 2026-10-11 it was filed as a rom
defect and chased through three innocent code sites (BG_BOTTOM_HOLES,
the rows 24-27 cover test, FBCLEAR) before the crop was checked. A
sampled scan hid it: stepping every 7th column missed the lit pixels in
rows 11..18 and "measured" the span as 19..234, which is 216 rows and is
exactly the 216-of-224 the bad crop produces.

Use crop_active(); do not re-type the numbers.
"""
X0, Y0, W, H = 65, 11, 1280, 224
BOX = (X0, Y0, X0 + W, Y0 + H)
RAW = (1415, 243)


def crop_active(im, size=(320, 224), resample=None):
    """Raw ares screenshot -> the active picture, scaled to `size`.

    Anything that is not the raw 1415x243 frame is passed through (already
    cropped corpora, 320x224 captures) and only resized if it must be.
    """
    from PIL import Image
    if resample is None:
        resample = Image.NEAREST
    if im.size == RAW:
        im = im.crop(BOX)
    if im.size != size:
        im = im.resize(size, resample)
    return im
