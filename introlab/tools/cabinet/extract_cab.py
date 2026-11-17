"""Runs INSIDE Blender. Derives a low-poly cabinet from a detailed model: ray-casts the side
silhouette and the centre-line profile, simplifies both, then bakes one flat-lit
orthographic texture per low-poly face.

    blender -b --python extract_cab.py -- <title config.json>

Writes <art>/cab.json (the profiles and the face list) and <art>/bake/*.png."""
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

cfg = common.load_config(sys.argv[-1])
out = os.path.join(cfg["_art"], "bake")
os.makedirs(out, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=common.resolve(cfg["model"]))
sc = bpy.context.scene
root = bpy.data.objects.new("root", None)
sc.collection.objects.link(root)
for o in list(sc.objects):
    if o.parent is None and o is not root:
        o.parent = root
root.rotation_euler = tuple(math.radians(d) for d in cfg["rotate_deg"])
bpy.context.view_layer.update()
for lift in cfg.get("lift", []):
    o = bpy.data.objects.get(lift["object"])
    if o is None:
        raise SystemExit(f"extract: the model has no object {lift['object']}")
    o.matrix_world = Matrix.Translation(lift["by"]) @ o.matrix_world
bpy.context.view_layer.update()

lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
for o in sc.objects:
    if o.type == 'MESH':
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo, hi = Vector(map(min, lo, w)), Vector(map(max, hi, w))
# Decal lifts push the box out by a hair; the cabinet proper is the rounded box.
X0, X1, YL, YR, HTOP = round(lo.x, 1), round(hi.x, 1), round(lo.y, 1), round(hi.y, 1), hi.z
T = cfg["side_panel_thickness"]
BODY_W, YC, XC = (YR - YL) - 2 * T, (YL + YR) / 2, (X0 + X1) / 2
dg = bpy.context.evaluated_depsgraph_get()


def cast(o, d):
    hit, loc, *_ = sc.ray_cast(dg, Vector(o), Vector(d))
    return loc if hit else None


def simplify(pts, tol):
    """Douglas-Peucker."""
    if len(pts) < 3:
        return pts
    (x0, z0), (x1, z1) = pts[0], pts[-1]
    length = math.hypot(x1 - x0, z1 - z0) or 1e-9
    best, bi = -1.0, 0
    for i in range(1, len(pts) - 1):
        d = abs((x1 - x0) * (z0 - pts[i][1]) - (x0 - pts[i][0]) * (z1 - z0)) / length
        if d > best:
            best, bi = d, i
    if best <= tol:
        return [pts[0], pts[-1]]
    return simplify(pts[:bi + 1], tol)[:-1] + simplify(pts[bi:], tol)


far = (X1 - X0) + 30
side_f, side_b, ctr = [], [], []
z = 0.15
while z < HTOP - 0.1:
    f = cast((X1 + far, YL + T / 2, z), (-1, 0, 0))
    b = cast((X0 - far, YL + T / 2, z), (1, 0, 0))
    if f and b:
        side_f.append((f.x, z))
        side_b.append((b.x, z))
    xs = sorted(h.x for h in (cast((X1 + far, YL + T + BODY_W * k, z), (-1, 0, 0))
                              for k in (0.18, 0.34, 0.5, 0.66, 0.82)) if h)
    if xs:
        ctr.append((xs[len(xs) // 2], z))       # the median ignores joysticks and coin slots
    z += 0.25
tol = cfg["profile_tolerance"]
side_f, side_b, ctr = simplify(side_f, tol["side"]), simplify(side_b, tol["side"]), simplify(ctr, tol["centre"])
print("PROFILE side_front", len(side_f), "side_back", len(side_b), "centre", len(ctr))

# ---- bakes: white world light only, orthographic, one per face ----
world = bpy.data.worlds.new("w")
sc.world = world
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs[0].default_value, bg.inputs[1].default_value = (1, 1, 1, 1), 1.0
for engine in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE'):
    try:
        sc.render.engine = engine
        break
    except TypeError:
        pass
sc.view_settings.view_transform = 'Standard'
sc.render.film_transparent = True
sc.render.image_settings.file_format = 'PNG'
sc.render.resolution_percentage = 100
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
sc.collection.objects.link(cam)
sc.camera = cam
cam.data.type = 'ORTHO'
cam.data.sensor_fit = 'HORIZONTAL'           # ortho_scale is the WIDTH of the view


def bake(name, centre, right, up, normal, wid, hgt, rw, rh, depth):
    """Render the rectangle wid x hgt centred on `centre`, seen along -normal, into rw x rh
    pixels. Pixels are made non-square when the rectangle and the image disagree in shape."""
    r, u, n = Vector(right).normalized(), Vector(up).normalized(), Vector(normal).normalized()
    m = Matrix((r, u, n)).transposed().to_4x4()
    m.translation = Vector(centre) + n * 2.0
    cam.matrix_world = m
    cam.data.clip_start, cam.data.clip_end = 0.05, 2.0 + depth
    cam.data.ortho_scale = wid
    stretch = (hgt * rw) / (wid * rh)        # pixel height / pixel width; Blender wants both >= 1
    sc.render.pixel_aspect_x, sc.render.pixel_aspect_y = (1.0, stretch) if stretch >= 1 else (1.0 / stretch, 1.0)
    sc.render.resolution_x, sc.render.resolution_y = rw, rh
    sc.render.filepath = os.path.join(out, name + ".png")
    bpy.ops.render.render(write_still=True)


faces = []
for i in range(len(ctr) - 1):
    (x0, z0), (x1, z1) = ctr[i], ctr[i + 1]
    length = math.hypot(x1 - x0, z1 - z0)
    t = Vector(((x1 - x0) / length, 0, (z1 - z0) / length))
    n = Vector((t.z, 0, -t.x))
    rh = max(4, min(128, 1 << max(2, round(math.log2(max(1.0, 128 * length / BODY_W))))))
    name = f"c{i:02d}"
    bake(name, ((x0 + x1) / 2, YC, (z0 + z1) / 2), t.cross(n), t, n, BODY_W, length, 128, rh, 1.2)
    faces.append(dict(name=name, p0=[x0, z0], p1=[x1, z1], tex=name, tw=128, th=rh))
    if cfg.get("banner", {}).get("face") == name:      # the same face again, at its flat on-screen size
        bw, bh = cfg["banner"]["size"]
        bake("banner", ((x0 + x1) / 2, YC, (z0 + z1) / 2), t.cross(n), t, n, BODY_W, length, bw, bh, 1.2)
SW = HTOP * 128 / 256                        # the 128x256 side bake covers this much depth
bake("side_r", (XC, YR, HTOP / 2), (-1, 0, 0), (0, 0, 1), (0, 1, 0), SW, HTOP, 128, 256, 0.3)
bake("side_l", (XC, YL, HTOP / 2), (1, 0, 0), (0, 0, 1), (0, -1, 0), SW, HTOP, 128, 256, 0.3)
json.dump(dict(side_front=side_f, side_back=side_b, centre=ctr, faces=faces,
               side=dict(x0=XC - SW / 2, x1=XC + SW / 2, z0=0, z1=HTOP),
               yl=YL, yr=YR, t=T, htop=HTOP, origin=[XC, YC, HTOP / 2]),
          open(os.path.join(cfg["_art"], "cab.json"), "w"), indent=1)
print("DONE", len(faces), "centre faces")
