#!/usr/bin/env python3
"""Lift the grass, foliage and dry foliage colour maps out of a Minecraft jar.

Usage: bake_colormaps.py <client.jar> <out.plcm>

These two 256x256 images are what decides whether a plains looks like a plains
and a swamp like a swamp. They are Mojang's art, so they are read from the
installation the player already owns and never committed here.
"""
import io
import sys
import zipfile
from pathlib import Path

from PIL import Image

MAGIC = b"PLCM"
SIDE = 256


def read(jar, name):
    with jar.open(f"assets/minecraft/textures/colormap/{name}.png") as entry:
        image = Image.open(io.BytesIO(entry.read())).convert("RGB")
    if image.size != (SIDE, SIDE):
        sys.exit(f"{name}.png is {image.size}, expected {SIDE}x{SIDE}")
    return image.tobytes()


def main(args):
    if len(args) != 2:
        sys.exit(__doc__)
    jar_path, out_path = Path(args[0]), Path(args[1])
    with zipfile.ZipFile(jar_path) as jar:
        grass = read(jar, "grass")
        foliage = read(jar, "foliage")
        dry = read(jar, "dry_foliage")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(MAGIC + grass + foliage + dry)
    print(f"{out_path}: three {SIDE}x{SIDE} maps, "
          f"{(len(grass) + len(foliage) + len(dry)) / 1024:.0f} KB")


if __name__ == "__main__":
    main(sys.argv[1:])
