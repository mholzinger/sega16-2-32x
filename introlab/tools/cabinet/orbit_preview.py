#!/usr/bin/env python3
"""Host preview of the packed cabinet, drawn within the limits the SH-2 renderer will have:
320x224, 8-bit indexed, convex polygons, back-face cull, NO z-buffer (fixed group order,
depth sort inside a group), flat shading per face, texture coordinates perspective-
corrected at the two ends of each span (two divides per scanline) and linear between.

    orbit_preview.py <title config.json>   -> <art>/preview.gif, preview_sheet.png
    orbit_preview.py <title config.json> --still <yaw deg> <pitch deg> <dist inches>
                                           -> <art>/still.png, the rom's camera

It is a picture of the data and the method, not a measurement: it says nothing about speed."""
import math
import os
import sys

import numpy as np
from PIL import Image

import cabmesh
import common

W, H = 320, 224


def render(mesh, cam, yaw, bob):
    nshade = len(mesh["shades"])
    upi = mesh["units_per_inch"]
    fb = np.full((H, W), mesh["background"], dtype=np.uint8)
    eye = np.array([math.cos(yaw) * cam["distance"], math.sin(yaw) * cam["distance"], cam["height"] + bob])
    fwd = -eye / np.linalg.norm(eye)
    right = np.cross(fwd, [0, 0, 1.0])
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    basis = np.stack([right, up, fwd])
    light = np.array([-0.45, 0.50, -0.74])           # camera space: upper left, behind the viewer
    vc = (np.array(mesh["verts"], dtype=np.float64) / upi - eye) @ basis.T
    near = "R" if eye[1] > 0 else "L"
    far = "L" if near == "R" else "R"
    rank = {far + "edge": 0, far + "in": 1, far + "out": 1, "body": 2, near + "edge": 3, near + "in": 4, near + "out": 4}
    order = [rank[g] for g in cabmesh.GROUPS]
    dl = []
    for f in mesh["faces"]:
        c = vc[f["vi"]]
        n = np.cross(c[1] - c[0], c[2] - c[0])
        ln = np.linalg.norm(n)
        if ln == 0 or n @ c[0] >= 0:
            continue                                  # back-facing
        lit = max(0.0, min(1.0, 0.45 + 0.6 * float((n / ln) @ light)))
        dl.append((order[f["group"]], -c[:, 2].mean(), f, c, min(nshade - 1, int(lit * nshade))))
    dl.sort(key=lambda d: (d[0], d[1]))
    written = 0
    for _, _, f, c, shade in dl:
        pts = [(W / 2 + cam["focal"] * p[0] / p[2] * W / 2, H / 2 - cam["focal"] * p[1] / p[2] * W / 2, 1.0 / p[2]) for p in c]
        t = mesh["textures"][f["tex"]] if f["tex"] >= 0 else None
        uvs = f["uv"] if t is not None else [(0, 0)] * len(pts)
        n = len(pts)
        ya = max(0, int(math.ceil(min(p[1] for p in pts) - 0.5)))
        yb = min(H - 1, int(math.floor(max(p[1] for p in pts) - 0.5)))
        for yy in range(ya, yb + 1):
            yc, lo, hi = yy + 0.5, None, None
            for i in range(n):
                a, b, ua, ub = pts[i], pts[(i + 1) % n], uvs[i], uvs[(i + 1) % n]
                if a[1] == b[1]:
                    continue
                if a[1] > b[1]:
                    a, b, ua, ub = b, a, ub, ua
                if not (a[1] <= yc < b[1]):
                    continue
                k = (yc - a[1]) / (b[1] - a[1])
                w = a[2] + (b[2] - a[2]) * k
                e = (a[0] + (b[0] - a[0]) * k,
                     (ua[0] * a[2] + (ub[0] * b[2] - ua[0] * a[2]) * k) / w,
                     (ua[1] * a[2] + (ub[1] * b[2] - ua[1] * a[2]) * k) / w)
                if lo is None or e[0] < lo[0]:
                    lo = e
                if hi is None or e[0] > hi[0]:
                    hi = e
            if lo is None:
                continue
            xa, xb = max(0, int(math.ceil(lo[0] - 0.5))), min(W - 1, int(math.floor(hi[0] - 0.5)))
            if xb < xa:
                continue
            written += xb - xa + 1
            if t is None:
                fb[yy, xa:xb + 1] = f["base"] * nshade + shade
            else:
                k = (np.arange(xa, xb + 1) + 0.5 - lo[0]) / max(1e-6, hi[0] - lo[0])
                th, tw = t.shape
                u = np.clip((lo[1] + (hi[1] - lo[1]) * k).astype(int), 0, tw - 1)
                v = np.clip((lo[2] + (hi[2] - lo[2]) * k).astype(int), 0, th - 1)
                fb[yy, xa:xb + 1] = t[v, u].astype(np.uint16) * nshade + shade
    return fb, len(dl), written


def main():
    cfg = common.load_config(sys.argv[1])
    mesh = cabmesh.build(cfg)
    cam = cfg["preview"]
    if "--still" in sys.argv:                 # one frame from the rom's spherical camera
        i = sys.argv.index("--still")
        yaw, pitch, dist = (float(v) for v in sys.argv[i + 1:i + 4])
        still = dict(cam, distance=dist * math.cos(math.radians(pitch)), height=dist * math.sin(math.radians(pitch)))
        fb, nd, wr = render(mesh, still, math.radians(yaw), 0.0)
        path = os.path.join(cfg["_art"], "still.png")
        Image.fromarray(mesh["cram"][fb], "RGB").save(path)
        print(f"still {cfg['title']}: yaw {yaw} pitch {pitch} dist {dist}: {nd} faces, {wr} pixels -> {path}")
        return
    nf = cam["frames"]
    frames, drawn, written = [], [], []
    for k in range(nf):
        ph = 2 * math.pi * k / nf
        fb, nd, wr = render(mesh, cam, math.radians(cam["swing_deg"]) * math.sin(ph), cam["bob"] * math.cos(ph))
        drawn.append(nd)
        written.append(wr)
        frames.append(Image.fromarray(mesh["cram"][fb], "RGB"))
    art = cfg["_art"]
    frames[0].save(os.path.join(art, "preview.gif"), save_all=True, append_images=frames[1:], duration=50, loop=0)
    sheet = Image.new("RGB", (4 * W + 40, 2 * H + 24), (40, 40, 40))
    for i in range(8):
        sheet.paste(frames[(i * nf) // 8], (8 + (i % 4) * (W + 8), 8 + (i // 4) * (H + 8)))
    sheet.save(os.path.join(art, "preview_sheet.png"))
    print(f"preview {cfg['title']}: faces drawn per frame {min(drawn)}..{max(drawn)}, "
          f"pixels written per frame mean {sum(written) // nf} max {max(written)} of {W * H}")
    print(f"  {os.path.join(art, 'preview.gif')}")
    print(f"  {os.path.join(art, 'preview_sheet.png')}")


if __name__ == "__main__":
    main()
