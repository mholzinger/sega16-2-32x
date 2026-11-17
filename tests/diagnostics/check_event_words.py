#!/usr/bin/env python3
"""Actual C painter guards plus strict footer decoder synthetic captures."""
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from rig_event import read
from active_area import X0, Y0, W, H, RAW


def paint(page, words):
    im = Image.new("RGB", (320, 224), (24, 48, 72))
    draw = ImageDraw.Draw(im)
    for row, word in enumerate(words):
        marker = 0xD000 | page << 8 | (1 + row // 2) << 4 | row % 2
        y = 188 + row * 2
        for b in range(32):
            draw.rectangle((b * 8, y, b * 8 + 7, y + 1),
                           fill="white" if word & (1 << (31 - b)) else "black")
        for b in range(16):
            draw.rectangle((256 + b * 4, y, 259 + b * 4, y + 1),
                           fill="white" if marker & (1 << (15 - b)) else "black")
    return im


with tempfile.TemporaryDirectory(prefix="event-words-") as tmp:
    tmp = Path(tmp)
    exe = tmp / "check"
    subprocess.run([os.environ.get("CC", "clang"), "-std=c99", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(Path(__file__).with_suffix(".c")), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    capture = tmp / "capture.png"

    def decode(im):
        im.save(capture)
        return read(capture)

    def reject(im):
        try:
            decode(im)
        except ValueError:
            return
        raise AssertionError("decoder accepted invalid capture")

    rng = random.Random(0x93726)
    for page in range(5):
        for case in range(12):
            words = [rng.getrandbits(32) for _ in range(4)]
            if not case:
                words = [0, 0xFFFFFFFF, 0x80000001, 0xAAAAAAAA]
            normal = paint(page, words)
            for dx in (-1, 0, 1):
                shifted = Image.new("RGB", normal.size, "black")
                shifted.paste(normal, (dx, 0))
                decoded = decode(shifted)
                assert decoded["page"] == page and decoded["raw32_values"] == words
                assert decoded["entrance"]["fields"] == words[:2]
                assert decoded["death"]["fields"] == words[2:]
                raw = Image.new("RGB", RAW, (40, 40, 40))
                raw.paste(shifted.resize((W, H), Image.Resampling.NEAREST), (X0, Y0))
                assert decode(raw) == decoded
    im = paint(2, [0, 1, 2, 3])
    reject(im.resize((640, 448)))
    bad = im.copy(); bad.putpixel((2, 188), (100, 100, 100)); reject(bad)
    bad = im.copy(); bad.putpixel((2, 188), (255, 255, 255)); reject(bad)
    bad = im.copy()
    ImageDraw.Draw(bad).rectangle((0, 189, 7, 189), fill="white")
    reject(bad)  # complete first-bit disagreement across duplicate rows
    for replacement in (0xC210, 0xD510, 0xD310, 0xD220, 0xD211):
        bad = im.copy(); draw = ImageDraw.Draw(bad)
        for b in range(16):
            draw.rectangle((256 + b * 4, 188, 259 + b * 4, 189),
                           fill="white" if replacement & (1 << (15 - b)) else "black")
        reject(bad)
    print("event decoder:360 native/raw shifted captures; bad geometry/colors/bits/duplicates/page/phase/field rejected")
