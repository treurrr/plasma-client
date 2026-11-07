#!/usr/bin/env python3
"""Slot icons for the items the game draws with an entity model.

A chest, a bed, a banner, a shulker box, a skull, a shield, a conduit, a
decorated pot and a copper golem statue have no block model at all: their item
definition names a `minecraft:special` renderer, and the game renders the same
model a block entity uses. The geometry comes out of the entity model table
`tools/export/export_models.py` asks the game for, the texture out of the jar,
and each renderer's own placement is below, so the icons are the game's rather
than a drawing of one.
"""
import math
import re

import numpy as np

import item_render

# `SpecialModelRenderers.ID_MAPPER`: the renderer each special model names,
# the model layer it bakes and where it puts it. Translation is in blocks,
# rotation in degrees about y, scale uniform.
#
# The layers come from `ModelLayers`; the placements from each renderer's own
# `submit`.
RENDERERS = {
    "minecraft:chest": {"layer": "chest", "parts": ("lid", "bottom", "lock")},
    "minecraft:shulker_box": {"layer": "shulker_box", "parts": ("lid", "base"),
                              "translate": (0.5, 0.5, 0.5), "scale": 0.9999},
    "minecraft:bed": {"layer": "bed_head", "second": "bed_foot"},
    "minecraft:banner": {"layer": "standing_banner", "parts": ("flag", "pole", "bar")},
    "minecraft:conduit": {"layer": "conduit_shell", "parts": ("shell",)},
    "minecraft:decorated_pot": {"layer": "decorated_pot_base"},
    "minecraft:head": {"layer": None},
    "minecraft:player_head": {"layer": None},
    "minecraft:shield": {"layer": "shield"},
    "minecraft:copper_golem_statue": {"layer": None},
}


def load_models(path):
    """The entity model table, as a map from layer name to its boxes.

    One layer is a list of parts; a part is a list of boxes in the game's own
    model space - pixels, y down - with where the part sits and how it turns.
    """
    if path is None or not path.is_file():
        return None
    layers = {}
    layer = None
    part = None
    for line in path.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if fields[0] == "model":
            layer = {"name": fields[1], "texture": (64.0, 32.0), "parts": {}}
            layers[fields[1]] = layer
        elif fields[0] == "texture" and layer is not None:
            layer["texture"] = (float(fields[1]), float(fields[2]))
        elif fields[0] == "part" and layer is not None:
            part = {"path": fields[1], "pose": [float(v) for v in fields[3:12]],
                    "boxes": []}
            layer["parts"][fields[1]] = part
        elif fields[0] == "box" and part is not None:
            part["boxes"].append([float(v) for v in fields[1:]])
    return layers


def icon(item, node, models, textures, tile):
    """One special item's picture, or why there is none yet."""
    del item, node, models, textures, tile
    return None, "an entity model the bake does not build yet"
