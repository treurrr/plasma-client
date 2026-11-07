#!/usr/bin/env python3
"""Bake the inventory icons the game draws for every item it knows.

Usage: bake_items.py <client.jar> <reports/registries.json> <reports/items.json>
                     <protocol> <out.plit> [--tile N] [--entity-models FILE]

An item id from the server indexes straight into a layer here, the same way a
block texture id indexes into the block atlas. The game resolves an item to a
model, places it with that model's `gui` display transform and renders it into
a cell of its own item atlas once per item state; this does the same work once,
at bake time, so a slot costs one textured quad.

Items whose model is a flat sprite - `item/generated` and its children - come
out of their layers unchanged. Everything else is rendered: a block's own
model, turned and shaded the way the game turns and shades it in a slot.
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
import item_render
import special_items
from resolve_block_models import Assets, deref_texture

MAGIC = b"PLIT"
VERSION = 4
# A slot is sixteen game pixels across and the game renders its own cell at
# sixteen times the gui scale, so no one baked size is right at every scale.
# Thirty two is gui scale two exactly, which is what this client's shots and
# the vanilla screenshot they are checked against both use; a larger scale
# enlarges it rather than resampling someone else's render.
TILE = 32


def align8(value):
    return (value + 7) & ~7


def default_branch(node, depth=0):
    """The model a stack with no components of its own resolves to.

    Every branching item definition picks by something a bare stack does not
    have - a charge, a trim, a damage fraction - so the default branch is the
    one the creative menu and an empty hand show.
    """
    if depth > 12 or node is None:
        return None, []
    if isinstance(node, str):
        return node, []
    if not isinstance(node, dict):
        return None, []
    kind = node.get("type")
    if kind == "minecraft:model":
        reference = node.get("model")
        return (reference if isinstance(reference, str) else None), node.get("tints") or []
    if kind == "minecraft:composite":
        for part in node.get("models") or []:
            found, tints = default_branch(part, depth + 1)
            if found is not None:
                return found, tints
        return None, []
    for key in ("fallback", "on_false", "model", "on_true"):
        if key in node:
            found, tints = default_branch(node.get(key), depth + 1)
            if found is not None:
                return found, tints
    for key in ("cases", "entries"):
        for case in node.get(key) or []:
            found, tints = default_branch(
                case if isinstance(case, str) else case.get("model"), depth + 1)
            if found is not None:
                return found, tints
    return None, []


def default_special(node, depth=0):
    """The `minecraft:special` renderer a bare stack resolves to.

    Returns the base model, which carries the display transforms, and the
    renderer's own node. The branch taken is the same default one
    `default_branch` walks, so a chest is the ordinary chest rather than the
    gift one and a shield is the one that is not being raised.
    """
    if depth > 12 or not isinstance(node, dict):
        return None
    if node.get("type") == "minecraft:special":
        return node.get("base"), node.get("model")
    if node.get("type") == "minecraft:composite":
        for part in node.get("models") or []:
            found = default_special(part, depth + 1)
            if found is not None:
                return found
        return None
    for key in ("fallback", "on_false", "model", "on_true"):
        if key in node:
            found = default_special(node.get(key), depth + 1)
            if found is not None:
                return found
    for key in ("cases", "entries"):
        for case in node.get(key) or []:
            found = default_special(case.get("model") if isinstance(case, dict) else None,
                                    depth + 1)
            if found is not None:
                return found
    return None


class Textures:
    """Every picture a model asks for, decoded once and kept."""

    def __init__(self, jar):
        self.jar = jar
        self.names = set(jar.namelist())
        self.cache = {}

    def get(self, reference):
        if reference in self.cache:
            return self.cache[reference]
        path = f"assets/minecraft/textures/{reference.split(':')[-1]}.png"
        image = None
        if path in self.names:
            with self.jar.open(path) as handle:
                picture = Image.open(handle).convert("RGBA")
            # An animated texture stacks its frames; a slot shows the first.
            if picture.height > picture.width:
                picture = picture.crop((0, 0, picture.width, picture.width))
            image = np.array(picture, dtype=np.uint8)
        self.cache[reference] = image
        return image


def tint_colours(entries, grass_map):
    """What each tint index multiplies by for a stack with nothing on it.

    A tint source reads a component the stack does not carry, so it falls back
    to the `default` the definition names; the two that read the world instead
    - a constant and a biome colour - are known here and now.
    """
    colours = []
    for entry in entries:
        kind = entry.get("type", "")
        if kind == "minecraft:constant":
            packed = entry.get("value", -1)
        elif kind == "minecraft:grass":
            # `GrassColor.get`: the colour map read at the biome's own corner.
            temperature = float(entry.get("temperature", 0.5))
            downfall = float(entry.get("downfall", 1.0))
            x = int((1.0 - temperature) * 255.0)
            y = int((1.0 - downfall * temperature) * 255.0)
            pixel = grass_map[min(y, 255), min(x, 255)]
            packed = 0xFF000000 | int(pixel[0]) << 16 | int(pixel[1]) << 8 | int(pixel[2])
        else:
            packed = entry.get("default", -1)
        colours.append(np.array([(packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF,
                                 (packed >> 24) & 0xFF], dtype=np.float64) / 255.0)
    return colours


def flat_icon(resolved, textures, tints, tile):
    """An item whose model is layers of flat sprite: its own picture, tinted.

    `item/generated` leaves the `gui` context untransformed, so the sprite
    fills the cell exactly and the whole render is one upscale.
    """
    out = np.zeros((tile, tile, 4), dtype=np.float64)
    drawn = False
    for index in range(16):
        reference = resolved["textures"].get(f"layer{index}")
        if reference is None:
            break
        path = deref_texture(f"#layer{index}", resolved["textures"])
        image = textures.get(path) if isinstance(path, str) else None
        if image is None:
            continue
        side = image.shape[0]
        if side != tile:
            rows = (np.arange(tile) * side) // tile
            image = image[rows][:, rows]
        layer = image.astype(np.float64)
        tint = tints[index] if index < len(tints) else item_render.WHITE
        layer[..., :3] *= tint[:3]
        layer[..., 3] *= tint[3]
        alpha = layer[..., 3:4] / 255.0
        out[..., :3] = layer[..., :3] * alpha + out[..., :3] * (1.0 - alpha)
        out[..., 3:4] = 255.0 * alpha + out[..., 3:4] * (1.0 - alpha)
        drawn = True
    return np.clip(out, 0, 255).astype(np.uint8) if drawn else None


def model_icon(resolved, textures, tints, tile):
    """A model with real geometry, placed by its `gui` transform and shaded."""
    quads = item_render.model_quads(resolved["elements"], resolved["textures"], deref_texture)
    if not quads:
        return None
    pose = item_render.Pose()
    item_render.apply_transform(pose, resolved["display"].get("gui"))
    flat_light = resolved.get("gui_light") == "front"
    pixels = item_render.render(
        quads, pose, textures.get, tile, flat_light=flat_light,
        tints=(lambda index: tints[index] if index < len(tints) else item_render.WHITE))
    return pixels if pixels[..., 3].any() else None


def item_icon(assets, jar, textures, grass_map, item, tile, entity_models):
    """One item's slot picture, or the reason the game draws it some other way."""
    definition = f"assets/minecraft/items/{item}.json"
    if definition not in textures.names:
        return None, "no items/*.json definition"
    try:
        entry = json.loads(jar.read(definition))
    except ValueError:
        return None, "items/*.json is not readable json"
    node = entry.get("model")
    reference, tint_entries = default_branch(node)
    tints = tint_colours(tint_entries, grass_map)
    if reference is None:
        special = default_special(node)
        if special is None:
            return None, "its model definition names no model at all"
        base, renderer = special
        resolved = assets.resolve_model(base) if isinstance(base, str) else None
        return special_items.icon(item, renderer, resolved, entity_models, textures, tile,
                                  item_render)
    resolved = assets.resolve_model(reference)
    if resolved is None:
        return None, f"model {reference} is missing"
    if any(key.startswith("layer") for key in resolved["textures"]):
        icon = flat_icon(resolved, textures, tints, tile)
        return (icon, None) if icon is not None else (None, "its layers are empty")
    if resolved["elements"]:
        icon = model_icon(resolved, textures, tints, tile)
        return (icon, None) if icon is not None else (None, "its model draws nothing")
    return None, f"model {reference} has neither layers nor elements"


def main(args):
    positional = []
    tile = TILE
    entity_models = None
    rest = list(args)
    while rest:
        argument = rest.pop(0)
        if argument == "--tile":
            tile = int(rest.pop(0))
        elif argument == "--entity-models":
            entity_models = Path(rest.pop(0))
        elif argument.startswith("--"):
            sys.exit(f"unknown option {argument}")
        else:
            positional.append(argument)
    if len(positional) != 5:
        sys.exit(__doc__)
    jar_path, registries_path, items_path, protocol, out_path = positional
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

    assets = Assets(jar_path)
    tiles = []
    missing = []
    with zipfile.ZipFile(jar_path) as jar:
        textures = Textures(jar)
        with jar.open("assets/minecraft/textures/colormap/grass.png") as handle:
            grass_map = np.array(Image.open(handle).convert("RGB"), dtype=np.uint8)
        models = special_items.load_models(entity_models) if entity_models else None
        for name in by_id:
            icon, reason = item_icon(assets, jar, textures, grass_map, name, tile, models)
            if icon is None:
                tiles.append(b"\0" * (tile * tile * 4))
                if name != "air":
                    missing.append((name, reason))
            else:
                tiles.append(icon.tobytes())

    pixels = b"".join(tiles)
    # Names follow the pixels so the hud can say what is in hand without a
    # second file to keep in step.
    names = b"".join(struct.pack("<H", len(n.encode())) + n.encode() + struct.pack("<B", a)
                     for n, a in zip(by_id, armour))
    header = MAGIC + struct.pack("<6I", VERSION, protocol, tile, len(tiles), len(pixels),
                                 len(names))
    header += b"\0" * (align8(len(header)) - len(header))
    Path(out_path).write_bytes(header + pixels + names)

    print(f"{out_path}: {(len(header) + len(pixels)) / 1e6:.2f} MB")
    print(f"  {len(tiles)} items at {tile}x{tile}, {len(tiles) - len(missing) - 1} with an icon")
    print(f"  {len(missing)} without one")
    for name, reason in missing:
        print(f"    {name}: {reason}")


if __name__ == "__main__":
    main(sys.argv[1:])
