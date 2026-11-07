#!/usr/bin/env python3
"""The other half of SpecialItemOracle: our own placement of the same models.

The `gui` display transform comes out of the same model files the oracle reads
and goes through our port of `ItemTransform.apply`; the vertices come out of
the entity model table and the placements in tools/special_items.py. Positions
are sorted the way the oracle sorts them, because the order parts hang in is a
hash map's and means nothing.
"""
import os
import sys
from pathlib import Path

import numpy as np

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / "oracle"))

import entity_models  # noqa: E402
import item_render  # noqa: E402
import joml  # noqa: E402
import special_items  # noqa: E402
from resolve_block_models import Assets  # noqa: E402
from run import reference_jar  # noqa: E402

TRANSFORMS = [
    ("chest", "item/chest"),
    ("bed", "item/white_bed"),
    ("banner", "item/template_banner"),
    ("shulker_box", "item/white_shulker_box"),
    ("skull", "item/template_skull"),
    ("dragon_head", "item/dragon_head"),
    ("conduit", "item/conduit"),
    ("decorated_pot", "item/decorated_pot"),
    ("statue", "item/template_copper_golem_statue"),
    ("shield", "item/shield"),
]

PLACEMENTS = [
    ("chest", {"type": "minecraft:chest", "texture": "minecraft:normal"}),
    ("bed", {"type": "minecraft:bed", "texture": "minecraft:white"}),
    ("banner", {"type": "minecraft:banner", "color": "white"}),
    ("shulker_box", {"type": "minecraft:shulker_box", "texture": "minecraft:shulker_white"}),
] + [(f"skull_{kind}", {"type": "minecraft:head", "kind": kind})
     for kind in ("skeleton", "wither_skeleton", "player", "zombie", "creeper", "piglin",
                  "dragon")] + [
    ("conduit", {"type": "minecraft:conduit"}),
    ("decorated_pot", {"type": "minecraft:decorated_pot"}),
    ("statue", {"type": "minecraft:copper_golem_statue", "pose": "standing",
                "texture": "minecraft:textures/entity/copper_golem/copper_golem.png"}),
    ("shield", {"type": "minecraft:shield"}),
]


def bits(value):
    return int(np.float32(value).view(np.int32))


def data_path(name):
    base = os.environ.get("XDG_DATA_HOME") or (Path.home() / ".local" / "share")
    return Path(base) / "plasma-client" / "data" / name


LAYERS = ["chest", "bed_head", "bed_foot", "standing_banner", "standing_banner#flag",
          "shulker_box", "skeleton_skull", "wither_skeleton_skull", "player_head",
          "zombie_head", "creeper_head", "piglin_head", "dragon_skull", "conduit#shell",
          "decorated_pot_base", "decorated_pot_sides", "copper_golem", "shield"]


def main():
    assets = Assets(reference_jar())
    for _, reference in TRANSFORMS:
        pose = joml.Pose()
        special_items.item_transform(pose, assets.resolve_model(reference)["display"].get("gui"))
        print(",".join(str(bits(pose.pose.m[column][row]))
                       for column in range(4) for row in range(4)))

    models = entity_models.load(data_path("entity_models-774.txt"))
    for _, node in PLACEMENTS:
        placed = []
        special_items.quads(node, models, joml.Pose(), placed)
        found = sorted(f"{bits(c[0])},{bits(c[1])},{bits(c[2])}"
                       for quad in placed if quad["pass"] == "geometry"
                       for c in quad["corners"])
        print(len(found))
        for line in found:
            print(line)

    for layer in LAYERS:
        found = []
        for part in models[layer].by_path.values():
            for box in part.boxes:
                for quad in entity_models.box_quads(box):
                    facing = ",".join(str(round(v * 127)) for v in quad["normal"])
                    for corner, uv in zip(quad["corners"], quad["uv"]):
                        found.append(f"{bits(corner[0])},{bits(corner[1])},{bits(corner[2])},"
                                     f"{bits(uv[0])},{bits(uv[1])},{facing}")
        found.sort()
        print(len(found))
        for line in found:
            print(line)


if __name__ == "__main__":
    main()
