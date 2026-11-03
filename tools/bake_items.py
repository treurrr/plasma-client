#!/usr/bin/env python3
"""Bake the inventory icons the game draws for every item it knows.

Usage: bake_items.py <client.jar> <reports/registries.json> <reports/items.json>
                     <protocol> <out.plit>

An item id from the server indexes straight into a layer here, the same way a
block texture id indexes into the block atlas. Items whose icon is a flat
sprite are taken as they are; items that are blocks have no sprite at all - the
game builds their icon out of the block model - so those carry the names of the
faces the renderer should assemble instead.
"""
import io
import json
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from bake_block_table import (TINT_BIRCH, TINT_DRY, TINT_EVERGREEN, TINT_FOLIAGE, TINT_GRASS,
                              TINT_LILY, TINT_WATER, tint_kind)
from item_cube import render as render_cube
from resolve_block_models import Assets, deref_texture

# The colours a plains biome resolves to, which is what the game paints an
# inventory icon with: a slot has no biome to ask.
PLAINS = {
    TINT_GRASS: (0x91, 0xBD, 0x59),
    TINT_FOLIAGE: (0x77, 0xAB, 0x2F),
    TINT_WATER: (0x3F, 0x76, 0xE4),
    TINT_DRY: (0x8F, 0x7A, 0x36),
    # These three never vary with the biome, in a slot or in the world.
    TINT_EVERGREEN: (0x61, 0x99, 0x61),
    TINT_BIRCH: (0x80, 0xA7, 0x55),
    TINT_LILY: (0x20, 0x80, 0x30),
}

MAGIC = b"PLIT"
VERSION = 4
TILE = 16


def align8(value):
    return (value + 7) & ~7


def first_model(node, depth=0):
    """The first plain model reference under an item definition."""
    if depth > 8 or node is None:
        return None
    if isinstance(node, str):
        return node
    if not isinstance(node, dict):
        return None
    if node.get("type") == "minecraft:model":
        reference = node.get("model")
        return reference if isinstance(reference, str) else None
    for key in ("model", "on_false", "on_true", "fallback"):
        found = first_model(node.get(key), depth + 1)
        if found is not None:
            return found
    for key in ("cases", "entries"):
        for case in node.get(key) or []:
            found = first_model(case if isinstance(case, str) else case.get("model"), depth + 1)
            if found is not None:
                return found
    return None


def cube_icon(assets, jar, names, item):
    """The inventory icon the game builds out of a block's own model.

    Only blocks whose model really is one full cube are drawn: a staircase or a
    fence would need its whole model rendered, and a cube in its place would be
    a lie about what the item is.
    """
    definition = f"assets/minecraft/items/{item}.json"
    if definition not in names:
        return None
    try:
        entry = json.loads(jar.read(definition))
    except ValueError:
        return None
    # Conditional and composite definitions pick between models by the item's
    # own state. For a slot icon the first model that can be reached is close
    # enough, so this walks down the first branch and gives up rather than
    # guessing at a state the item does not have here.
    reference = first_model(entry.get("model"))
    if reference is None:
        return None

    resolved = assets.resolve_model(reference)
    if resolved is None:
        return None
    elements = resolved["elements"]
    full = next((e for e in elements
                 if e.get("from") == [0, 0, 0] and e.get("to") == [16, 16, 16]), None)
    if full is None:
        return None

    faces = {}
    for side in ("down", "up", "north", "south", "west", "east"):
        face = full.get("faces", {}).get(side)
        if face is None:
            continue
        path = deref_texture(face.get("texture"), resolved["textures"])
        if not isinstance(path, str):
            continue
        full_path = f"assets/minecraft/textures/{path.split(':')[-1]}.png"
        if full_path not in names:
            continue
        image = Image.open(io.BytesIO(jar.read(full_path))).convert("RGBA")
        if image.height > image.width:
            image = image.crop((0, 0, image.width, image.width))
        # A tinted face ships grey and is multiplied by a colour the block
        # decides from its biome; a slot has no biome, so it gets plains.
        kind = tint_kind("minecraft:" + item, face.get("tintindex"))
        colour = PLAINS.get(kind)
        if colour is not None:
            tinted = np.array(image, dtype=np.float32)
            for channel in range(3):
                tinted[..., channel] *= colour[channel] / 255.0
            image = Image.fromarray(np.clip(tinted, 0, 255).astype(np.uint8), "RGBA")
        faces[side] = image

    # A cutout texture would show straight through a three faced cube, so the
    # gaps are closed first: the game draws the far faces behind the near ones
    # and you never see daylight through a block of leaves in a slot.
    if not all(side in faces for side in ("up", "north", "east")):
        return None
    return render_cube(faces, TILE)


def main(args):
    if len(args) != 5:
        sys.exit(__doc__)
    jar_path, registries_path, items_path, protocol, out_path = args
    protocol = int(protocol)

    registries = json.loads(Path(registries_path).read_text())
    entries = registries["minecraft:item"]["entries"]
    by_id = [""] * len(entries)
    for name, entry in entries.items():
        by_id[entry["protocol_id"]] = name.removeprefix("minecraft:")

    # Armour points are the game's own numbers, out of its own report, rather
    # than a table typed in here.
    components = json.loads(Path(items_path).read_text())
    armour = [0] * len(by_id)
    for name, entry in components.items():
        protocol_id = entries.get(name, {}).get("protocol_id")
        if protocol_id is None:
            continue
        for modifier in entry.get("components", {}).get("minecraft:attribute_modifiers", []):
            if modifier.get("type") == "minecraft:armor":
                armour[protocol_id] = int(modifier.get("amount", 0))

    tiles = []
    flat = 0
    cubes = 0
    block_like = []
    assets = Assets(jar_path)
    with zipfile.ZipFile(jar_path) as jar:
        names = set(jar.namelist())
        for name in by_id:
            path = f"assets/minecraft/textures/item/{name}.png"
            # A compass or a clock has no picture of its own, only frames; the
            # inventory shows the first one when no world says otherwise.
            if path not in names:
                path = f"assets/minecraft/textures/item/{name}_00.png"
            if path in names:
                with jar.open(path) as handle:
                    image = Image.open(handle).convert("RGBA")
                # Animated items store their frames stacked; the icon is the first.
                if image.height > image.width:
                    image = image.crop((0, 0, image.width, image.width))
                tiles.append(image.resize((TILE, TILE), Image.NEAREST).tobytes())
                flat += 1
            else:
                icon = cube_icon(assets, jar, names, name)
                if icon is not None:
                    tiles.append(icon.tobytes())
                    cubes += 1
                else:
                    tiles.append(b"\0" * (TILE * TILE * 4))
                    block_like.append(name)

    pixels = b"".join(tiles)
    # Names follow the pixels so the hud can say what is in hand without a
    # second file to keep in step.
    names = b"".join(struct.pack("<H", len(n.encode())) + n.encode() + struct.pack("<B", a)
                     for n, a in zip(by_id, armour))
    header = MAGIC + struct.pack("<6I", VERSION, protocol, TILE, len(tiles), len(pixels),
                                 len(names))
    header += b"\0" * (align8(len(header)) - len(header))
    Path(out_path).write_bytes(header + pixels + names)

    print(f"{out_path}: {(len(header) + len(pixels)) / 1e6:.2f} MB")
    print(f"  {len(tiles)} items, {flat} with a sprite of their own")
    print(f"  {cubes} are blocks drawn as the cube the game draws in a slot")
    print(f"  {len(block_like)} have neither, for example {', '.join(block_like[:5])}")
    print(f"  {sum(1 for a in armour if a > 0)} are armour")


if __name__ == "__main__":
    main(sys.argv[1:])
