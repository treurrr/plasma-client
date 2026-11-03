#!/usr/bin/env python3
"""Bake every block state's geometry into one memory-mappable table.

Usage: bake_block_table.py <client.jar> <reports/blocks.json> <protocol> <out.plbk>

The client mmaps the result and reads it as-is: no JSON, no allocation, no
parsing at startup. Minecraft's own assets stay out of it - only geometry
derived from the installation the player already owns ends up in the file.
"""
import io
import json
import struct
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from resolve_block_models import (Assets, FACES, FLUIDS, MODEL_LESS,  # noqa: E402
                                   quads_for_state)

MAGIC = b"PLBK"
VERSION = 1
UNITS = 256  # model coordinates are 0..16; store sixteenths of a sixteenth
FACE_INDEX = {"down": 0, "up": 1, "north": 2, "south": 3, "west": 4, "east": 5}

FLAG_NO_MODEL = 1 << 0
FLAG_HAS_GEOMETRY = 1 << 1
FLAG_FULL_CUBE = 1 << 2
FLAG_OCCLUDES = 1 << 3
FLAG_FLUID = 1 << 4

QUAD_UVLOCK = 1 << 0
QUAD_NO_SHADE = 1 << 1
QUAD_NO_AO = 1 << 2
QUAD_TRANSLUCENT = 1 << 3
QUAD_RESCALE = 1 << 4

# A model only says a face is tinted, never which tint. Which one is a property
# of the block, the way the game decides it: grass and foliage read different
# colour maps, and three blocks ignore the maps entirely.
TINT_GRASS, TINT_FOLIAGE, TINT_WATER = 0, 1, 2
TINT_EVERGREEN, TINT_BIRCH, TINT_LILY, TINT_DRY = 3, 4, 5, 6

FIXED_TINT = {
    "minecraft:spruce_leaves": TINT_EVERGREEN,
    "minecraft:birch_leaves": TINT_BIRCH,
    "minecraft:lily_pad": TINT_LILY,
}
GRASS_TINTED = {
    "minecraft:grass_block", "minecraft:short_grass", "minecraft:fern",
    "minecraft:tall_grass", "minecraft:large_fern", "minecraft:potted_fern",
    "minecraft:sugar_cane",
}
DRY_TINTED = {"minecraft:pale_oak_leaves", "minecraft:leaf_litter"}


AXIS_INDEX = {"x": 0, "y": 1, "z": 2}


def element_rotation(rotation):
    """Angle in eighths of 45 degrees, the axis, the origin, and whether the
    element is widened to keep its corners where they were."""
    if not rotation:
        return 0, 0, (8.0, 8.0, 8.0), False
    angle = float(rotation.get("angle", 0))
    axis = AXIS_INDEX.get(rotation.get("axis", "y"), 1)
    origin = tuple(float(v) for v in rotation.get("origin", [8, 8, 8]))
    return round(angle / 22.5), axis, origin, bool(rotation.get("rescale"))


def tint_kind(name, tint):
    if tint is None:
        return -1
    if name in FLUIDS:
        return TINT_WATER
    if name in FIXED_TINT:
        return FIXED_TINT[name]
    if name in DRY_TINTED:
        return TINT_DRY
    if name in GRASS_TINTED:
        return TINT_GRASS
    if name.endswith("_leaves") or name == "minecraft:vine":
        return TINT_FOLIAGE
    return TINT_GRASS


QUAD = struct.Struct("<3h3h4hHBbbBBBb B 3h")
assert QUAD.size == 36, QUAD.size


def fixed(value):
    scaled = round(float(value) * UNITS)
    if not -32768 <= scaled <= 32767:
        raise ValueError(f"model coordinate {value} out of range")
    return scaled


_opacity = {}


def texture_is_opaque(archive, name):
    """Leaves, glass and ice fill their whole block but you can see through
    them, so geometry alone does not decide whether a block may cull its
    neighbours."""
    if name in _opacity:
        return _opacity[name]
    path = f"assets/minecraft/textures/{name.split(':')[-1]}.png"
    try:
        raw = archive.read(path)
    except KeyError:
        _opacity[name] = False
        return False
    with Image.open(io.BytesIO(raw)) as image:
        if image.mode in ("RGB", "L"):
            opaque = True
        else:
            alpha = image.convert("RGBA").getchannel("A")
            opaque = alpha.getextrema()[0] == 255
    _opacity[name] = opaque
    return opaque


def covers_whole_block(quad):
    return ([float(v) for v in quad["from"]] == [0.0, 0.0, 0.0]
            and [float(v) for v in quad["to"]] == [16.0, 16.0, 16.0]
            and quad["cullface"] is not None)


def full_faces(quads):
    """The directions covered edge to edge.

    Counting quads is wrong: grass has ten, six sides plus four tinted
    overlays, and is every bit as solid as stone."""
    return {q["face"] for q in quads if covers_whole_block(q)}


def opaque_faces(archive, quads):
    covered = set()
    for quad in quads:
        if covers_whole_block(quad) and texture_is_opaque(archive, quad["texture"]):
            covered.add(quad["face"])
    return covered


def pad(blob, alignment=8):
    while len(blob) % alignment:
        blob += b"\0"
    return blob


def main(args):
    if len(args) != 4:
        sys.exit(__doc__)
    jar, report, protocol, out_path = args[0], args[1], int(args[2]), Path(args[3])

    assets = Assets(jar)
    blocks = json.loads(Path(report).read_text())

    states = []
    for name, info in blocks.items():
        for state in info.get("states", []):
            states.append((state["id"], name, state.get("properties", {})))
    states.sort()
    if [s[0] for s in states] != list(range(len(states))):
        sys.exit("block state ids are not contiguous from 0")

    strings = bytearray()
    string_offsets = {}

    def intern(text):
        if text not in string_offsets:
            string_offsets[text] = len(strings)
            strings.extend(text.encode())
            strings.append(0)
        return string_offsets[text]

    block_names = sorted({name for _, name, _ in states})
    block_index = {name: i for i, name in enumerate(block_names)}
    block_name_offsets = [intern(name) for name in block_names]

    textures = {}
    texture_name_offsets = []

    def texture_id(path):
        if path not in textures:
            textures[path] = len(texture_name_offsets)
            texture_name_offsets.append(intern(path))
        return textures[path]

    # A texture with fractional alpha - water, ice, stained glass - has to be
    # blended, not cut out, and blending has to happen after everything solid.
    translucent_textures = {}

    def translucent(path):
        if path not in translucent_textures:
            try:
                with Image.open(io.BytesIO(assets.zip.read(f"assets/minecraft/textures/{path}.png"))) as image:
                    alpha = image.convert("RGBA").getchannel("A").getextrema()
                translucent_textures[path] = 0 < alpha[0] < 255 or 0 < alpha[1] < 255
            except KeyError:
                translucent_textures[path] = False
        return translucent_textures[path]

    quad_blob = bytearray()
    quad_offsets = [0]
    state_block = []
    state_flags = []
    problems = 0

    for state_id, name, properties in states:
        quads, problem = quads_for_state(assets, name, properties)
        if problem:
            problems += 1
            quads = []

        for q in quads:
            uv = q["uv"] or [0, 0, 16, 16]
            tint = q["tintindex"]
            eighths, axis, origin, rescale = element_rotation(q.get("element_rotation"))
            quad_blob.extend(QUAD.pack(
                fixed(q["from"][0]), fixed(q["from"][1]), fixed(q["from"][2]),
                fixed(q["to"][0]), fixed(q["to"][1]), fixed(q["to"][2]),
                fixed(uv[0]), fixed(uv[1]), fixed(uv[2]), fixed(uv[3]),
                texture_id(q["texture"]),
                FACE_INDEX[q["face"]],
                FACE_INDEX[q["cullface"]] if q["cullface"] in FACE_INDEX else -1,
                tint_kind(name, tint),
                ((1 if q["uvlock"] else 0) |
                 (0 if q.get("shade", True) else QUAD_NO_SHADE) |
                 (0 if q.get("ambientocclusion", True) else QUAD_NO_AO) |
                 (QUAD_TRANSLUCENT if translucent(q["texture"]) else 0) |
                 (QUAD_RESCALE if rescale else 0)),
                (q["rotation"][0] // 90) % 4,
                (q["rotation"][1] // 90) % 4,
                eighths,
                axis,
                fixed(origin[0]), fixed(origin[1]), fixed(origin[2]),
            ))

        quad_offsets.append(len(quad_blob) // QUAD.size)
        state_block.append(block_index[name])

        flags = 0
        if name in MODEL_LESS:
            flags |= FLAG_NO_MODEL
        if name in FLUIDS:
            flags |= FLAG_FLUID
        if quads:
            flags |= FLAG_HAS_GEOMETRY
            if len(full_faces(quads)) == len(FACES):
                flags |= FLAG_FULL_CUBE
                if len(opaque_faces(assets.zip, quads)) == len(FACES):
                    flags |= FLAG_OCCLUDES
        state_flags.append(flags)

    if problems:
        sys.exit(f"{problems} states could not be resolved; baking nothing")

    header = MAGIC + struct.pack("<7I", VERSION, protocol, len(states),
                                 len(quad_blob) // QUAD.size,
                                 len(texture_name_offsets), len(block_names), len(strings))
    body = b""
    body = pad(body + struct.pack(f"<{len(quad_offsets)}I", *quad_offsets))
    body = pad(body + bytes(quad_blob))
    body = pad(body + struct.pack(f"<{len(texture_name_offsets)}I", *texture_name_offsets))
    body = pad(body + struct.pack(f"<{len(block_name_offsets)}I", *block_name_offsets))
    body = pad(body + struct.pack(f"<{len(state_block)}I", *state_block))
    body = pad(body + bytes(state_flags))
    body = pad(body + bytes(strings))

    out_path.write_bytes(pad(header) + body)

    full = sum(1 for f in state_flags if f & FLAG_FULL_CUBE)
    occluding = sum(1 for f in state_flags if f & FLAG_OCCLUDES)
    print(f"{out_path}: {len(pad(header) + body) / 1e6:.2f} MB")
    print(f"  {len(states)} states, {len(quad_blob) // QUAD.size} quads, "
          f"{len(texture_name_offsets)} textures, {len(block_names)} blocks")
    print(f"  {full} states are a full cube, of which {occluding} are opaque "
          f"(only those may cull neighbours)")


if __name__ == "__main__":
    main(sys.argv[1:])
