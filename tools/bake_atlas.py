#!/usr/bin/env python3
"""Bake every texture the block table references into a layered array texture.

Usage: bake_atlas.py <client.jar> <blocks.plbk> <out.plat> [particle_sprites.txt]

Texture ids come from the block table and become array layers, so the two files
stay aligned by construction and a lookup needs no arithmetic at all. A layer
per texture also means mip levels are built per texture: a grid atlas bleeds
between neighbouring tiles as soon as minification filters across their shared
edge, which no amount of padding fully fixes.

The particle sprites, when their file is given, are appended behind the block
textures in the order it lists them, so a particle samples the same array as a
block does and its layer is the block texture count plus its index there. They
count as textures, not as further animation frames, so each keeps the identity
frame the renderer fills in by default.

The levels themselves are the game's: `MipmapGenerator.generateMipLevels` with
the mipmapLevels option at its default of 4, so a block in the distance shows
texel for texel what vanilla shows.
"""
import io
import json
import math
import struct
import sys
import zipfile
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image

MAGIC = b"PLAT"
VERSION = 4
# `Options.mipmapLevels`: four levels under the sprite's own, which takes a
# 16px block texture down to a single texel.
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


# `ARGB.SRGB_TO_LINEAR`, `ARGB.LINEAR_TO_SRGB`: the game averages texels in
# linear light through two small tables, so their rounding is part of the
# answer and a float pipeline does not reproduce it.
def _srgb_to_linear_table():
    out = np.empty(256, dtype=np.int32)
    for i in range(256):
        v = i / 255.0
        linear = ((v + 0.055) / 1.055) ** 2.4 if v >= 0.04045 else v / 12.92
        out[i] = math.floor(linear * 1023.0 + 0.5)
    return out


def _linear_to_srgb_table():
    out = np.empty(1024, dtype=np.uint8)
    for i in range(1024):
        v = i / 1023.0
        srgb = 1.055 * v ** (1.0 / 2.4) - 0.055 if v >= 0.0031308 else 12.92 * v
        out[i] = math.floor(srgb * 255.0 + 0.5)
    return out


SRGB_TO_LINEAR = _srgb_to_linear_table()
LINEAR_TO_SRGB = _linear_to_srgb_table()


def mean_linear(a, b, c, d):
    """`ARGB.meanLinear`: alpha averages as a plain integer, colour through the
    linear tables."""
    out = np.empty(a.shape, dtype=np.uint8)
    out[:, :, 3] = (a[:, :, 3].astype(np.int32) + b[:, :, 3] + c[:, :, 3] +
                    d[:, :, 3]) // 4
    for channel in range(3):
        total = (SRGB_TO_LINEAR[a[:, :, channel]] + SRGB_TO_LINEAR[b[:, :, channel]] +
                 SRGB_TO_LINEAR[c[:, :, channel]] + SRGB_TO_LINEAR[d[:, :, channel]]) // 4
        out[:, :, channel] = LINEAR_TO_SRGB[total]
    return out


def darkened_alpha_blend(a, b, c, d):
    """`MipmapGenerator.darkenedAlphaBlend`: leaves average only the texels
    that are there and still divide by four, so a half-empty corner fades out
    instead of smearing."""
    total = np.zeros(a.shape, dtype=np.float32)
    for quad in (a, b, c, d):
        visible = quad[:, :, 3] != 0
        linear = SRGB_TO_LINEAR[quad].astype(np.float32) / np.float32(1023.0)
        total += np.where(visible[:, :, None], linear, np.float32(0.0))
    total /= np.float32(4.0)
    index = np.clip(np.floor(total * np.float32(1023.0)).astype(np.int32), 0, 1023)
    return LINEAR_TO_SRGB[index]


def solidify(pixels):
    """`TextureUtil.solidify`: every transparent texel takes the colour of the
    nearest texel that is not, breadth first, with alpha left at zero. A
    transparent texel in a png is usually black, and black is what bleeds into
    the edge of a leaf once a level averages across it."""
    height, width = pixels.shape[:2]
    alpha = pixels[:, :, 3]
    if not (alpha != 0).any() or (alpha != 0).all():
        return pixels

    flat = pixels.reshape(-1, 4)
    colour = np.zeros((height * width, 3), dtype=np.uint8)
    distance = np.full(height * width, 1 << 30, dtype=np.int64)
    queue = deque()
    for x in range(width):
        for y in range(height):
            cell = x + y * width
            if flat[cell, 3] != 0:
                distance[cell] = 0
                colour[cell] = flat[cell, :3]
                queue.append(cell)

    while queue:
        cell = queue.popleft()
        x = cell % width
        y = cell // width
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx = x + dx
            ny = y + dy
            if nx < 0 or ny < 0 or nx >= width or ny >= height:
                continue
            neighbour = nx + ny * width
            if distance[neighbour] > distance[cell] + 1:
                distance[neighbour] = distance[cell] + 1
                colour[neighbour] = colour[cell]
                queue.append(neighbour)

    out = pixels.copy()
    hidden = (alpha == 0).reshape(-1)
    out.reshape(-1, 4)[hidden, 0:3] = colour[hidden]
    return out


def fill_empty_areas_with_dark_colour(pixels):
    """`TextureUtil.fillEmptyAreasWithDarkColor`: the darkest texel that is
    there, three quarters as bright, behind every texel that is not."""
    visible = pixels[:, :, 3] != 0
    if not visible.any():
        return pixels
    sums = pixels[:, :, :3].astype(np.int32).sum(axis=2)
    # The game walks columns first, so the darkest texel it keeps is the first
    # in x-then-y order; the same order here picks the same texel.
    ranked = np.where(visible, sums, 1 << 30).T.reshape(-1)
    darkest = pixels.transpose(1, 0, 2).reshape(-1, 4)[int(np.argmin(ranked))]
    fill = np.array([3 * int(darkest[0]) // 4, 3 * int(darkest[1]) // 4,
                     3 * int(darkest[2]) // 4], dtype=np.uint8)
    out = pixels.copy()
    out[~visible, 0:3] = fill
    return out


def alpha_test_coverage(pixels, threshold, scale):
    """`MipmapGenerator.alphaTestCoverage`: how much of the texture survives
    the cutout once alpha is scaled, sampled on a 4x4 grid inside every square
    of four texels."""
    height, width = pixels.shape[:2]
    if height < 2 or width < 2:
        return 0.0
    alpha = np.clip(pixels[:, :, 3].astype(np.float32) / np.float32(255.0) * scale,
                    np.float32(0.0), np.float32(1.0))
    a00 = alpha[:-1, :-1]
    a10 = alpha[:-1, 1:]
    a01 = alpha[1:, :-1]
    a11 = alpha[1:, 1:]
    hits = np.zeros(a00.shape, dtype=np.float32)
    for sy in range(4):
        wy = np.float32((sy + 0.5) / 4.0)
        for sx in range(4):
            wx = np.float32((sx + 0.5) / 4.0)
            blended = (a00 * (1.0 - wx) * (1.0 - wy) + a10 * wx * (1.0 - wy) +
                       a01 * (1.0 - wx) * wy + a11 * wx * wy)
            hits += blended > threshold
    return float((hits / np.float32(16.0)).sum() / ((width - 1) * (height - 1)))


def scale_alpha_to_coverage(pixels, target, threshold, bias):
    """`MipmapGenerator.scaleAlphaToCoverage`: five bisection steps for the
    alpha scale that keeps as much of the texture as the first level had, so a
    fern in the distance does not thin away to nothing."""
    low = 0.0
    high = 4.0
    scale = 1.0
    best = 1.0
    best_difference = float("inf")
    for _ in range(5):
        coverage = alpha_test_coverage(pixels, threshold, scale)
        difference = abs(coverage - target)
        if difference < best_difference:
            best_difference = difference
            best = scale
        if coverage < target:
            low = scale
        elif coverage > target:
            high = scale
        else:
            break
        scale = (low + high) * 0.5

    alpha = pixels[:, :, 3].astype(np.float32) / np.float32(255.0)
    alpha = np.clip(alpha * np.float32(best) + np.float32(bias) + np.float32(0.025),
                    np.float32(0.0), np.float32(1.0))
    pixels[:, :, 3] = np.floor(alpha * np.float32(255.0)).astype(np.uint8)
    return pixels


def generate_mip_levels(pixels, max_level, strategy, bias):
    """`MipmapGenerator.generateMipLevels`: the chain for one sprite, cutouts
    solidified first and every level scaled back to the coverage the first one
    had."""
    if strategy == "auto":
        strategy = "cutout" if (pixels[:, :, 3] == 0).any() else "mean"

    if strategy in ("cutout", "strict_cutout"):
        pixels = solidify(pixels)
    elif strategy == "dark_cutout":
        pixels = fill_empty_areas_with_dark_colour(pixels)

    levels = [pixels]
    cutout = strategy in ("cutout", "strict_cutout", "dark_cutout")
    threshold = 0.3 if strategy == "strict_cutout" else 0.5
    target = alpha_test_coverage(pixels, threshold, 1.0) if cutout else 0.0

    for _ in range(max_level):
        previous = levels[-1]
        if previous.shape[0] < 2 or previous.shape[1] < 2:
            break
        quads = (previous[0::2, 0::2], previous[0::2, 1::2],
                 previous[1::2, 0::2], previous[1::2, 1::2])
        level = darkened_alpha_blend(*quads) if strategy == "dark_cutout" \
            else mean_linear(*quads)
        if cutout:
            level = scale_alpha_to_coverage(level, target, threshold, bias)
        levels.append(level)
    return levels


def texture_metadata(archive, name):
    """`TextureMetadataSection`: the sprite's own mipmap strategy and alpha
    bias, or the defaults."""
    try:
        meta = json.loads(archive.read(asset_path(name) + ".mcmeta"))
    except KeyError:
        return "auto", 0.0
    section = meta.get("texture") or {}
    return section.get("mipmap_strategy", "auto"), float(
        section.get("alpha_cutoff_bias", 0.0))


def load_strip(archive, name):
    """The sprite exactly as it ships: one square frame, or a vertical strip of
    them. The game builds its levels over the whole strip, so we do too."""
    try:
        raw = archive.read(asset_path(name))
    except KeyError:
        return None
    with Image.open(io.BytesIO(raw)) as image:
        return np.asarray(image.convert("RGBA"), dtype=np.uint8).copy()


def frame_count(strip):
    side = strip.shape[1]
    return max(strip.shape[0] // side, 1)


def animation_of(archive, name, frames):
    """The texture's .mcmeta animation as (interpolate, [(frame, ticks)]), or
    None for a still texture."""
    try:
        meta = json.loads(archive.read(asset_path(name) + ".mcmeta"))
    except KeyError:
        return None
    animation = meta.get("animation")
    if animation is None or frames < 2:
        return None
    default_time = animation.get("frametime", 1)
    sequence = []
    for entry in animation.get("frames", range(frames)):
        if isinstance(entry, dict):
            sequence.append((entry["index"], entry.get("time", default_time)))
        else:
            sequence.append((entry, default_time))
    return bool(animation.get("interpolate", False)), sequence


def missing_tile(size):
    # The classic missing-texture check, so a gap is obvious on screen.
    tile = np.zeros((size, size, 4), dtype=np.uint8)
    tile[:, :, 3] = 255
    half = size // 2
    tile[:half, :half, :3] = (248, 0, 248)
    tile[half:, half:, :3] = (248, 0, 248)
    return tile


def upscale(level, factor):
    return np.repeat(np.repeat(level, factor, axis=0), factor, axis=1)


def to_power_of_two(strip):
    """A sprite whose side is not a power of two - several of the particle
    textures are 3x3 or 5x5 - resampled up to the next one, nearest texel. A
    layer is a power of two either way, and the shader samples across the whole
    of it, so every uv still lands on the texel the game would show."""
    native = strip.shape[1]
    if native & (native - 1) == 0:
        return strip
    side = 1 << (native - 1).bit_length()
    frames = frame_count(strip)
    rows = np.minimum((np.arange(side * frames) * native) // side, native * frames - 1)
    columns = np.minimum((np.arange(side) * native) // side, native - 1)
    return strip[rows][:, columns]


def chain_for(archive, name, strip, tile_size, levels_in_atlas):
    """One frame per level per layer. A sprite smaller than the layer is
    repeated up to its size, which leaves every level below unchanged, so each
    one still holds exactly the texels vanilla put there."""
    native = strip.shape[1]
    strategy, bias = texture_metadata(archive, name)
    chain = generate_mip_levels(strip, max(MIP_LEVELS, int(math.log2(native))),
                                strategy, bias)
    shift = int(math.log2(tile_size // native))
    frames = frame_count(strip)
    per_level = []
    for level in range(levels_in_atlas):
        source = chain[0] if level < shift else chain[min(level - shift, len(chain) - 1)]
        if level < shift:
            source = upscale(source, 1 << (shift - level))
        side = source.shape[1]
        per_level.append([source[i * side:(i + 1) * side] for i in range(frames)])
    return per_level, frames


def read_sprite_textures(path):
    """The T lines bake_particle_sprites.py wrote: the textures the particle
    sprite sets index into, in the order they are appended here."""
    return [line.split()[1] for line in Path(path).read_text().splitlines()
            if line.startswith("T ")]


def main(args):
    if not 3 <= len(args) <= 4:
        sys.exit(__doc__)
    jar, table_path, out_path = args[0], args[1], args[2]

    protocol, names = read_texture_names(table_path)
    blocks = len(names)
    if len(args) == 4:
        names = names + read_sprite_textures(args[3])
    archive = zipfile.ZipFile(jar)

    strips = []
    missing = 0
    tile_size = 16
    for name in names:
        strip = load_strip(archive, name)
        if strip is None:
            missing += 1
        else:
            strip = to_power_of_two(strip)
            tile_size = max(tile_size, strip.shape[1])
        strips.append(strip)

    levels_in_atlas = int(math.log2(tile_size)) + 1
    fallback = missing_tile(tile_size)

    layers = [[] for _ in range(levels_in_atlas)]
    animations = []
    animated = []
    for texture, (name, strip) in enumerate(zip(names, strips)):
        per_level, frames = chain_for(archive, name,
                                      fallback if strip is None else strip,
                                      tile_size, levels_in_atlas)
        for level in range(levels_in_atlas):
            layers[level].append(per_level[level][0])
        if frames > 1:
            animated.append((texture, name, per_level, frames))

    # A texture's other frames go after every texture's first one, so a texture
    # id is still its first frame's layer.
    for texture, name, per_level, frames in animated:
        layer_of = [texture]
        for frame in range(1, frames):
            layer_of.append(len(layers[0]))
            for level in range(levels_in_atlas):
                layers[level].append(per_level[level][frame])
        animation = animation_of(archive, name, frames)
        if animation is None:
            continue
        interpolate, sequence = animation
        animations.append((texture, interpolate,
                           [(layer_of[index % frames], max(time, 1))
                            for index, time in sequence]))

    # Vulkan reads one region per mip level with every layer behind each other,
    # so the file is laid out the same way.
    pixels = b"".join(np.ascontiguousarray(tile).tobytes()
                      for level in layers for tile in level)
    header = MAGIC + struct.pack("<7I", VERSION, protocol, tile_size, len(layers[0]),
                                 levels_in_atlas, len(pixels), len(names))
    header += b"\0" * (align8(len(header)) - len(header))

    table = struct.pack("<I", len(animations))
    for texture, interpolate, sequence in animations:
        table += struct.pack("<3I", texture, int(interpolate), len(sequence))
        for layer, time in sequence:
            table += struct.pack("<2I", layer, time)
    Path(out_path).write_bytes(header + pixels + table)
    print(f"{out_path}: {(len(header) + len(pixels)) / 1e6:.2f} MB")
    print(f"  {len(layers[0])} layers of {tile_size}px, {levels_in_atlas} mip levels, "
          f"{len(animations)} animated")
    if len(names) > blocks:
        print(f"  {blocks} block textures, {len(names) - blocks} particle sprite layers behind them")
    if missing:
        print(f"  {missing} textures not found; those got the missing-texture tile")


if __name__ == "__main__":
    main(sys.argv[1:])
