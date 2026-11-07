#!/usr/bin/env python3
"""The items the game draws with a block entity's model, and why they have no
slot icon yet.

A chest, a bed, a banner, a shulker box, a skull, a shield, a conduit, a
decorated pot and a copper golem statue have no block model at all: their
item definition names a `minecraft:special` renderer and the base model it
names carries only the display transforms. The game renders the same model a
block entity uses, so an icon needs three things this bake does not have:

  - the geometry, which is in the entity model table
    `tools/export/export_models.py` already writes beside the item icons;
  - the texture, which is under `textures/entity/` in the jar rather than
    `textures/block/`;
  - each renderer's own placement, which is a handful of lines in its
    `*SpecialRenderer.submit` and the block entity renderer it delegates to.

None of it is hard and none of it is here. `tools/item_render.py` already
rasterises boxes with a texture window, which is what a model part is, so
what is missing is the parsing and the nine placements.
"""

# `SpecialModelRenderers.ID_MAPPER`: the special models the game knows, with
# the model layer each one bakes, for whoever fills these in.
RENDERERS = {
    "minecraft:chest": "chest",
    "minecraft:shulker_box": "shulker_box",
    "minecraft:bed": "bed_head and bed_foot",
    "minecraft:banner": "standing_banner and standing_banner#flag",
    "minecraft:conduit": "conduit#shell",
    "minecraft:decorated_pot": "decorated_pot_base and decorated_pot_sides",
    "minecraft:head": "the skull of the kind it names",
    "minecraft:player_head": "player_head",
    "minecraft:shield": "shield",
    "minecraft:copper_golem_statue": "copper_golem",
}


def load_models(path):
    del path
    return None


def icon(item, node, models, textures, tile):
    """One special item's picture, or why there is none yet."""
    del item, models, textures, tile
    kinds = sorted(k for k in RENDERERS if node is not None and k in str(node))
    which = kinds[0] if kinds else "a block entity"
    return None, f"the game draws it with {which}'s model, which this bake does not build"
