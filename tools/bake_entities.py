#!/usr/bin/env python3
"""Bake what the client draws entities with into one file.

Usage: bake_entities.py <client.jar> <data/entity_models.txt> <out.plen>

Every texture under textures/entity/ as RGBA, the shadow, the ascii sheet of
the default font with each glyph's advance (for name tags), and the model
geometry, which is not in the jar at all and comes from the hand-written data
file instead. The textures are Mojang's art, read from the player's own jar and
never committed here.
"""
import io
import json
import struct
import sys
import zipfile
import zlib
from pathlib import Path

from PIL import Image

MAGIC = b"PLEN"
VERSION = 1
ENTITY = "assets/minecraft/textures/entity/"
SHADOW = "assets/minecraft/textures/misc/shadow.png"


def rgba(jar, name):
    image = Image.open(io.BytesIO(jar.read(name))).convert("RGBA")
    return image.size, image.tobytes()


def font(jar):
    """The ascii sheet and how far each of its 256 glyphs moves the pen.

    A glyph is as wide as its rightmost lit column, plus one for the gap; the
    space is the one character whose advance is given rather than measured.
    """
    providers = json.loads(jar.read("assets/minecraft/font/include/default.json"))["providers"]
    sheet = next(p for p in providers if p.get("file") == "minecraft:font/ascii.png")
    image = Image.open(io.BytesIO(jar.read("assets/minecraft/textures/font/ascii.png"))).convert("RGBA")
    rows = sheet["chars"]
    cell_w = image.size[0] // len(rows[0])
    cell_h = image.size[1] // len(rows)
    pixels = image.load()
    advances = [0] * 256
    spaces = json.loads(jar.read("assets/minecraft/font/include/space.json"))["providers"][0]["advances"]
    for row, chars in enumerate(rows):
        for column, char in enumerate(chars):
            code = ord(char)
            if code == 0 or code > 255:
                continue
            width = 0
            for x in range(cell_w - 1, -1, -1):
                if any(pixels[column * cell_w + x, row * cell_h + y][3] != 0 for y in range(cell_h)):
                    width = x + 1
                    break
            advances[code] = width + 1
    for char, advance in spaces.items():
        if ord(char) < 256:
            advances[ord(char)] = advance
    return image.size, image.tobytes(), cell_w, cell_h, sheet["ascent"], advances


def main(args):
    if len(args) != 3:
        sys.exit(__doc__)
    jar_path, models_path, out_path = Path(args[0]), Path(args[1]), Path(args[2])
    models = models_path.read_bytes()
    with zipfile.ZipFile(jar_path) as jar:
        names = sorted(n for n in jar.namelist() if n.startswith(ENTITY) and n.endswith(".png"))
        textures = [(n[len(ENTITY):-4], *rgba(jar, n)) for n in names]
        textures.append(("$shadow", *rgba(jar, SHADOW)))
        font_size, font_pixels, cell_w, cell_h, ascent, advances = font(jar)

    body = bytearray()
    body += struct.pack("<I", len(models)) + models
    body += struct.pack("<I", len(textures))
    for name, (width, height), pixels in textures:
        encoded = name.encode()
        body += struct.pack("<H", len(encoded)) + encoded + struct.pack("<HH", width, height) + pixels
    body += struct.pack("<HHBBB", font_size[0], font_size[1], cell_w, cell_h, ascent)
    body += bytes(advances) + font_pixels

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(MAGIC + struct.pack("<II", VERSION, len(body)) + zlib.compress(bytes(body), 6))
    print(f"{out_path}: {len(textures)} textures, {sum(1 for l in models.splitlines() if l.startswith(b'['))} models, "
          f"{len(body) / 1e6:.1f} MB before compression")


if __name__ == "__main__":
    main(sys.argv[1:])
