#!/usr/bin/env python3
"""Resolve every block state to its quads, from a client jar and a server report.

Usage: resolve_block_models.py <client.jar> <reports/blocks.json> [block ...]

Named blocks are described in detail; without them the whole registry is
resolved and summarised. Reads only assets the player already owns.
"""
import json
import sys
import zipfile
from pathlib import Path

FACES = ("down", "up", "north", "south", "west", "east")

# Air and a few markers carry no geometry at all.
MODEL_LESS = {"minecraft:air", "minecraft:cave_air", "minecraft:void_air",
              "minecraft:barrier", "minecraft:light",
              "minecraft:moving_piston", "minecraft:structure_void"}

# Fluids have no block model either, but they are drawn, so the quads are
# synthesised here instead of read from an asset.
FLUIDS = {
    "minecraft:water": ("block/water_still", 1),
    "minecraft:flowing_water": ("block/water_still", 1),
    "minecraft:bubble_column": ("block/water_still", 1),
    "minecraft:lava": ("block/lava_still", -1),
    "minecraft:flowing_lava": ("block/lava_still", -1),
}

FLUID_SURFACE = 14.0   # a source block's surface sits just below the top


def fluid_quads(name, properties):
    texture, tint = FLUIDS[name]
    level = int(properties.get("level", "0"))
    # Levels 1..7 are flowing and shallower; 8 and up are falling, so full.
    if level == 0 or level >= 8:
        height = FLUID_SURFACE
    else:
        height = FLUID_SURFACE * (8 - level) / 8.0

    quads = []
    for face in FACES:
        # Only the top is lowered, so the sides still meet their neighbours.
        quads.append({
            "face": face,
            "from": [0, 0, 0],
            "to": [16, height if face != "down" else 16, 16],
            "texture": texture,
            "cullface": face,
            "uv": None,
            "tintindex": tint if tint >= 0 else None,
            "rotation": (0, 0),
            "uvlock": False,
        })
    return quads


class Assets:
    def __init__(self, jar_path):
        self.zip = zipfile.ZipFile(jar_path)
        self.cache = {}

    def _read(self, path):
        try:
            return json.loads(self.zip.read(path))
        except KeyError:
            return None

    def blockstate(self, block_name):
        return self._read(f"assets/minecraft/blockstates/{block_name.split(':')[-1]}.json")

    def model(self, ref):
        if ref in self.cache:
            return self.cache[ref]
        path = f"assets/minecraft/models/{ref.split(':')[-1]}.json"
        model = self._read(path)
        self.cache[ref] = model
        return model

    def resolve_model(self, ref, depth=0):
        """Flatten the parent chain: textures merge child-first, elements are
        taken from the nearest ancestor that defines any."""
        if depth > 16:
            raise RuntimeError(f"model parent chain too deep at {ref}")
        model = self.model(ref)
        if model is None:
            return None

        textures = dict(model.get("textures", {}))
        elements = model.get("elements")
        parent = model.get("parent")

        if parent:
            inherited = self.resolve_model(parent, depth + 1)
            if inherited:
                merged = dict(inherited["textures"])
                merged.update(textures)
                textures = merged
                if elements is None:
                    elements = inherited["elements"]
        return {"textures": textures, "elements": elements or [], "parent": parent}


def deref_texture(name, textures):
    """Follow a texture slot to a real path.

    A reference normally starts with '#', but block/heavy_core.json names its
    own slot without one, and the game resolves that too."""
    for _ in range(16):
        if not isinstance(name, str):
            return name
        if name.startswith("#"):
            name = textures.get(name[1:])
        elif name in textures:
            name = textures[name]
        else:
            return name
    return None


def variant_matches(properties, predicate):
    """A multipart `when` clause: AND over keys, OR over `|`-separated values."""
    if "OR" in predicate:
        return any(variant_matches(properties, p) for p in predicate["OR"])
    if "AND" in predicate:
        return all(variant_matches(properties, p) for p in predicate["AND"])
    for key, wanted in predicate.items():
        actual = properties.get(key)
        if actual is None:
            return False
        if actual not in str(wanted).split("|"):
            return False
    return True


def pick_entries(blockstate, properties):
    """Return the model entries that apply to one state's properties."""
    if "variants" in blockstate:
        for key, entry in blockstate["variants"].items():
            if key == "":
                return [entry if isinstance(entry, dict) else entry[0]]
            wanted = dict(pair.split("=", 1) for pair in key.split(","))
            if all(properties.get(k) == v for k, v in wanted.items()):
                return [entry if isinstance(entry, dict) else entry[0]]
        return []

    entries = []
    for part in blockstate.get("multipart", []):
        if "when" not in part or variant_matches(properties, part["when"]):
            apply = part["apply"]
            entries.append(apply if isinstance(apply, dict) else apply[0])
    return entries


def quads_for_state(assets, block_name, properties):
    if block_name in MODEL_LESS:
        return [], None
    if block_name in FLUIDS:
        return fluid_quads(block_name, properties), None

    blockstate = assets.blockstate(block_name)
    if blockstate is None:
        return [], f"no blockstate file"

    entries = pick_entries(blockstate, properties)
    if not entries:
        # Multipart with nothing matching draws nothing, which is legal; a
        # variants map with no match is a hole in our property handling.
        if "multipart" in blockstate:
            return [], None
        return [], f"no variant matches {properties}"

    quads = []
    for entry in entries:
        model = assets.resolve_model(entry["model"])
        if model is None:
            return quads, f"model {entry['model']} is missing"

        rotation = (entry.get("x", 0), entry.get("y", 0))
        uvlock = entry.get("uvlock", False)
        for element in model["elements"]:
            for face_name, face in element.get("faces", {}).items():
                texture = deref_texture(face.get("texture"), model["textures"])
                if texture is None:
                    return quads, f"texture {face.get('texture')} unresolved"
                quads.append({
                    "face": face_name,
                    "from": element["from"],
                    "to": element["to"],
                    "texture": texture,
                    "cullface": face.get("cullface"),
                    "uv": face.get("uv"),
                    "tintindex": face.get("tintindex"),
                    "rotation": rotation,
                    "uvlock": uvlock,
                })
    return quads, None


def main(args):
    if len(args) < 2:
        sys.exit(__doc__)
    assets = Assets(args[0])
    blocks = json.loads(Path(args[1]).read_text())
    wanted = [a for a in args[2:] if not a.startswith('--')]
    show_all = '--all-empty' in args

    states = []
    for name, info in blocks.items():
        for state in info.get("states", []):
            states.append((state["id"], name, state.get("properties", {})))
    states.sort()

    if wanted:
        for state_id, name, properties in states:
            if name.split(":")[-1] not in wanted:
                continue
            quads, problem = quads_for_state(assets, name, properties)
            faces = sorted({q["face"] for q in quads})
            culled = sorted({q["cullface"] for q in quads if q["cullface"]})
            textures = sorted({q["texture"] for q in quads})
            print(f"{state_id:>6}  {name} {properties or ''}")
            print(f"        {len(quads)} quads, faces {faces}")
            print(f"        cullface {culled}")
            print(f"        textures {textures}")
            if problem:
                print(f"        PROBLEM: {problem}")
        return

    problems = {}
    empty_blocks = {}
    total_quads = 0
    resolved = 0
    modelless = 0
    for state_id, name, properties in states:
        quads, problem = quads_for_state(assets, name, properties)
        total_quads += len(quads)
        if problem:
            problems.setdefault(problem.split()[0] + " " + problem.split()[1], []).append(
                (name, properties, problem))
        elif quads:
            resolved += 1
        elif name in MODEL_LESS:
            modelless += 1
        else:
            empty_blocks.setdefault(name, 0)
            empty_blocks[name] += 1

    print(f"{len(states)} block states")
    print(f"  {resolved:>6} with geometry, {total_quads} quads "
          f"({total_quads / max(resolved, 1):.1f} on average)")
    print(f"  {modelless:>6} without a model (air and fluids)")
    print(f"  {sum(empty_blocks.values()):>6} empty without an error, across {len(empty_blocks)} blocks")
    print(f"  {sum(len(v) for v in problems.values()):>6} with a problem")

    if empty_blocks:
        print("\nempty, most common:")
        limit = None if show_all else 8
        for name, count in sorted(empty_blocks.items(), key=lambda kv: -kv[1])[:limit]:
            print(f"  {count:>6}  {name}")
    if problems:
        print("\nproblems:")
        for kind, items in sorted(problems.items(), key=lambda kv: -len(kv[1])):
            name, props, problem = items[0]
            print(f"  {len(items):>6}  {kind}   e.g. {name} {props}")


if __name__ == "__main__":
    main(sys.argv[1:])
