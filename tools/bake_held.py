#!/usr/bin/env python3
"""Bake the geometry of every item as the hand holds it.

Usage: bake_held.py <client.jar> <reports/registries.json> <protocol> <out.plhd>

A slot icon is a picture; an item in the hand is a model, seen from any angle
and lit by the world. This writes, per item, the quads of the model the game
resolves it to - a block's own model, or the sprite `ItemModelGenerator`
extrudes into a sixteenth of a block of relief - with the first person display
transforms the model gives either hand, and its own small texture array, so
the client needs nothing else to draw what is in its hand.
"""
import json
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import item_render
from bake_items import Textures, default_branch, tint_colours
from resolve_block_models import Assets, deref_texture
from PIL import Image

MAGIC = b"PLHD"
VERSION = 3
TILE = 16
# A bow and a crossbow swap models as they are drawn. The first stage is the
# item at rest, the rest are what `minecraft:bow/pull` dispatches to.
STAGES = 4

# `ItemModelGenerator`: the sprite sits a sixteenth of a block thick, and its
# side faces are pulled a tenth of a pixel in so they do not z-fight the front.
MIN_Z = 7.5
MAX_Z = 8.5
UV_SHRINK = 0.1
SOUTH_UVS = [0.0, 0.0, 16.0, 16.0]
NORTH_UVS = [16.0, 0.0, 0.0, 16.0]
# `ItemModelGenerator.SideDirection`: left faces east, right faces west.
SIDES = (("up", "up", 0, -1), ("down", "down", 0, 1), ("left", "east", -1, 0),
         ("right", "west", 1, 0))


def side_elements(alpha, layer, slot):
    """`ItemModelGenerator.createSideElements`: a wall at every edge of the sprite."""
    height, width = alpha.shape
    scale_x = 16.0 / width
    scale_y = 16.0 / height
    opaque = alpha > 0

    faces = []
    for name, direction, step_x, step_y in SIDES:
        inside = np.zeros_like(opaque)
        xs = slice(max(step_x, 0), width + min(step_x, 0))
        ys = slice(max(step_y, 0), height + min(step_y, 0))
        shifted_xs = slice(max(-step_x, 0), width + min(-step_x, 0))
        shifted_ys = slice(max(-step_y, 0), height + min(-step_y, 0))
        inside[ys, xs] = ~opaque[shifted_ys, shifted_xs]
        # Outside the sprite counts as transparent, so an edge pixel walls off.
        if step_y < 0: inside[0, :] = True
        if step_y > 0: inside[height - 1, :] = True
        if step_x < 0: inside[:, 0] = True
        if step_x > 0: inside[:, width - 1] = True
        for y, x in zip(*np.nonzero(opaque & inside)):
            faces.append((name, direction, int(x), int(y)))

    elements = []
    for name, direction, x, y in faces:
        u0 = x + UV_SHRINK
        u1 = x + 1.0 - UV_SHRINK
        if name in ("up", "down"):
            v0 = y + UV_SHRINK
            v1 = y + 1.0 - UV_SHRINK
        else:
            v0 = y + 1.0 - UV_SHRINK
            v1 = y + UV_SHRINK
        left, top, right, bottom = float(x), float(y), float(x), float(y)
        if name == "up":
            right += 1
        elif name == "down":
            right += 1
            top += 1
            bottom += 1
        elif name == "left":
            bottom += 1
        else:
            left += 1
            right += 1
            bottom += 1
        left *= scale_x
        right *= scale_x
        top = 16.0 - top * scale_y
        bottom = 16.0 - bottom * scale_y
        face = {direction: {"texture": f"#layer{layer}", "tintindex": slot,
                            "uv": [u0 * scale_x, v0 * scale_x, u1 * scale_y, v1 * scale_y]}}
        if name == "up":
            element = ([left, top, MIN_Z], [right, top, MAX_Z])
        elif name == "down":
            element = ([left, bottom, MIN_Z], [right, bottom, MAX_Z])
        elif name == "left":
            element = ([left, top, MIN_Z], [left, bottom, MAX_Z])
        else:
            element = ([right, top, MIN_Z], [right, bottom, MAX_Z])
        elements.append({"from": element[0], "to": element[1], "faces": face})
    return elements


def generated_elements(resolved, textures):
    """`ItemModelGenerator.bake`: a flat sprite given a sixteenth of relief."""
    elements = []
    for layer in range(5):
        slot = f"layer{layer}"
        if slot not in resolved["textures"]:
            break
        path = deref_texture(f"#{slot}", resolved["textures"])
        image = textures.get(path) if isinstance(path, str) else None
        if image is None:
            break
        elements.append({
            "from": [0.0, 0.0, MIN_Z], "to": [16.0, 16.0, MAX_Z],
            "faces": {
                "south": {"texture": f"#{slot}", "tintindex": layer, "uv": SOUTH_UVS},
                "north": {"texture": f"#{slot}", "tintindex": layer, "uv": NORTH_UVS},
            },
        })
        elements.extend(side_elements(image[..., 3], layer, layer))
    return elements


def pull_models(node, depth=0):
    """The models a drawn bow or crossbow passes through, in order.

    `ItemModelResolver` picks them by `minecraft:bow/pull`, which a bare stack
    does not have; the hand does, so the stages are kept beside the item.
    """
    if depth > 12 or not isinstance(node, dict):
        return []
    property = str(node.get("property", ""))
    if node.get("type") == "minecraft:range_dispatch" and (
            "pull" in property or property == "minecraft:use_duration"):
        stages = [(default_branch(node.get("fallback"))[0], 0.0)]
        for case in sorted(node.get("entries") or [], key=lambda e: e.get("threshold", 0.0)):
            stages.append((default_branch(case.get("model"))[0], float(case.get("threshold", 0.0))))
        return [s for s in stages if s[0]]
    for key in ("on_true", "on_false", "fallback", "model"):
        found = pull_models(node.get(key), depth + 1)
        if found:
            return found
    for key in ("cases", "entries"):
        for case in node.get(key) or []:
            found = pull_models(case.get("model") if isinstance(case, dict) else None, depth + 1)
            if found:
                return found
    return []


def display_of(resolved, context):
    transform = resolved["display"].get(context)
    if transform is None:
        return (0.0,) * 3 + (0.0,) * 3 + (1.0, 1.0, 1.0) + (0,)
    rotation = transform.get("rotation", [0.0, 0.0, 0.0])
    translation = [min(max(v * 0.0625, -5.0), 5.0)
                   for v in transform.get("translation", [0.0, 0.0, 0.0])]
    scale = [min(max(v, -4.0), 4.0) for v in transform.get("scale", [1.0, 1.0, 1.0])]
    return tuple(rotation) + tuple(translation) + tuple(scale) + (1,)


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

    assets = Assets(jar_path)
    layers = {}
    layer_pixels = []
    resized = []

    def layer_for(reference):
        if reference in layers:
            return layers[reference]
        image = textures.get(reference)
        if image is None:
            return -1
        if image.shape[0] != TILE or image.shape[1] != TILE:
            resized.append(reference)
            rows = (np.arange(TILE) * image.shape[0]) // TILE
            columns = (np.arange(TILE) * image.shape[1]) // TILE
            image = image[rows][:, columns]
        layers[reference] = len(layer_pixels)
        layer_pixels.append(image.astype(np.uint8).tobytes())
        return layers[reference]

    index = bytearray()
    quads = bytearray()
    quad_count = 0
    missing = []
    with zipfile.ZipFile(jar_path) as jar:
        textures = Textures(jar)
        with jar.open("assets/minecraft/textures/colormap/grass.png") as handle:
            grass_map = np.array(Image.open(handle).convert("RGB"), dtype=np.uint8)
        for name in by_id:
            right = (0.0,) * 9 + (0,)
            left = right
            ranges = [(quad_count, 0)] * STAGES
            thresholds = [2.0] * (STAGES - 1)
            definition = f"assets/minecraft/items/{name}.json"
            if definition in textures.names:
                entry = json.loads(jar.read(definition))
                node = entry.get("model")
                reference, tint_entries = default_branch(node)
                tints = tint_colours(tint_entries, grass_map)
                drawn = pull_models(node)[:STAGES - 1]
                stages = [reference] + [model for model, _ in drawn]
                thresholds = [t for _, t in drawn] + [2.0] * (STAGES - 1 - len(drawn))
                resolved_first = assets.resolve_model(reference) if reference else None
                if resolved_first is not None:
                    right = display_of(resolved_first, "firstperson_righthand")
                    left = display_of(resolved_first, "firstperson_lefthand")
                    third_right = display_of(resolved_first, "thirdperson_righthand")
                    third_left = display_of(resolved_first, "thirdperson_lefthand")
                built = []
                for stage, model_name in enumerate(stages):
                    begin = quad_count
                    resolved = assets.resolve_model(model_name) if model_name else None
                    if resolved is not None:
                        if any(key.startswith("layer") for key in resolved["textures"]):
                            elements = generated_elements(resolved, textures)
                        else:
                            elements = resolved["elements"]
                        for quad in item_render.model_quads(elements, resolved["textures"],
                                                            deref_texture):
                            layer = layer_for(quad["texture"])
                            if layer < 0:
                                continue
                            tint = (tints[quad["tint"]] if 0 <= quad["tint"] < len(tints)
                                    else item_render.WHITE)
                            packed = 0
                            for channel in range(4):
                                packed |= int(round(float(tint[channel]) * 255.0)) << (channel * 8)
                            for corner in range(4):
                                quads.extend(struct.pack(
                                    "<5f", *quad["corners"][corner], *quad["uv"][corner]))
                            quads.extend(struct.pack("<3fII", *quad["normal"], layer, packed))
                            quad_count += 1
                    built.append((begin, quad_count - begin))
                # A stage the item has no model for falls back to the one before
                # it, so a half drawn bow is never an empty hand.
                for stage in range(STAGES):
                    ranges[stage] = built[stage] if stage < len(built) else built[-1]
                if built[0][1] == 0 and name != "air":
                    missing.append(name)
            elif name != "air":
                missing.append(name)
            for begin, count in ranges:
                index.extend(struct.pack("<2I", begin, count))
            index.extend(struct.pack("<3f", *thresholds[:STAGES - 1]))
            index.extend(struct.pack("<9fI", *right))
            index.extend(struct.pack("<9fI", *left))
            index.extend(struct.pack("<9fI", *third_right))
            index.extend(struct.pack("<9fI", *third_left))

    pixels = b"".join(layer_pixels)
    header = MAGIC + struct.pack("<8I", VERSION, protocol, TILE, len(layer_pixels),
                                 len(by_id), len(pixels), len(index), STAGES)
    header += b"\0" * (((len(header) + 7) & ~7) - len(header))
    Path(out_path).write_bytes(header + pixels + bytes(index) + bytes(quads))

    print(f"{out_path}: {(len(header) + len(pixels) + len(index) + len(quads)) / 1e6:.2f} MB")
    print(f"  {len(by_id)} items, {quad_count} quads, {len(layer_pixels)} textures")
    print(f"  {len(missing)} with no model to hold, for example {', '.join(missing[:5])}")
    if resized:
        print(f"  {len(resized)} textures were not {TILE} square, e.g. {resized[0]}")


if __name__ == "__main__":
    main(sys.argv[1:])
