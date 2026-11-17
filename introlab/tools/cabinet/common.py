"""Shared by every cabinet tool, including the one that runs inside Blender, so: standard
library only."""
import json
import os
import subprocess

LAB = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # introlab/
REPO = os.path.dirname(LAB)


def _main_checkout():
    """The repository's primary working tree. Untracked artwork exists only there."""
    try:
        common = subprocess.run(["git", "-C", REPO, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True, check=True).stdout.strip()
        return os.path.dirname(common)
    except (OSError, subprocess.CalledProcessError):
        return REPO


def resolve(path):
    """A config path: absolute, or relative to this worktree, or to the main checkout."""
    if os.path.isabs(path):
        return path
    for root in (REPO, _main_checkout()):
        p = os.path.join(root, path)
        if os.path.exists(p):
            return p
    raise SystemExit(f"cabinet: {path} is in neither {REPO} nor {_main_checkout()}")


def load_config(path):
    cfg = json.load(open(path))
    cfg["_path"] = os.path.abspath(path)
    cfg["_art"] = os.path.join(LAB, "art", cfg["title"])
    return cfg
