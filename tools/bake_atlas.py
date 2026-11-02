#!/usr/bin/env python3
"""Bake every texture the block table references into a layered array texture.

Usage: bake_atlas.py <client.jar> <blocks.plbk> <out.plat>

Texture ids come from the block table and become array layers, so the two files
stay aligned by construction and a lookup needs no arithmetic at all. A layer
per texture also means mip levels are built per texture: a grid atlas bleeds
between neighbouring tiles as soon as minification filters across their shared
edge, which no amount of padding fully fixes.
"""
import io
import struct
import sys
import zipfile
from pathlib import Path

from PIL import Image

MAGIC = b"PLAT"
VERSION = 3
# Four levels takes a 32px tile down to 4px; below that neighbouring tiles
# start to matter even though a box filter stays inside tile boundaries.
MIP_LEVELS = 4
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

    tiles = []
    missing = 0
    fallback = missing_tile(tile_size)
    for name in names:
        tile = load_tile(archive, name, tile_size)
        if tile is None:
            tile = fallback
            missing += 1
        tiles.append(tile)

    mips = 1
    while mips < MIP_LEVELS and (tile_size >> mips) >= 1:
        mips += 1

    # Vulkan reads one region per mip level with every layer behind each other,
    # so the file is laid out the same way.
    chunks = []
    for mip in range(mips):
        size = tile_size >> mip
        for tile in tiles:
            level = tile if size == tile_size else tile.resize((size, size), Image.BOX)
            chunks.append(level.tobytes())
    pixels = b"".join(chunks)

    header = MAGIC + struct.pack("<6I", VERSION, protocol, tile_size, len(tiles), mips,
                                 len(pixels))
    header += b"\0" * (align8(len(header)) - len(header))

    Path(out_path).write_bytes(header + pixels)
    print(f"{out_path}: {(len(header) + len(pixels)) / 1e6:.2f} MB")
    print(f"  {len(tiles)} lagen van {tile_size}px, {mips} mipniveaus")
    if missing:
        print(f"  {missing} texturen niet gevonden; die kregen het ontbrekend-blokje")


if __name__ == "__main__":
    main(sys.argv[1:])
