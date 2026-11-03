#!/usr/bin/env python3
"""Bake the inventory icons the game draws for every item it knows.

Usage: bake_items.py <client.jar> <reports/registries.json> <protocol> <out.plit>

An item id from the server indexes straight into a layer here, the same way a
block texture id indexes into the block atlas. Items whose icon is a flat
sprite are taken as they are; items that are blocks have no sprite at all - the
game builds their icon out of the block model - so those carry the names of the
faces the renderer should assemble instead.
"""
import json
import struct
import sys
import zipfile
from pathlib import Path

from PIL import Image

MAGIC = b"PLIT"
VERSION = 2
TILE = 16


def align8(value):
    return (value + 7) & ~7


def main(args):
    if len(args) != 4:
        sys.exit(__doc__)
    jar_path, registries_path, protocol, out_path = args
    protocol = int(protocol)

    registries = json.loads(Path(registries_path).read_text())
    entries = registries["minecraft:item"]["entries"]
    by_id = [""] * len(entries)
    for name, entry in entries.items():
        by_id[entry["protocol_id"]] = name.removeprefix("minecraft:")

    tiles = []
    flat = 0
    block_like = []
    with zipfile.ZipFile(jar_path) as jar:
        names = set(jar.namelist())
        for name in by_id:
            path = f"assets/minecraft/textures/item/{name}.png"
            if path in names:
                with jar.open(path) as handle:
                    image = Image.open(handle).convert("RGBA")
                # Animated items store their frames stacked; the icon is the first.
                if image.height > image.width:
                    image = image.crop((0, 0, image.width, image.width))
                tiles.append(image.resize((TILE, TILE), Image.NEAREST).tobytes())
                flat += 1
            else:
                tiles.append(b"\0" * (TILE * TILE * 4))
                block_like.append(name)

    pixels = b"".join(tiles)
    # Names follow the pixels so the hud can say what is in hand without a
    # second file to keep in step.
    names = b"".join(struct.pack("<H", len(n.encode())) + n.encode() for n in by_id)
    header = MAGIC + struct.pack("<6I", VERSION, protocol, TILE, len(tiles), len(pixels),
                                 len(names))
    header += b"\0" * (align8(len(header)) - len(header))
    Path(out_path).write_bytes(header + pixels + names)

    print(f"{out_path}: {(len(header) + len(pixels)) / 1e6:.2f} MB")
    print(f"  {len(tiles)} items, {flat} with a sprite of their own")
    print(f"  {len(block_like)} are blocks and need their model drawn instead, "
          f"for example {', '.join(block_like[:4])}")


if __name__ == "__main__":
    main(sys.argv[1:])
