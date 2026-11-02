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
from resolve_block_models import Assets, FLUIDS, MODEL_LESS, quads_for_state  # noqa: E402

MAGIC = b"PLBK"
VERSION = 1
UNITS = 256  # model coordinates are 0..16; store sixteenths of a sixteenth
FACE_INDEX = {"down": 0, "up": 1, "north": 2, "south": 3, "west": 4, "east": 5}

FLAG_NO_MODEL = 1 << 0
FLAG_HAS_GEOMETRY = 1 << 1
FLAG_FULL_CUBE = 1 << 2
FLAG_OCCLUDES = 1 << 3
FLAG_FLUID = 1 << 4

QUAD = struct.Struct("<3h3h4hHBbbBBB")
assert QUAD.size == 28, QUAD.size


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


def is_full_cube(quads):
    if len(quads) != 6:
        return False
    if any(q["cullface"] is None for q in quads):
        return False
    for q in quads:
        if [float(v) for v in q["from"]] != [0.0, 0.0, 0.0]:
            return False
        if [float(v) for v in q["to"]] != [16.0, 16.0, 16.0]:
            return False
    return True


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
        sys.exit("blokstaat-ID's zijn niet aaneensluitend vanaf 0")

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
            quad_blob.extend(QUAD.pack(
                fixed(q["from"][0]), fixed(q["from"][1]), fixed(q["from"][2]),
                fixed(q["to"][0]), fixed(q["to"][1]), fixed(q["to"][2]),
                fixed(uv[0]), fixed(uv[1]), fixed(uv[2]), fixed(uv[3]),
                texture_id(q["texture"]),
                FACE_INDEX[q["face"]],
                FACE_INDEX[q["cullface"]] if q["cullface"] in FACE_INDEX else -1,
                -1 if tint is None else min(127, int(tint)),
                1 if q["uvlock"] else 0,
                (q["rotation"][0] // 90) % 4,
                (q["rotation"][1] // 90) % 4,
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
            if is_full_cube(quads):
                flags |= FLAG_FULL_CUBE
                if all(texture_is_opaque(assets.zip, q["texture"]) for q in quads):
                    flags |= FLAG_OCCLUDES
        state_flags.append(flags)

    if problems:
        sys.exit(f"{problems} staten konden niet worden opgelost; bak niets")

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
    print(f"  {len(states)} staten, {len(quad_blob) // QUAD.size} quads, "
          f"{len(texture_name_offsets)} texturen, {len(block_names)} blokken")
    print(f"  {full} staten zijn een volle kubus, waarvan {occluding} ondoorzichtig "
          f"(alleen die mogen buren wegsnijden)")


if __name__ == "__main__":
    main(sys.argv[1:])
