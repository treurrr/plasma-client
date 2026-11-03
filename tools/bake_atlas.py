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

import numpy as np
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
        sys.exit(f"{path} is not a block table")
    version, protocol, states, quads, textures, blocks, strings = struct.unpack_from(
        "<7I", data, 4)

    at = HEADER_BYTES
    at = align8(at + (states + 1) * 4)   # quad offsets
    at = align8(at + quads * 36)         # quads
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


def to_linear(srgb):
    return np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)


def to_srgb(linear):
    return np.where(linear <= 0.0031308, linear * 12.92,
                    1.055 * np.clip(linear, 0.0, None) ** (1.0 / 2.4) - 0.055)


def bleed(tile):
    """Give transparent texels the colour of their nearest opaque neighbour.

    A transparent texel in a png is usually black, and black is what bleeds
    into the edge of a leaf once anything averages or filters across it."""
    pixels = np.asarray(tile, dtype=np.float32)
    rgb = pixels[:, :, :3]
    alpha = pixels[:, :, 3]
    known = alpha > 0
    if not known.any() or known.all():
        return tile

    for _ in range(max(tile.width, tile.height)):
        weight = np.zeros_like(alpha)
        total = np.zeros_like(rgb)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            shifted_rgb = np.roll(np.roll(rgb, dy, axis=0), dx, axis=1)
            shifted_known = np.roll(np.roll(known, dy, axis=0), dx, axis=1)
            total += shifted_rgb * shifted_known[:, :, None]
            weight += shifted_known
        fill = (~known) & (weight > 0)
        if not fill.any():
            break
        rgb[fill] = total[fill] / weight[fill][:, None]
        known = known | fill

    pixels[:, :, :3] = rgb
    return Image.fromarray(np.round(pixels).astype(np.uint8), "RGBA")


def mip(tile):
    """Half-size, averaged the way the game averages: in linear light, weighted
    by alpha, so a texel nobody can see cannot darken one they can."""
    pixels = np.asarray(tile, dtype=np.float32) / 255.0
    alpha = pixels[:, :, 3]
    linear = to_linear(pixels[:, :, :3])

    quads = lambda a: (a[0::2, 0::2], a[0::2, 1::2], a[1::2, 0::2], a[1::2, 1::2])
    weights = quads(alpha)
    colours = quads(linear)

    weight = sum(weights)
    total = sum(c * w[:, :, None] for c, w in zip(colours, weights))
    safe = np.maximum(weight, 1e-6)[:, :, None]
    rgb = to_srgb(total / safe)
    # Where everything was transparent there is no colour to average; the
    # bleed above already put a sensible one there, so take it flat.
    flat = sum(colours) / 4.0
    rgb = np.where((weight == 0)[:, :, None], to_srgb(flat), rgb)

    out = np.empty((rgb.shape[0], rgb.shape[1], 4), dtype=np.float32)
    out[:, :, :3] = np.clip(rgb, 0.0, 1.0) * 255.0
    out[:, :, 3] = weight / 4.0 * 255.0
    return Image.fromarray(np.round(out).astype(np.uint8), "RGBA")


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
        tiles.append(bleed(tile))

    mips = 1
    while mips < MIP_LEVELS and (tile_size >> mips) >= 1:
        mips += 1

    # Vulkan reads one region per mip level with every layer behind each other,
    # so the file is laid out the same way.
    chunks = []
    levels = [tiles]
    for _ in range(1, mips):
        levels.append([mip(tile) for tile in levels[-1]])
    for level in levels:
        for tile in level:
            chunks.append(tile.tobytes())
    pixels = b"".join(chunks)

    header = MAGIC + struct.pack("<6I", VERSION, protocol, tile_size, len(tiles), mips,
                                 len(pixels))
    header += b"\0" * (align8(len(header)) - len(header))

    Path(out_path).write_bytes(header + pixels)
    print(f"{out_path}: {(len(header) + len(pixels)) / 1e6:.2f} MB")
    print(f"  {len(tiles)} layers of {tile_size}px, {mips} mip levels")
    if missing:
        print(f"  {missing} textures not found; those got the missing-texture tile")


if __name__ == "__main__":
    main(sys.argv[1:])
