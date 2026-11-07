#!/usr/bin/env python3
"""The baker's mip levels for the textures MipOracle names, as digests.

Reads the oracle's lines on stdin ("mip <name>/<level> <w>x<h>:<digest>") and
prints one value per line in the same order, so run.py can compare them.
"""
import hashlib
import importlib.util
import io
import os
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("bake_atlas", HERE.parent / "bake_atlas.py")
bake_atlas = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake_atlas)

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402


def main():
    jar = os.environ.get("PLASMA_REFERENCE_JAR") or str(
        Path.home() / "mc-reference" / "1.21.11" / "client-named.jar")
    archive = zipfile.ZipFile(jar)
    cache = {}
    for line in sys.stdin:
        parts = line.split()
        if len(parts) < 2:
            continue
        name, level = parts[1].rsplit("/", 1)
        if name not in cache:
            raw = archive.read(f"assets/minecraft/textures/{name}.png")
            with Image.open(io.BytesIO(raw)) as image:
                pixels = np.asarray(image.convert("RGBA"), dtype=np.uint8).copy()
            strategy, bias = bake_atlas.texture_metadata(archive, "minecraft:" + name)
            cache[name] = bake_atlas.generate_mip_levels(pixels, 4, strategy, bias)
        levels = cache[name]
        index = int(level)
        if index >= len(levels):
            print("missing")
            continue
        tile = np.ascontiguousarray(levels[index])
        digest = hashlib.sha256(tile.tobytes()).hexdigest()[:16]
        print(f"{tile.shape[1]}x{tile.shape[0]}:{digest}")


if __name__ == "__main__":
    main()
