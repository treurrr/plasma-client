#!/usr/bin/env python3
"""Bake the sprite set behind every particle the game can spawn.

Usage: bake_particle_sprites.py <client.jar> <out.txt>

A particle's look is a list of textures, not one: `SpriteSet.get(age, lifetime)`
walks it as the particle ages and `SpriteSet.get(random)` picks one at random.
Each `assets/minecraft/particles/<name>.json` names that list.

Out come two sections,
    T <texture>
    S <set> <index> <index> ...
the T lines every texture any set uses, once each, in the order bake_atlas.py
appends them to the array behind the block textures, and the S lines each set's
textures as indices into them. A set's layer is therefore the block texture
count plus the index, with nothing looked up by name on the gpu. Sharing one
layer between the sets that name the same texture is what keeps the array
inside the layer count a gpu promises.
"""
import json
import sys
import zipfile
from pathlib import Path


def qualify(name):
    """A texture as the jar names it, with its folder and namespace filled in."""
    namespace, _, path = name.rpartition(":")
    namespace = namespace or "minecraft"
    if "/" not in path:
        path = f"particle/{path}"
    return f"{namespace}:{path}"


def main(args):
    if len(args) != 2:
        sys.exit(__doc__)
    jar, out = args[0], Path(args[1])
    archive = zipfile.ZipFile(jar)
    present = set(archive.namelist())

    def exists(texture):
        return f"assets/minecraft/textures/{texture.split(':')[-1]}.png" in present

    order = []
    index_of = {}
    sets = []
    for entry in sorted(archive.namelist()):
        if not entry.startswith("assets/minecraft/particles/") or not entry.endswith(".json"):
            continue
        try:
            textures = json.loads(archive.read(entry)).get("textures") or []
        except ValueError:
            continue
        indices = []
        for texture in textures:
            if not isinstance(texture, str):
                continue
            texture = qualify(texture)
            if not exists(texture):
                continue
            if texture not in index_of:
                index_of[texture] = len(order)
                order.append(texture)
            indices.append(index_of[texture])
        if indices:
            sets.append((f"minecraft:{Path(entry).stem}", indices))

    lines = [f"T {texture}\n" for texture in order]
    lines += [f"S {name} {' '.join(map(str, indices))}\n" for name, indices in sets]
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("".join(lines))
    print(f"{out}: {len(sets)} sprite sets over {len(order)} textures")


if __name__ == "__main__":
    main(sys.argv[1:])
