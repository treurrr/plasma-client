#!/usr/bin/env python3
"""Bake every texture the block table references into one atlas.

Usage: bake_atlas.py <client.jar> <blocks.plbk> <out.plat>

Texture ids come from the block table, so the two files stay aligned by
construction. Tiles are a uniform size - the largest native size in the pack,
smaller ones scaled up with nearest neighbour - which is what the game's own
atlas does and what keeps a lookup a single multiply.
"""
import io
import struct
import sys
import zipfile
from pathlib import Path

from PIL import Image

MAGIC = b"PLAT"
VERSION = 1
PLBK_MAGIC = b"PLBK"
HEADER_BYTES = 32


def align8(value):
    return (value + 7) & ~7


def read_texture_names(path):
    data = Path(path).read_bytes()
    if data[:4] != PLBK_MAGIC:
        sys.exit(f"{path} is geen blokkentabel")
    version, protocol, states, quads, textures, blocks, strings = struct.unpack_from(
        "<7I", data, 4)

    at = HEADER_BYTES
    at = align8(at + (states + 1) * 4)   # quad offsets
    at = align8(at + quads * 28)         # quads
    texture_offsets_at = at
    at = align8(at + textures * 4)
    at = align8(at + blocks * 4)         # block name offsets
    at = align8(at + states * 4)         # state -> block
    at = align8(at + states)             # state flags
    strings_at = at

    names = []
    for i in range(textures):
        offset = struct.unpack_from("<I", data, texture_offsets_at + i * 4)[0]
        end = data.index(b"\0", strings_at + offset)
        names.append(data[strings_at + offset:end].decode())
    return protocol, names


def asset_path(name):
    return f"assets/minecraft/textures/{name.split(':')[-1]}.png"


def load_tile(archive, name, size):
    try:
        raw = archive.read(asset_path(name))
    except KeyError:
        return None
    with Image.open(io.BytesIO(raw)) as image:
        image = image.convert("RGBA")
        # An animation is a vertical strip of square frames; take the first.
        if image.height > image.width:
            image = image.crop((0, 0, image.width, image.width))
        if image.size != (size, size):
            image = image.resize((size, size), Image.NEAREST)
        return image.copy()


def missing_tile(size):
    # The classic missing-texture check, so a gap is obvious on screen.
    tile = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    half = size // 2
    for box in ((0, 0, half, half), (half, half, size, size)):
        tile.paste((248, 0, 248, 255), box)
    return tile


def main(args):
    if len(args) != 3:
        sys.exit(__doc__)
    jar, table_path, out_path = args

    protocol, names = read_texture_names(table_path)
    archive = zipfile.ZipFile(jar)

    tile_size = 16
    for name in names:
        try:
            with Image.open(io.BytesIO(archive.read(asset_path(name)))) as image:
                tile_size = max(tile_size, image.width)
        except KeyError:
            continue

    columns = 1
    while columns * columns < len(names):
        columns *= 2
    rows = (len(names) + columns - 1) // columns
    atlas = Image.new("RGBA", (columns * tile_size, rows * tile_size), (0, 0, 0, 0))

    rects = []
    missing = 0
    fallback = missing_tile(tile_size)
    for index, name in enumerate(names):
        tile = load_tile(archive, name, tile_size)
        if tile is None:
            tile = fallback
            missing += 1
        x = (index % columns) * tile_size
        y = (index // columns) * tile_size
        atlas.paste(tile, (x, y))
        rects.append((x, y, tile_size, tile_size))

    pixels = atlas.tobytes()
    header = MAGIC + struct.pack("<7I", VERSION, protocol, atlas.width, atlas.height, tile_size,
                                 len(names), len(pixels))
    body = b"".join(struct.pack("<4H", *rect) for rect in rects)
    body += b"\0" * (align8(len(body)) - len(body))

    Path(out_path).write_bytes(header + body + pixels)
    print(f"{out_path}: {(len(header) + len(body) + len(pixels)) / 1e6:.2f} MB")
    print(f"  {atlas.width}x{atlas.height}, tegels van {tile_size}px, {len(names)} texturen")
    if missing:
        print(f"  {missing} texturen niet gevonden; die kregen het ontbrekend-blokje")


if __name__ == "__main__":
    main(sys.argv[1:])
