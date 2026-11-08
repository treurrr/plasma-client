#!/usr/bin/env python3
"""Write down which textures each flat item icon was composed from.

Usage: bake_item_layers.py <client.jar> <items.plit> <out.txt>

tools/bake_items.py renders every item's slot picture once, at bake time, and
a rendered picture cannot be composed again from different textures. For the
items whose model is layers of flat sprite - `item/generated` and its children,
which is most of what a player holds - the whole render is one resample and one
tint per layer, so the recipe is enough to compose the icon again from other
pictures.

That is what a server's resource pack needs: the client cannot run a Python
baker mid-session, but with this file it can read the pack's own
textures/item/*.png and build the same icon the baker would have.

One tab-separated line per layer, in the order they are drawn over one another:
the item's protocol id, its name, the texture the layer reads, and the rgba the
layer is multiplied by.
"""
import json
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import bake_items
import item_render
from resolve_block_models import Assets, deref_texture

PLIT_MAGIC = b"PLIT"
HEADER_BYTES = 32


def read_item_names(path):
    """The names bake_items.py wrote behind the pixels, in protocol id order."""
    data = Path(path).read_bytes()
    if data[:4] != PLIT_MAGIC:
        sys.exit(f"{path} is not an item table")
    version, protocol, tile, layers, pixel_bytes, name_bytes = struct.unpack_from("<6I", data, 4)
    at = HEADER_BYTES + pixel_bytes
    names = []
    for _ in range(layers):
        length = struct.unpack_from("<H", data, at)[0]
        at += 2
        names.append(data[at:at + length].decode())
        at += length + 1   # the armour byte follows each name
    return protocol, tile, names


def flat_layers(assets, jar, textures, grass_map, item):
    """The item's flat layers as (texture, rgba tint), or an empty list when
    its icon is rendered from a model instead."""
    definition = f"assets/minecraft/items/{item}.json"
    if definition not in textures.names:
        return []
    try:
        entry = json.loads(jar.read(definition))
    except ValueError:
        return []
    reference, tint_entries = bake_items.default_branch(entry.get("model"))
    if reference is None:
        return []
    resolved = assets.resolve_model(reference)
    if resolved is None or not any(key.startswith("layer") for key in resolved["textures"]):
        return []
    tints = bake_items.tint_colours(tint_entries, grass_map)
    out = []
    for index in range(16):
        if resolved["textures"].get(f"layer{index}") is None:
            break
        path = deref_texture(f"#layer{index}", resolved["textures"])
        if not isinstance(path, str) or textures.get(path) is None:
            continue
        tint = tints[index] if index < len(tints) else item_render.WHITE
        out.append((path, tint))
    return out


def main(args):
    if len(args) != 3:
        sys.exit(__doc__)
    jar_path, plit_path, out_path = args
    protocol, tile, names = read_item_names(plit_path)

    assets = Assets(jar_path)
    rows = [f"# tile {tile}", "# id\tname\ttexture\ttint rgba"]
    items = 0
    with zipfile.ZipFile(jar_path) as jar:
        textures = bake_items.Textures(jar)
        with jar.open("assets/minecraft/textures/colormap/grass.png") as handle:
            grass_map = np.array(Image.open(handle).convert("RGB"), dtype=np.uint8)
        for item_id, name in enumerate(names):
            layers = flat_layers(assets, jar, textures, grass_map, name)
            if layers:
                items += 1
            for path, tint in layers:
                tint_hex = "".join(f"{int(round(float(c) * 255.0)):02x}" for c in tint)
                rows.append(f"{item_id}\t{name}\t{path}\t{tint_hex}")

    Path(out_path).write_text("\n".join(rows) + "\n")
    print(f"{out_path}: {len(rows) - 2} flat layers over {items} items, "
          f"of {len(names)} in protocol {protocol}")


if __name__ == "__main__":
    main(sys.argv[1:])
