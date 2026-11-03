#!/usr/bin/env python3
"""Lift the sky textures out of a Minecraft jar.

Usage: bake_sky.py <client.jar> <out.plsk>

The cloud sheet is one bit per texel in the jar; it becomes coverage here so
the shader can fade it. The sun keeps its colour. Both are Mojang's art, read
from the installation the player already owns and never committed here.
"""
import io
import sys
import zipfile
from pathlib import Path

from PIL import Image

MAGIC = b"PLSK"
CLOUD_SIDE = 256
SUN_SIDE = 32
ENVIRONMENT = "assets/minecraft/textures/environment"


def main(args):
    if len(args) != 2:
        sys.exit(__doc__)
    jar_path, out_path = Path(args[0]), Path(args[1])
    with zipfile.ZipFile(jar_path) as jar:
        clouds = Image.open(io.BytesIO(jar.read(f"{ENVIRONMENT}/clouds.png"))).convert("L")
        sun = Image.open(io.BytesIO(jar.read(f"{ENVIRONMENT}/celestial/sun.png"))).convert("RGBA")
    if clouds.size != (CLOUD_SIDE, CLOUD_SIDE) or sun.size != (SUN_SIDE, SUN_SIDE):
        sys.exit(f"unexpected sizes: clouds {clouds.size}, sun {sun.size}")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(MAGIC + clouds.tobytes() + sun.tobytes())
    print(f"{out_path}: clouds {CLOUD_SIDE}x{CLOUD_SIDE}, sun {SUN_SIDE}x{SUN_SIDE}")


if __name__ == "__main__":
    main(sys.argv[1:])
