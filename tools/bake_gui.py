#!/usr/bin/env python3
"""Bake the game's own interface sprites and its font into one atlas.

Usage: bake_gui.py <client.jar> <protocol> <out.plgu>

The hotbar, the hearts, the crosshair and everything else the hud is made of
ship as separate images. They are packed into one texture here, with the names
kept, so the client can draw an interface that is the game's rather than a
lookalike. Sprites that are meant to grow - buttons, tooltips, tabs - say how
in their own .mcmeta: the border that keeps its size, and whether the middle
stretches or repeats. That is carried along for the client to draw nine slices.
"""
import io
import json
import struct
import sys
import zipfile
from pathlib import Path

from PIL import Image

MAGIC = b"PLGU"
VERSION = 3
PREFIX = "assets/minecraft/textures/gui/sprites/"
# The container backgrounds are not sprites: they are whole 256x256 sheets the
# game blits a window out of, so they come in under their own names.
CONTAINERS = "assets/minecraft/textures/gui/container/"
# Whole textures the screens draw: the menu and list backgrounds, the header
# and footer lines, the recipe book's sheet and the title screen's logo.
TEXTURES = "assets/minecraft/textures/gui/"
TITLE = ("title/minecraft.png", "title/edition.png")
SIDE = 2048
PAD = 1


def align8(value):
    return (value + 7) & ~7


def read_font(jar):
    """Every glyph the default font draws from a bitmap, with its advance.

    The game lays each sheet out as a fixed grid of cells, one codepoint per
    cell, and the width a glyph takes is decided by its own pixels: the last
    column that has any ink, plus one. A cell with no ink at all is a space,
    and its advance comes from the space provider instead.
    """
    spaces = {}
    for provider in json.loads(jar.read("assets/minecraft/font/include/space.json"))["providers"]:
        if provider.get("type") == "space":
            for char, advance in provider["advances"].items():
                spaces[ord(char)] = advance

    out = {}
    sheets = json.loads(jar.read("assets/minecraft/font/include/default.json"))["providers"]
    for provider in sheets:
        if provider.get("type") != "bitmap":
            continue
        path = "assets/minecraft/textures/" + provider["file"].split(":", 1)[1]
        sheet = Image.open(io.BytesIO(jar.read(path))).convert("RGBA")
        rows = provider["chars"]
        columns = max(len(row) for row in rows)
        cell_w = sheet.width // columns
        cell_h = sheet.height // len(rows)
        height = provider.get("height", 8)
        ascent = provider["ascent"]
        for row_index, row in enumerate(rows):
            for column, char in enumerate(row):
                code = ord(char)
                if code == 0 or code in out:
                    continue
                cell = sheet.crop((column * cell_w, row_index * cell_h,
                                   (column + 1) * cell_w, (row_index + 1) * cell_h))
                box = cell.getchannel("A").getbbox()
                if box is None:
                    if code in spaces:
                        out[code] = (None, spaces[code], height, ascent)
                    continue
                # Scaled the way the sheet asks to be scaled, so a twelve pixel
                # accented sheet and an eight pixel ascii one line up.
                scale = height / cell_h
                advance = round((box[2]) * scale) + 1
                out[code] = (cell, advance, height, ascent)

    for code, advance in spaces.items():
        out.setdefault(code, (None, advance, 8, 7))

    return [(code, image, advance, height, ascent)
            for code, (image, advance, height, ascent) in sorted(out.items())
            if image is not None] + \
           [(code, Image.new("RGBA", (1, 1), (0, 0, 0, 0)), advance, height, ascent)
            for code, (image, advance, height, ascent) in sorted(out.items())
            if image is None]


def main(args):
    if len(args) != 3:
        sys.exit(__doc__)
    jar_path, protocol, out_path = args
    protocol = int(protocol)

    sprites = []
    glyphs = []
    slicing = {}
    with zipfile.ZipFile(jar_path) as jar:
        glyphs = read_font(jar)
        for name in sorted(jar.namelist()):
            if name.startswith(TEXTURES) and name.endswith(".png"):
                rest = name[len(TEXTURES):]
                if ("/" not in rest and rest != "demo_background.png") or rest in TITLE:
                    image = Image.open(io.BytesIO(jar.read(name))).convert("RGBA")
                    sprites.append(("gui/" + rest[:-len(".png")], image))
                    continue
            if name.startswith(CONTAINERS) and name.endswith(".png"):
                image = Image.open(io.BytesIO(jar.read(name))).convert("RGBA")
                sprites.append(("container/" + name[len(CONTAINERS):-len(".png")], image))
                continue
            if not name.startswith(PREFIX) or not name.endswith(".png"):
                continue
            image = Image.open(io.BytesIO(jar.read(name))).convert("RGBA")
            # An animated sprite stacks its frames; the first one is the sprite.
            meta = name + ".mcmeta"
            short = name[len(PREFIX):-len(".png")]
            if meta in jar.namelist():
                try:
                    frames = json.loads(jar.read(meta))
                    if "animation" in frames and image.height > image.width:
                        image = image.crop((0, 0, image.width, image.width))
                    scaling = frames.get("gui", {}).get("scaling", {})
                    if scaling.get("type") == "nine_slice":
                        border = scaling["border"]
                        if isinstance(border, int):
                            border = {"left": border, "top": border, "right": border,
                                      "bottom": border}
                        slicing[short] = (border["left"], border["top"], border["right"],
                                          border["bottom"], int(scaling.get("stretch_inner", False)))
                except ValueError:
                    pass
            sprites.append((short, image))

    # The font rides in the same atlas: a glyph is a sprite like any other.
    for code, image, advance, height, ascent in glyphs:
        sprites.append((f"glyph/{code}", image))
    advances = {code: (advance, height, ascent) for code, _, advance, height, ascent in glyphs}

    # Tallest first, then a shelf packer: with a few hundred small sprites this
    # wastes a little and costs nothing to understand.
    sprites.sort(key=lambda pair: -pair[1].height)
    atlas = Image.new("RGBA", (SIDE, SIDE), (0, 0, 0, 0))
    placed = []
    x = y = shelf = 0
    for name, image in sprites:
        if x + image.width + PAD > SIDE:
            x = 0
            y += shelf + PAD
            shelf = 0
        if y + image.height > SIDE:
            sys.exit(f"{SIDE}x{SIDE} is not enough for {len(sprites)} sprites")
        atlas.paste(image, (x, y))
        placed.append((name, x, y, image.width, image.height))
        x += image.width + PAD
        shelf = max(shelf, image.height)

    table = b"".join(
        struct.pack("<H", len(name.encode())) + name.encode() +
        struct.pack("<4H", sx, sy, w, h)
        for name, sx, sy, w, h in placed)
    # A codepoint needs its advance even when it has no picture, which is what
    # a space is, so the font table is kept apart from the sprite rectangles.
    font = b"".join(struct.pack("<IhBB", code, advance, height, ascent)
                    for code, (advance, height, ascent) in sorted(advances.items()))
    slices = b"".join(struct.pack("<H", len(name.encode())) + name.encode() +
                      struct.pack("<5B", *values) for name, values in sorted(slicing.items()))
    pixels = atlas.tobytes()
    header = MAGIC + struct.pack("<7I", VERSION, protocol, SIDE, len(placed), len(table),
                                 len(advances), len(slicing))
    header += b"\0" * (align8(len(header)) - len(header))
    Path(out_path).write_bytes(header + pixels + table + font + slices)

    print(f"{out_path}: {(len(header) + len(pixels) + len(table) + len(font)) / 1e6:.2f} MB")
    print(f"  {len(placed)} sprites in {SIDE}x{SIDE}, {y + shelf} rows used")
    print(f"  {len(advances)} glyphs from the game's own font")
    print(f"  {len(slicing)} sprites drawn in nine slices")


if __name__ == "__main__":
    main(sys.argv[1:])
