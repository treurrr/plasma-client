#!/usr/bin/env python3
"""Bake the interface typefaces and icons into one atlas the client can mmap.

Usage: bake_font.py <pixel-size> <out.plft>

The interface font is ours, not Minecraft's: the game's own font is an asset of
theirs and stays out of this repository. Noto and Font Awesome are both under
the SIL Open Font Licence, so a baked atlas can ship with the client.

One atlas holds every style and every icon, so the whole interface is one
pipeline, one buffer and one draw call.

Layout: PLFT, version, pixel size, line height, ascent, record count, atlas
width, atlas height, style count, icon count, then one record per glyph and the
single-channel coverage bitmap. Coverage rather than colour, so the shader picks
the colour.
"""
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

MAGIC = b"PLFT"
VERSION = 2
FIRST = 32
LAST = 126
PAD = 1

NOTO = "/usr/share/fonts/google-noto"
AWESOME = "/usr/share/fonts/fontawesome-6-free-fonts"
SOLID = f"{AWESOME}/Font Awesome 6 Free-Solid-900.otf"
OUTLINE = f"{AWESOME}/Font Awesome 6 Free-Regular-400.otf"

STYLES = [
    f"{NOTO}/NotoSans-Medium.ttf",
    f"{NOTO}/NotoSans-Bold.ttf",
    f"{NOTO}/NotoSansMono-Medium.ttf",
]

# Order is the Icon enum in text.hpp; changing it changes the client.
ICONS = [
    (SOLID, 0xF004),    # heart
    (OUTLINE, 0xF004),  # heart outline
    (SOLID, 0xF023),    # lock
    (SOLID, 0xF002),    # search
    (SOLID, 0xF013),    # gear
    (SOLID, 0xF009),    # grid
    (SOLID, 0xF108),    # display
    (SOLID, 0xF5FD),    # layers
    (SOLID, 0xF012),    # signal
    (SOLID, 0xF071),    # warning
    (SOLID, 0xF054),    # chevron
    (SOLID, 0xF70C),    # running
    (SOLID, 0xF05B),    # crosshair
    (SOLID, 0xF0C3),    # flask
    (SOLID, 0xF3ED),    # shield
    (SOLID, 0xF553),    # shirt
    (SOLID, 0xF06E),    # eye
    (SOLID, 0xF06D),    # fire
    (SOLID, 0xF03A),    # list
    (SOLID, 0xF1B2),    # cube
    (SOLID, 0xF11C),    # keyboard
    (SOLID, 0xF0E7),    # bolt
    (SOLID, 0xF625),    # gauge
    (SOLID, 0xF8CC),    # mouse
    (SOLID, 0xF14E),    # compass
    (SOLID, 0xF2F2),    # clock
    (SOLID, 0xF807),    # helmet
    (SOLID, 0xE085),    # vest
    (SOLID, 0xF696),    # socks
    (SOLID, 0xF54B),    # boot
    (SOLID, 0xF065),    # expand
    (SOLID, 0xF504),    # sneaking
]

# Brand art, baked as coverage like everything else. The mark is two inks, so
# it is two layers the client stacks in the two brand colours.
IMAGES = [
    ("brand/source/plasma-mark.png", (0x4A, 0x32, 0xF0)),
    ("brand/source/plasma-mark.png", (0xE0, 0x55, 0xF0)),
    ("brand/source/plasma-wordmark.png", None),
]

GLYPH = struct.Struct("<5h2h")   # x, y, w, h, advance, bearing_x, bearing_y
assert GLYPH.size == 14, GLYPH.size


def render(font, ch):
    box = font.getbbox(ch)
    width = max(1, box[2] - box[0])
    height = max(1, box[3] - box[1])
    image = Image.new("L", (width + 2 * PAD, height + 2 * PAD), 0)
    ImageDraw.Draw(image).text((PAD - box[0], PAD - box[1]), ch, font=font, fill=255)
    return image, box, int(font.getlength(ch))


def layer(path, ink, size):
    """One ink of an image, trimmed and scaled to the baked size."""
    image = Image.open(Path(__file__).resolve().parent.parent / path).convert("RGBA")
    pixels = image.load()
    mask = Image.new("L", image.size, 0)
    out = mask.load()
    inks = [other for _, other in IMAGES if other is not None]
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = pixels[x, y]
            if a == 0:
                continue
            if ink is not None:
                nearest = min(inks, key=lambda c: (c[0] - r) ** 2 + (c[1] - g) ** 2 +
                              (c[2] - b) ** 2)
                if nearest != ink:
                    continue
            out[x, y] = a
    # Every ink is cropped to the whole image's bounds, so the layers still
    # line up when the client stacks them.
    mask = mask.crop(image.getchannel("A").getbbox())
    scaled = mask.resize((max(1, round(mask.width * size / mask.height)), size),
                         Image.LANCZOS)
    padded = Image.new("L", (scaled.width + 2 * PAD, scaled.height + 2 * PAD), 0)
    padded.paste(scaled, (PAD, PAD))
    return padded


def main(args):
    if len(args) != 2:
        sys.exit(__doc__)
    size, out_path = int(args[0]), Path(args[1])

    rendered = []
    line_height = ascent = 0
    for index, path in enumerate(STYLES):
        font = ImageFont.truetype(path, size)
        if index == 0:
            ascent, descent = font.getmetrics()
            line_height = ascent + descent
        for code in range(FIRST, LAST + 1):
            rendered.append(render(font, chr(code)))

    # Icons are square by convention, so they sit on a text baseline at whatever
    # size the caller asks for without per-icon fiddling.
    for path, code in ICONS:
        font = ImageFont.truetype(path, size)
        image, box, _ = render(font, chr(code))
        rendered.append((image, box, image.width - 2 * PAD))

    for path, ink in IMAGES:
        image = layer(path, ink, size)
        rendered.append((image, (0, 0, image.width, image.height), image.width - 2 * PAD))

    atlas_width = 1024
    x = y = row_height = 0
    places = []
    for image, _, _ in rendered:
        if x + image.width > atlas_width:
            x = 0
            y += row_height
            row_height = 0
        places.append((x, y))
        x += image.width
        row_height = max(row_height, image.height)
    atlas_height = y + row_height

    # A solid patch, so the same pipeline can fill rectangles without a second
    # atlas or a second pipeline.
    atlas_height += 4
    atlas = Image.new("L", (atlas_width, atlas_height), 0)
    for sx in range(4):
        for sy in range(4):
            atlas.putpixel((atlas_width - 4 + sx, atlas_height - 4 + sy), 255)

    records = bytearray()
    for (image, box, advance), (px, py) in zip(rendered, places):
        atlas.paste(image, (px, py))
        records.extend(GLYPH.pack(px + PAD, py + PAD, image.width - 2 * PAD,
                                  image.height - 2 * PAD, advance, box[0], box[1]))

    header = MAGIC + struct.pack("<9I", VERSION, size, line_height, ascent, len(rendered),
                                 atlas_width, atlas_height, len(STYLES),
                                 len(ICONS) + len(IMAGES))
    out_path.write_bytes(header + bytes(records) + atlas.tobytes())

    print(f"{out_path}: {(len(header) + len(records) + atlas_width * atlas_height) / 1024:.0f} KB")
    print(f"  {len(STYLES)} styles, {len(ICONS) + len(IMAGES)} icons, "
          f"atlas {atlas_width}x{atlas_height}, "
          f"line height {line_height}, ascent {ascent}")


if __name__ == "__main__":
    main(sys.argv[1:])
