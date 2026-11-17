#!/usr/bin/env python3
"""One command per title: model -> low-poly mesh + baked textures -> rom header -> preview.

    build.py <title config.json> [--force]

The Blender step (about a minute) is skipped when its outputs are newer than the config,
the model and the extraction script; --force runs it regardless."""
import os
import shutil
import subprocess
import sys

import common

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        sys.exit(__doc__)
    cfg = common.load_config(args[0])
    art = cfg["_art"]
    os.makedirs(art, exist_ok=True)
    extract = os.path.join(HERE, "extract_cab.py")
    cab = os.path.join(art, "cab.json")
    sources = [cfg["_path"], common.resolve(cfg["model"]), extract]
    stale = "--force" in sys.argv or not os.path.exists(cab) or \
        any(os.path.getmtime(s) > os.path.getmtime(cab) for s in sources)
    if stale:
        blender = shutil.which("blender")
        if not blender:
            sys.exit("build: blender is not on PATH; it is needed to derive the mesh and bake the textures")
        print(f"extract {cfg['title']}: running Blender on {cfg['model']}", flush=True)
        r = subprocess.run([blender, "-b", "--python", extract, "--", cfg["_path"]], capture_output=True, text=True)
        tail = [line for line in r.stdout.splitlines() if line.startswith(("PROFILE", "DONE"))]
        if r.returncode != 0 or not any(line.startswith("DONE") for line in tail):
            sys.exit("build: Blender extraction failed\n" + "\n".join((r.stdout + r.stderr).splitlines()[-25:]))
        print("  " + "; ".join(tail), flush=True)
    else:
        print(f"extract {cfg['title']}: up to date", flush=True)
    for tool in ("pack.py", "orbit_preview.py"):
        subprocess.run([sys.executable, os.path.join(HERE, tool), cfg["_path"]], check=True)


if __name__ == "__main__":
    main()
