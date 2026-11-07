#!/usr/bin/env python3
"""Bake the block states the game draws with a renderer of their own.

Usage: bake_block_entities.py <reports/blocks.json> <blocks-with-entities.txt> <out.txt>

A chest, a sign, a bed, a banner, a skull, a shulker box, a bell, a conduit, a
decorated pot, an enchanting table, a lectern and a copper golem statue are
drawn by a model of their own on top of (or instead of) the block model in the
chunk mesh. Which blocks those are comes from the game itself, through
tools/export/ExportBlockEntities.java; which model layer and which texture each
state wants is `BlockEntityRenderers` and the renderer it names, worked out
here once so a frame only reads a row.

The second argument is that exporter's output: one `<block>\t<block entity
type>` per line.
"""
import json
import sys
from pathlib import Path

HEADER = """\
# Block states with a renderer of their own, written by
# tools/bake_block_entities.py. One line per state:
#
#   <state> <kind> model=<layer> tex=<under textures/entity/> [key=value ...]
#
#   facing   the Direction's 3d value, 0 down to 5 east
#   rot      the sixteenth of a turn a free-standing block was placed at
#   variant  chest 0 single 1 left 2 right; sign 0 standing 1 wall; hanging
#            sign 0 wall 1 ceiling 2 ceiling_middle; bed 0 foot 1 head; banner
#            0 standing 1 wall; bell 0 floor 1 ceiling 2 single_wall 3
#            double_wall; statue 0 standing 1 sitting 2 star 3 running
#   colour   the dye colour a banner's base layer is tinted with
#   flag     1 when a lectern has its book
"""

# `BlockEntityRenderers`: the types with a renderer, and what this draws them
# with. The ones left out carry no model of their own here yet.
KINDS = {
    "minecraft:chest": "chest",
    "minecraft:ender_chest": "chest",
    "minecraft:trapped_chest": "chest",
    "minecraft:sign": "sign",
    "minecraft:hanging_sign": "hanging_sign",
    "minecraft:bed": "bed",
    "minecraft:banner": "banner",
    "minecraft:skull": "skull",
    "minecraft:shulker_box": "shulker_box",
    "minecraft:bell": "bell",
    "minecraft:conduit": "conduit",
    "minecraft:decorated_pot": "decorated_pot",
    "minecraft:enchanting_table": "enchanting_table",
    "minecraft:lectern": "lectern",
    "minecraft:copper_golem_statue": "copper_golem_statue",
}

DIRECTIONS = {"down": 0, "up": 1, "north": 2, "south": 3, "west": 4, "east": 5}

COLOURS = ["white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
           "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"]

# `Sheets.chooseMaterial`: which chest picture a block wears. Waxing changes
# nothing a renderer can see.
CHEST_TEXTURES = {
    "chest": "normal", "trapped_chest": "trapped", "ender_chest": "ender",
    "copper_chest": "copper", "exposed_copper_chest": "copper_exposed",
    "weathered_copper_chest": "copper_weathered", "oxidized_copper_chest": "copper_oxidized",
}

# `SkullBlockRenderer.createModel` and `SKIN_BY_TYPE`.
SKULLS = {
    "skeleton": ("skeleton_skull", "skeleton/skeleton"),
    "wither_skeleton": ("wither_skeleton_skull", "skeleton/wither_skeleton"),
    "player": ("player_head", "player/slim/steve"),
    "zombie": ("zombie_head", "zombie/zombie"),
    "creeper": ("creeper_head", "creeper/creeper"),
    "dragon": ("dragon_skull", "enderdragon/dragon"),
    "piglin": ("piglin_head", "piglin/piglin"),
}

STATUE_POSES = {"standing": 0, "sitting": 1, "star": 2, "running": 3}
STATUE_LAYERS = ["copper_golem", "copper_golem_sitting", "copper_golem_star",
                 "copper_golem_running"]
BELL_ATTACHMENTS = {"floor": 0, "ceiling": 1, "single_wall": 2, "double_wall": 3}


def strip_copper(name):
    """A copper block's weathering, and what is left of its name."""
    name = name.removeprefix("waxed_")
    for level in ("exposed_", "weathered_", "oxidized_"):
        if name.startswith(level):
            return level.rstrip("_"), name[len(level):]
    return "unaffected", name


def coloured(name, suffix):
    """A dyed block's colour and the colourless name under it."""
    for index, colour in enumerate(COLOURS):
        if name == f"{colour}_{suffix}":
            return index, colour
    return -1, None


def row(kind, block, properties):
    """One state's renderer, or None when this does not draw its kind yet."""
    fields = {}
    facing = DIRECTIONS.get(properties.get("facing"), -1)
    if kind == "chest":
        weather, bare = strip_copper(block)
        if bare in ("copper_chest",):
            bare = {"unaffected": "copper_chest", "exposed": "exposed_copper_chest",
                    "weathered": "weathered_copper_chest",
                    "oxidized": "oxidized_copper_chest"}[weather]
        texture = CHEST_TEXTURES[bare]
        kind_of = properties.get("type", "single")
        variant = {"single": 0, "left": 1, "right": 2}[kind_of]
        # `Sheets.chooseMaterial`: the ender chest is one picture whole; every
        # other double chest reads half of a wider one.
        if variant and texture != "ender":
            texture += "_left" if variant == 1 else "_right"
        model = ["chest", "double_chest_left", "double_chest_right"][variant]
        fields = {"model": model, "tex": "chest/" + texture, "facing": facing,
                  "variant": variant}
    elif kind == "sign":
        wall = block.endswith("_wall_sign")
        wood = block.removesuffix("_wall_sign" if wall else "_sign")
        fields = {"model": f"sign/{'wall' if wall else 'standing'}/{wood}",
                  "tex": "signs/" + wood, "variant": 1 if wall else 0}
        if wall:
            fields["facing"] = facing
        else:
            fields["rot"] = int(properties.get("rotation", "0"))
    elif kind == "hanging_sign":
        wall = block.endswith("_wall_hanging_sign")
        wood = block.removesuffix("_wall_hanging_sign" if wall
                                  else "_hanging_sign")
        # `HangingSignRenderer.AttachmentType.byBlockState`.
        if wall:
            variant, attachment = 0, "wall"
        elif properties.get("attached") == "true":
            variant, attachment = 2, "ceiling_middle"
        else:
            variant, attachment = 1, "ceiling"
        fields = {"model": f"hanging_sign/{wood}/{attachment}",
                  "tex": "signs/hanging/" + wood, "variant": variant}
        if wall:
            fields["facing"] = facing
        else:
            fields["rot"] = int(properties.get("rotation", "0"))
    elif kind == "bed":
        colour, name = coloured(block, "bed")
        head = properties.get("part") == "head"
        fields = {"model": "bed_head" if head else "bed_foot", "tex": "bed/" + name,
                  "facing": facing, "variant": 1 if head else 0}
    elif kind == "banner":
        wall = "_wall_banner" in block
        colour, _ = coloured(block, "wall_banner" if wall else "banner")
        fields = {"model": "wall_banner" if wall else "standing_banner",
                  "tex": "banner_base", "variant": 1 if wall else 0, "colour": colour}
        if wall:
            fields["facing"] = facing
        else:
            fields["rot"] = int(properties.get("rotation", "0"))
    elif kind == "skull":
        wall = "_wall_" in block
        bare = block.replace("_wall_", "_")
        name = bare.removesuffix("_skull").removesuffix("_head")
        if name not in SKULLS:
            return None
        model, texture = SKULLS[name]
        fields = {"model": model, "tex": texture, "variant": 1 if wall else 0}
        if wall:
            fields["facing"] = facing
        else:
            fields["rot"] = int(properties.get("rotation", "0"))
    elif kind == "shulker_box":
        colour, name = coloured(block, "shulker_box")
        fields = {"model": "shulker_box",
                  "tex": "shulker/shulker" if colour < 0 else f"shulker/shulker_{name}",
                  "facing": DIRECTIONS.get(properties.get("facing"), 1)}
    elif kind == "bell":
        fields = {"model": "bell", "tex": "bell/bell_body",
                  "variant": BELL_ATTACHMENTS[properties.get("attachment", "floor")],
                  "facing": facing}
    elif kind == "conduit":
        fields = {"model": "conduit#shell", "tex": "conduit/base"}
    elif kind == "decorated_pot":
        fields = {"model": "decorated_pot_base", "tex": "decorated_pot/decorated_pot_base",
                  "facing": facing}
    elif kind == "enchanting_table":
        fields = {"model": "book", "tex": "enchanting_table_book"}
    elif kind == "lectern":
        fields = {"model": "book", "tex": "enchanting_table_book", "facing": facing,
                  "flag": 1 if properties.get("has_book") == "true" else 0}
    elif kind == "copper_golem_statue":
        weather, _ = strip_copper(block)
        prefix = "" if weather == "unaffected" else weather + "_"
        pose = STATUE_POSES[properties.get("copper_golem_pose", "standing")]
        fields = {"model": STATUE_LAYERS[pose],
                  "tex": f"copper_golem/{prefix}copper_golem", "facing": facing,
                  "variant": pose}
    else:
        return None
    return fields


def main(args):
    if len(args) != 3:
        sys.exit(__doc__)
    blocks = json.loads(Path(args[0]).read_text())
    types = {}
    for line in Path(args[1]).read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            block, kind = line.split("\t")
            types[block] = kind

    lines = []
    counts = {}
    for name, info in blocks.items():
        kind = KINDS.get(types.get(name))
        if kind is None:
            continue
        bare = name.removeprefix("minecraft:")
        for state in info.get("states", []):
            fields = row(kind, bare, state.get("properties", {}))
            if fields is None:
                continue
            parts = " ".join(f"{key}={value}" for key, value in fields.items())
            lines.append((state["id"], f"{state['id']} {kind} {parts}"))
            counts[kind] = counts.get(kind, 0) + 1
    lines.sort()

    out = Path(args[2])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(HEADER + "\n".join(line for _, line in lines) + "\n")
    print(f"{out}: {len(lines)} block states")
    for kind, count in sorted(counts.items()):
        print(f"  {count:>6} {kind}")


if __name__ == "__main__":
    main(sys.argv[1:])
