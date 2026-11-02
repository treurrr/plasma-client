#!/usr/bin/env python3
"""Bake a font into an atlas the client can mmap.

Usage: bake_font.py <font.ttf> <pixel-size> <out.plft>

The interface font is ours, not Minecraft's: the game's own font is an asset of
theirs and stays out of this repository. IBM Plex is under the SIL Open Font
Licence, so a baked atlas can ship with the client.

Layout: PLFT, version, pixel size, line height, ascent, glyph count, atlas
width, atlas height, then one record per glyph and the single-channel coverage
bitmap. Coverage rather than colour, so the shader picks the colour.
"""
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

MAGIC = b"PLFT"
VERSION = 1
FIRST = 32
LAST = 126
PAD = 1

GLYPH = struct.Struct("<5h2h")   # x, y, w, h, advance, bearing_x, bearing_y
assert GLYPH.size == 14, GLYPH.size


def main(args):
    if len(args) != 3:
        sys.exit(__doc__)
    font_path, size, out_path = args[0], int(args[1]), Path(args[2])
    font = ImageFont.truetype(font_path, size)

    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    rendered = []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        box = font.getbbox(ch)
        width = max(1, box[2] - box[0])
        height = max(1, box[3] - box[1])
        image = Image.new("L", (width + 2 * PAD, height + 2 * PAD), 0)
        ImageDraw.Draw(image).text((PAD - box[0], PAD - box[1]), ch, font=font, fill=255)
        rendered.append((code, image, box, int(font.getlength(ch))))

    # One row per glyph is wasteful; pack into rows of a fixed width instead.
    atlas_width = 512
    x = y = row_height = 0
    places = []
    for _, image, _, _ in rendered:
        if x + image.width > atlas_width:
            x = 0
            y += row_height
            row_height = 0
        places.append((x, y))
        x += image.width
        row_height = max(row_height, image.height)
    atlas_height = y + row_height

    atlas = Image.new("L", (atlas_width, atlas_height), 0)
    records = bytearray()
    for (code, image, box, advance), (px, py) in zip(rendered, places):
        atlas.paste(image, (px, py))
        records.extend(GLYPH.pack(px + PAD, py + PAD, image.width - 2 * PAD,
                                  image.height - 2 * PAD, advance, box[0], box[1]))

    header = MAGIC + struct.pack("<6I", VERSION, size, line_height, ascent,
                                 len(rendered), atlas_width)
    header += struct.pack("<I", atlas_height)
    out_path.write_bytes(header + bytes(records) + atlas.tobytes())

    print(f"{out_path}: {(len(header) + len(records) + atlas_width * atlas_height) / 1024:.0f} KB")
    print(f"  {len(rendered)} glyphs, atlas {atlas_width}x{atlas_height}, "
          f"line height {line_height}, ascent {ascent}")


if __name__ == "__main__":
    main(sys.argv[1:])
