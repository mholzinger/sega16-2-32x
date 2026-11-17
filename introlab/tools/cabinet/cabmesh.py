"""Builds the low-poly cabinet the rom draws, from what extract_cab.py left in <art>/:
the mesh, its textures reduced to one shared palette, and the 32X CRAM image.

The packer and the host preview both take their data from build(), so the preview shows
exactly what is packed: same quantised vertices, same texel coordinates, same palette."""
import json
import os

import numpy as np
from PIL import Image

import common

UNITS_PER_INCH = 64                       # vertices are int16 in 1/64 inch about the cabinet centre
GROUPS = ("body", "Redge", "Rin", "Rout", "Ledge", "Lin", "Lout")


def ear_clip(poly):
    """Triangulate a simple counter-clockwise polygon; index triples."""
    idx, tris = list(range(len(poly))), []

    def cr(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    while len(idx) > 3:
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = poly[i0], poly[i1], poly[i2]
            if cr(a, b, c) <= 1e-9:
                continue
            if any(cr(a, b, poly[j]) >= 0 and cr(b, c, poly[j]) >= 0 and cr(c, a, poly[j]) >= 0
                   for j in idx if j not in (i0, i1, i2)):
                continue
            tris.append((i0, i1, i2))
            idx.pop(k)
            break
        else:
            raise SystemExit("cabmesh: the side outline did not triangulate")
    tris.append(tuple(idx))
    return tris


def build(cfg):
    art = cfg["_art"]
    cab = json.load(open(os.path.join(art, "cab.json")))
    col = {k: tuple(v) for k, v in cfg["colours"].items()}
    nbase, shades = cfg["palette"]["base"], cfg["palette"]["shades"]
    nshade = len(shades)
    if nbase * nshade > 256:
        raise SystemExit("cabmesh: base colours x shade levels exceeds the 256-entry CRAM")

    # ---- textures: composite on the panel colour, optional screen art, one palette ----
    def load(name):
        im = Image.open(os.path.join(art, "bake", name + ".png")).convert("RGBA")
        flat = Image.new("RGBA", im.size, col["panel"] + (255,))
        flat.alpha_composite(im)
        return flat.convert("RGB")
    gamma = cfg["palette"]["gamma"]

    def marquee(path, rep):
        """Marquee art as supplied, pre-darkened so the palette's lift (see `shown`) lands
        each colour back on the artist's own: a flat printed red stays red."""
        src = Image.open(common.resolve(path)).convert("RGB")
        if "crop" in rep:
            src = src.crop(tuple(rep["crop"]))
        a = np.asarray(src, dtype=np.float32) / 255.0
        return Image.fromarray((255.0 * a ** (1.0 / gamma) + 0.5).astype(np.uint8))
    names = [f["tex"] for f in cab["faces"]] + ["side_l", "side_r"]
    rgb = {n: load(n) for n in names}
    screen = cfg.get("screen")
    if screen:
        if screen["face"] not in rgb:
            raise SystemExit(f"cabmesh: screen face {screen['face']} is not a baked face ({names})")
        tex = rgb[screen["face"]]
        u0, v0, u1, v1 = screen["rect"]
        box = (round(u0 * tex.width), round(v0 * tex.height), round(u1 * tex.width), round(v1 * tex.height))
        art_im = Image.open(common.resolve(screen["image"])).convert("RGB")
        tex.paste(art_im.resize((box[2] - box[0], box[3] - box[1]), Image.LANCZOS), box[:2])
    # Regional variants: a region may replace part of a face's texture (its screen, its
    # marquee). Each replacement becomes an extra texture; faces keep their base index and
    # the rom swaps through a per-region remap table.
    regions = cfg.get("regions") or [{"name": "default"}]
    remap = []
    for reg in regions:
        table = {}
        for rep in reg.get("replace", []):
            if rep["face"] not in rgb:
                raise SystemExit(f"cabmesh: region {reg['name']} replaces unknown face {rep['face']}")
            name = f"{rep['face']}@{reg['name']}"
            tex = rgb[table.get(rep["face"], rep["face"])].copy()
            u0, v0, u1, v1 = rep["rect"]
            box = (round(u0 * tex.width), round(v0 * tex.height), round(u1 * tex.width), round(v1 * tex.height))
            if cfg.get("banner") and rep["face"] == cfg["banner"]["face"]:
                src = marquee(rep["image"], rep)
            else:
                src = Image.open(common.resolve(rep["image"])).convert("RGB")
                if "crop" in rep:
                    src = src.crop(tuple(rep["crop"]))
            tex.paste(src.resize((box[2] - box[0], box[3] - box[1]), Image.LANCZOS), box[:2])
            rgb[name] = tex
            table[rep["face"]] = name
        remap.append(table)
    base_names = list(names)
    names = list(rgb.keys())
    # The banner: the marquee again as a flat picture, one per region.
    banners_rgb = []
    if cfg.get("banner"):
        bw, bh = cfg["banner"]["size"]
        for reg in regions:
            im = load("banner")
            for rep in reg.get("replace", []):
                if rep["face"] == cfg["banner"]["face"] and rep["rect"] == [0, 0, 1, 1]:
                    im = marquee(rep["image"], rep).resize((bw, bh), Image.LANCZOS)
            banners_rgb.append(im)
    strip_w = max([128] + [b.width for b in banners_rgb])
    strip = Image.new("RGB", (strip_w, sum(i.height for i in rgb.values()) + sum(b.height for b in banners_rgb) + 8))
    y = 0
    for n in names:
        strip.paste(rgb[n], (0, y))
        y += rgb[n].height
    for bim in banners_rgb:
        strip.paste(bim, (0, y))
        y += bim.height
    for k, key in enumerate(("edge", "panel", "inner", "background")):
        strip.paste(col[key], (k * 32, y, k * 32 + 32, y + 8))
    pal_img = strip.quantize(colors=nbase, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    base_pal = np.array(pal_img.getpalette()[:nbase * 3], dtype=np.float32).reshape(nbase, 3)
    # The palette above is taken from the art at its baked size, so it does not move when
    # the stored textures are made narrower ("texture_width" in the title's json: a power of
    # two; on screen a face is about 60 pixels across, so 128 texels was twice what shows).
    tex_w = int(cfg.get("texture_width", 128))
    if tex_w != 128:
        rgb = {n: im.resize((im.width * tex_w // 128, im.height), Image.LANCZOS) for n, im in rgb.items()}
    textures = [np.array(rgb[n].quantize(palette=pal_img, dither=Image.Dither.NONE), dtype=np.uint8) for n in names]
    banners = [np.array(b.quantize(palette=pal_img, dither=Image.Dither.NONE), dtype=np.uint8) for b in banners_rgb]

    def nearest(c):
        return int(np.argmin(((base_pal - np.array(c, dtype=np.float32)) ** 2).sum(1)))
    i_edge, i_panel, i_inner, i_bg = (nearest(col[k]) for k in ("edge", "panel", "inner", "background"))
    shown = 255.0 * (base_pal / 255.0) ** gamma      # lift near-black panels off the backdrop
    cram = np.zeros((256, 3), dtype=np.uint8)
    for b in range(nbase):
        for s in range(nshade):
            cram[b * nshade + s] = np.clip(shown[b] * shades[s], 0, 255)

    # ---- mesh, in inches, wound for the renderer's left-handed camera space ----
    faces = []
    YL, YR, T = cab["yl"], cab["yr"], cab["t"]
    y0, y1 = YL + T, YR - T
    tix = {n: i for i, n in enumerate(names)}

    def add(v, group, tex=None, uv=None, base=i_panel):
        faces.append(dict(v=v, group=GROUPS.index(group), tex=-1 if tex is None else tix[tex], uv=uv, base=base))
    for f in cab["faces"]:                       # the front, one quad per profile segment
        (xa, za), (xb, zb) = f["p0"], f["p1"]
        add([(xb, y0, zb), (xb, y1, zb), (xa, y1, za), (xa, y0, za)], "body", f["tex"], [(0, 0), (1, 0), (1, 1), (0, 1)])
    sf, sb = cab["side_front"], cab["side_back"]
    outline = sf + sb[::-1]
    area = sum(outline[i][0] * outline[(i + 1) % len(outline)][1] - outline[(i + 1) % len(outline)][0] * outline[i][1]
               for i in range(len(outline)))
    if area < 0:
        outline = outline[::-1]
    bx, bz = sb[0]
    (tfx, tfz), (tbx, tbz) = sf[-1], sb[-1]
    add([(tbx, y0, tbz), (tbx, y1, tbz), (tfx, y1, tfz), (tfx, y0, tfz)], "body")          # top
    for i in range(len(sb) - 1, 0, -1):                                                    # back
        (xa, za), (xb, zb) = sb[i], sb[i - 1]
        add([(xa, y1, za), (xa, y0, za), (xb, y0, zb), (xb, y1, zb)], "body")
    add([(bx, y1, bz), (bx, y0, bz), (bx, y0, 0), (bx, y1, 0)], "body")                    # back of the plinth
    front = max(x for x, z in cab["centre"] if z < bz)
    for yy, flip in ((y1, True), (y0, False)):                                            # plinth side walls
        q = [(front, yy, 0), (bx, yy, 0), (bx, yy, bz), (front, yy, bz)]
        add(q[::-1] if flip else q, "body")
    S = cab["side"]
    tris = ear_clip(outline)
    for side, yo, yi in (("R", YR, YR - T), ("L", YL, YL + T)):
        def uv(p, side=side):
            u = (S["x1"] - p[0]) / (S["x1"] - S["x0"]) if side == "R" else (p[0] - S["x0"]) / (S["x1"] - S["x0"])
            return (u, (S["z1"] - p[1]) / (S["z1"] - S["z0"]))
        for t in tris:
            o = t if side == "R" else t[::-1]
            add([(outline[j][0], yo, outline[j][1]) for j in o], side + "out",
                "side_r" if side == "R" else "side_l", [uv(outline[j]) for j in o])
            add([(outline[j][0], yi, outline[j][1]) for j in o[::-1]], side + "in", base=i_inner)
        for i in range(len(outline)):                                                      # T-molding
            a, b = outline[i], outline[(i + 1) % len(outline)]
            q = [(a[0], yo, a[1]), (a[0], yi, a[1]), (b[0], yi, b[1]), (b[0], yo, b[1])]
            add(q if side == "R" else q[::-1], side + "edge", base=i_edge)

    # ---- quantise to what the rom stores ----
    origin = cab["origin"]
    verts, vix = [], {}
    for f in faces:
        f["vi"] = []
        for p in f["v"]:
            q = tuple(int(round((p[k] - origin[k]) * UNITS_PER_INCH)) for k in range(3))
            if any(abs(c) > 32767 for c in q):
                raise SystemExit("cabmesh: a vertex does not fit int16 at 1/64 inch")
            f["vi"].append(vix.setdefault(q, len(verts)))
            if vix[q] == len(verts):
                verts.append(q)
        if f["tex"] >= 0:
            th, tw = textures[f["tex"]].shape
            f["uv"] = [(int(round(u * tw)), int(round(v * th))) for u, v in f["uv"]]
        # Outward unit normal, 2.14. The renderer's camera space is left-handed, so the
        # outward side is MINUS the cross product of the first two edges.
        a_, b_, c_ = (np.array(verts[i], dtype=np.float64) for i in f["vi"][:3])
        nrm = -np.cross(b_ - a_, c_ - a_)
        f["n"] = [int(round(c)) for c in nrm / np.linalg.norm(nrm) * 16384]
        del f["v"]
    if tex_w & (tex_w - 1) or any(t.shape[1] != tex_w for t in textures):
        raise SystemExit("cabmesh: the renderer indexes texels with a fixed shift; every texture must be the same power-of-two width")
    if nshade != 4:
        raise SystemExit("cabmesh: the renderer's pixel writers assume 4 shade levels")
    region_tex = [[tix[table.get(n, n)] for n in base_names] for table in remap]
    return dict(title=cfg["title"], verts=verts, faces=faces, textures=textures, texture_names=names,
                regions=[r["name"] for r in regions], region_tex=region_tex, banners=banners,
                cram=cram, nbase=nbase, shades=shades, background=i_bg * nshade,
                units_per_inch=UNITS_PER_INCH)
