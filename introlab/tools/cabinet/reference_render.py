"""Runs INSIDE Blender. Renders the detailed source model at 320x224 from a front 3/4
angle: the picture the low-poly version is judged against.

    blender -b --python reference_render.py -- <title config.json> [azimuth degrees]

Writes <art>/reference.png. Azimuth 0 is head-on; positive turns toward the right side."""
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

args = sys.argv[sys.argv.index("--") + 1:]
cfg = common.load_config(args[0])
az = math.radians(float(args[1]) if len(args) > 1 else 30.0)
os.makedirs(cfg["_art"], exist_ok=True)

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
    o = bpy.data.objects[lift["object"]]
    o.matrix_world = Matrix.Translation(lift["by"]) @ o.matrix_world
bpy.context.view_layer.update()
lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
for o in sc.objects:
    if o.type == 'MESH':
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo, hi = Vector(map(min, lo, w)), Vector(map(max, hi, w))
size, ctr = hi - lo, (lo + hi) / 2

cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
sc.collection.objects.link(cam)
sc.camera = cam
dist = size.z * 2.2
cam.location = ctr + Vector((math.cos(az) * dist, math.sin(az) * dist, size.z * 0.10))   # the front is +X
cam.rotation_euler = (ctr - cam.location).to_track_quat('-Z', 'Y').to_euler()
cam.data.clip_start, cam.data.clip_end = dist / 100, dist * 10
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN'))
sc.collection.objects.link(sun)
sun.data.energy = 2.5
sun.rotation_euler = (math.radians(55), 0, az + math.radians(65))
world = bpy.data.worlds.new("w")
sc.world = world
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs[0].default_value, bg.inputs[1].default_value = (0.35, 0.35, 0.42, 1), 1.0
for engine in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE'):
    try:
        sc.render.engine = engine
        break
    except TypeError:
        pass
sc.view_settings.view_transform = 'Standard'
sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = 320, 224, 100
sc.render.image_settings.file_format = 'PNG'
sc.render.filepath = os.path.join(cfg["_art"], "reference.png")
bpy.ops.render.render(write_still=True)
print("REFERENCE", sc.render.filepath)
