#!/usr/bin/env python3
"""Packs one title's cabinet into the C header the rom compiles in.

    pack.py <title config.json>    -> <art>/cabinet_data.h

Every title produces the same symbol names, so the rom's renderer is title-agnostic and a
build selects a title by which art directory is on its include path."""
import os
import sys

import numpy as np
from PIL import Image

import cabmesh
import common

FB_W = 320
BACKDROP_MAX = 18      # colours a backdrop may use


def cut_figure(sheet, seed):
    """The solid shape of the figure drawn at `seed` on a sprite sheet with a black ground:
    every pixel of it, including the black ones inside its outline. Returns a bool array."""
    lit = sheet.max(axis=2) > 24
    h, w = lit.shape
    seen = np.zeros_like(lit)
    near = [(abs(dy) + abs(dx), seed[1] + dy, seed[0] + dx) for dy in range(-12, 13) for dx in range(-12, 13)
            if 0 <= seed[1] + dy < h and 0 <= seed[0] + dx < w and lit[seed[1] + dy, seed[0] + dx]]
    todo = [min(near)[1:]] if near else []         # the seed may sit a few pixels off the figure
    while todo:                                    # the lit pixels joined to the seed, bridging 2-pixel gaps
        y, x = todo.pop()
        if not (0 <= y < h and 0 <= x < w) or seen[y, x] or not lit[y, x]:
            continue
        seen[y, x] = True
        todo += [(y + dy, x + dx) for dy in range(-2, 3) for dx in range(-2, 3) if dy or dx]
    if not seen.any():
        sys.exit(f"pack: no figure at {seed} on the sprite sheet")
    ys, xs = np.nonzero(seen)
    body = seen[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    pad = np.pad(body, 1)
    outside = np.zeros_like(pad)
    todo = [(0, 0)]
    while todo:                                    # whatever the border cannot reach is inside
        y, x = todo.pop()
        if not (0 <= y < pad.shape[0] and 0 <= x < pad.shape[1]) or outside[y, x] or pad[y, x]:
            continue
        outside[y, x] = True
        todo += [(y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)]
    return ~outside[1:-1, 1:-1]


def banded_sky(spec):
    """A plain night wall: bands from `top` to `bottom` colour, then `ground` rows of black,
    with black figures cut from a sprite sheet standing on the ground line."""
    h, bands, ground = spec["height"], spec["bands"], spec["ground"]
    top, bottom = np.array(spec["top"]), np.array(spec["bottom"])
    sky = [tuple(int(v) for v in top + (bottom - top) * i / (bands - 1)) for i in range(bands)]
    # bands get shorter toward the ground, as the light gathers at the horizon
    edge = [round((h - ground) * (1 - ((bands - i) / bands) ** 1.5)) for i in range(bands + 1)]
    im = np.zeros((h, FB_W, 3), np.uint8)
    for i in range(bands):
        im[edge[i]:edge[i + 1]] = sky[i]
    if spec.get("figures"):
        sheet = np.array(Image.open(common.resolve(spec["sheet"])).convert("RGB"))
        for f in spec["figures"]:
            m = cut_figure(sheet, f["seed"])
            k = f.get("scale", 1)
            m = m.repeat(k, axis=0).repeat(k, axis=1)
            if f.get("flip"):
                m = m[:, ::-1]
            y0, x0 = h - ground - m.shape[0] + f.get("sink", 0), f["x"] - m.shape[1] // 2
            for y, x in zip(*np.nonzero(m)):
                if 0 <= y0 + y < h and 0 <= x0 + x < FB_W:
                    im[y0 + y, x0 + x] = 0
    return im


def backdrop(cfg):
    """The picture on the room's back wall: (pixels as colour numbers, [(r, g, b)]).
    A title lists its walls under "backdrops" and names the one in use in "backdrop"; the
    BACKDROP environment variable overrides that name. A wall is either an "image" (320 wide;
    `keep_rows` lists the source rows to stack, the last one meeting the floor) or a banded
    sky with figures (see banded_sky). A title with no wall gets a plain banded one."""
    name = os.environ.get("BACKDROP") or cfg.get("backdrop")
    spec = cfg.get("backdrops", {}).get(name) if name else None
    if name and spec is None:
        sys.exit(f"pack: no backdrop named {name!r} in {cfg['_path']}")
    if spec is None:
        spec = {"height": 150, "bands": 8, "ground": 0, "top": [8, 8, 24], "bottom": [40, 40, 88]}
    if "image" in spec:
        im = np.array(Image.open(common.resolve(spec["image"])).convert("RGB"))
        if im.shape[1] != FB_W:
            sys.exit(f"pack: backdrop {spec['image']} must be {FB_W} pixels wide, it is {im.shape[1]}")
        im = np.concatenate([im[a:b] for a, b in spec.get("keep_rows", [[0, im.shape[0]]])])
    else:
        im = banded_sky(spec)
    colours, index = np.unique(im.reshape(-1, 3), axis=0, return_inverse=True)
    if len(colours) > BACKDROP_MAX:
        q = Image.fromarray(im).quantize(BACKDROP_MAX, dither=Image.Dither.NONE)
        colours = np.array(q.getpalette()[:3 * BACKDROP_MAX]).reshape(-1, 3)
        index = np.array(q).reshape(-1)
    k = spec.get("brightness", 1.0)
    Image.fromarray(im).save(os.path.join(cfg["_art"], "backdrop_check.png"))
    return index.reshape(im.shape[:2]).astype(np.uint8), [tuple(min(255, int(c * k)) for c in rgb) for rgb in colours]


def rows(values, per, fmt="{}"):
    out = []
    for i in range(0, len(values), per):
        out.append("\t" + ",".join(fmt.format(v) for v in values[i:i + per]) + ",")
    return "\n".join(out)


def write(mesh, path, back):
    faces, texs = mesh["faces"], mesh["textures"]
    nshade = len(mesh["shades"])
    offs, off = [], 0
    for t in texs:
        offs.append(off)
        off += t.size
    h = [f"/* Generated by introlab/tools/cabinet/pack.py for title \"{mesh['title']}\". Do not edit.",
         " * Derived from Sega artwork: never commit this file. */",
         "#ifndef CABINET_DATA_H", "#define CABINET_DATA_H", "#include <stdint.h>", "",
         f"#define CAB_NVERTS    {len(mesh['verts'])}",
         f"#define CAB_NFACES    {len(faces)}",
         f"#define CAB_NTEX      {len(texs)}",
         f"#define CAB_NSHADE    {nshade}      /* CRAM index = base colour * CAB_NSHADE + shade */",
         f"#define CAB_NCRAM     {mesh['nbase'] * nshade}",
         f"#define CAB_BG        {mesh['background']}",
         f"#define CAB_UNITS_PER_INCH {mesh['units_per_inch']}",
         f"#define CAB_TEX_SHIFT {texs[0].shape[1].bit_length() - 1}       /* every texture is {texs[0].shape[1]} texels wide */",
         f"#define CAB_NREGION   {len(mesh['regions'])}       /* {', '.join(f'{i}={n}' for i, n in enumerate(mesh['regions']))} */",
         f"#define CAB_NTEX_BASE {len(mesh['region_tex'][0])}      /* faces name these; a region remaps them */",
         "#define CAB_NOTEX     0xFF", "",
         "/* Draw-order groups: far side panel (edge, then in/out), body, near side panel. */"]
    h += [f"#define CAB_G_{g.upper():<6} {i}" for i, g in enumerate(cabmesh.GROUPS)]
    h += ["", "typedef struct {",
          "\tuint8_t  nv;        /* 3 or 4, convex, wound for back-face culling */",
          "\tuint8_t  tex;       /* texture index, or CAB_NOTEX for a flat face */",
          "\tuint8_t  base;      /* base colour of a flat face */",
          "\tuint8_t  group;",
          "\tuint8_t  vi[4];",
          "\tint16_t  u[4], v[4]; /* texel coordinates */",
          "\tint16_t  n[3];      /* outward unit normal, 2.14 */",
          "} cab_face_t;", "",
          "typedef struct { uint16_t w, h; uint32_t offset; } cab_tex_t;", "",
          "static const int16_t cab_verts[CAB_NVERTS][3] = {"]
    h += ["\t{" + ",".join(str(c) for c in v) + "}," for v in mesh["verts"]]
    h += ["};", "", "static const cab_face_t cab_faces[CAB_NFACES] = {"]
    for f in faces:
        vi = f["vi"] + [0] * (4 - len(f["vi"]))
        uv = (f["uv"] or []) + [(0, 0)] * (4 - len(f["uv"] or []))
        tex = "CAB_NOTEX" if f["tex"] < 0 else str(f["tex"])
        h.append(f"\t{{{len(f['vi'])},{tex},{f['base']},{f['group']},{{{','.join(map(str, vi))}}},"
                 f"{{{','.join(str(u) for u, _ in uv)}}},{{{','.join(str(v) for _, v in uv)}}},"
                 f"{{{','.join(map(str, f['n']))}}}}},")
    h += ["};", "", "/* " + ", ".join(f"{i}={n}" for i, n in enumerate(mesh["texture_names"])) + " */",
          "static const cab_tex_t cab_tex[CAB_NTEX] = {"]
    h += [f"\t{{{t.shape[1]},{t.shape[0]},{o}}}," for t, o in zip(texs, offs)]
    h += ["};", "", "/* cab_region_tex[region][face texture] = the texture that region shows there. */",
          "static const uint8_t cab_region_tex[CAB_NREGION][CAB_NTEX_BASE] = {"]
    h += ["\t{" + ",".join(map(str, row)) + "}," for row in mesh["region_tex"]]
    h += ["};", "", f"/* Texels are base colour index x CAB_NSHADE, so a pixel is texel | shade; row-major. */",
          f"static const uint8_t cab_texels[{off}] = {{"]
    for t in texs:
        h.append(rows((t.flatten().astype(int) * nshade).tolist(), 32))
    if mesh["banners"]:
        bh, bw = mesh["banners"][0].shape
        h += ["};", "", "/* The marquee as a flat picture per region: finished pixels (brightest shade), row-major. */",
              f"#define CAB_BANNER_W {bw}", f"#define CAB_BANNER_H {bh}",
              "static const uint8_t cab_banner[CAB_NREGION][CAB_BANNER_W * CAB_BANNER_H] = {"]
        for b in mesh["banners"]:
            h.append("\t{")
            h.append(rows((b.flatten().astype(int) * nshade + nshade - 1).tolist(), 32))
            h.append("\t},")
    h += ["};", "", "/* 32X CRAM words, 0BBBBBGGGGGRRRRR. */", "static const uint16_t cab_cram[CAB_NCRAM] = {"]
    words = [(int(r) >> 3) | ((int(g) >> 3) << 5) | ((int(b) >> 3) << 10)
             for r, g, b in mesh["cram"][:mesh["nbase"] * nshade]]
    h.append(rows(words, 12, "0x{:04X}"))
    pix, colours = back
    ncram = mesh["nbase"] * nshade
    h += ["};", "", "/* The room's back wall: a picture FB_W wide whose last row meets the floor. Pixels are",
          " * finished CRAM indices, CAB_BACKDROP_CRAM onward; row-major. */",
          f"#define CAB_BACKDROP_H     {pix.shape[0]}",
          f"#define CAB_BACKDROP_CRAM  {ncram}", f"#define CAB_BACKDROP_NCRAM {len(colours)}",
          "static const uint8_t cab_backdrop[CAB_BACKDROP_H * 320] __attribute__((aligned(4))) = {",
          rows((pix.flatten().astype(int) + ncram).tolist(), 32),
          "};", "", "static const uint16_t cab_backdrop_cram[CAB_BACKDROP_NCRAM] = {",
          rows([(r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) for r, g, b in colours], 12, "0x{:04X}")]
    h += ["};", "", "#endif", ""]
    open(path, "w").write("\n".join(h))
    return off


if __name__ == "__main__":
    cfg = common.load_config(sys.argv[1])
    mesh = cabmesh.build(cfg)
    path = os.path.join(cfg["_art"], "cabinet_data.h")
    texel_bytes = write(mesh, path, backdrop(cfg))
    rom = texel_bytes + len(mesh["verts"]) * 6 + len(mesh["faces"]) * 30 + mesh["nbase"] * len(mesh["shades"]) * 2
    print(f"packed {mesh['title']}: {len(mesh['faces'])} faces, {len(mesh['verts'])} verts, "
          f"{len(mesh['textures'])} textures ({texel_bytes} B), {rom} B in rom -> {path}")
