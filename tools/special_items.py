#!/usr/bin/env python3
"""The items the game draws with a block entity's model, not a block model.

A chest, a bed, a banner, a shulker box, a skull, a shield, a conduit, a
decorated pot and a copper golem statue have no block model at all: their item
definition names a `minecraft:special` renderer and the base model it names
carries only the display transforms. Each of those renderers is a handful of
lines - a placement, a layer and a texture - in front of the block entity
renderer the game uses in the world, so a slot icon is the same bake as any
other model once the geometry comes from the entity model table instead of
`models/block/`.

The textures are under `textures/entity/` in the jar; the game stitches them
into little atlases of their own (chest, beds, banner patterns) and reads a
sprite out of each, so a model's uvs land on the whole picture either way.
"""
import math

import numpy as np

import entity_models
import joml
from entity_models import Rig

# `SpecialModelRenderers.ID_MAPPER`: the special models the game knows, with
# the model layer each one bakes.
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

# `DyeColor`: the colour a dyed pattern layer is multiplied by.
DYE_COLOURS = {
    "white": 0xF9FFFE, "orange": 0xF9801D, "magenta": 0xC74EBD, "light_blue": 0x3AB3DA,
    "yellow": 0xFED83D, "lime": 0x80C71F, "pink": 0xF38BAA, "gray": 0x474F52,
    "light_gray": 0x9D9D97, "cyan": 0x169C9C, "purple": 0x8932B8, "blue": 0x3C44AA,
    "brown": 0x835432, "green": 0x5E7C16, "red": 0xB02E26, "black": 0x1D1D21,
}

# `SkullBlockRenderer.createModel`, `SkullBlockRenderer.SKIN_BY_TYPE`: the layer
# and the skin of each kind of head. The player's is the default skin, which is
# `DefaultPlayerSkin.DEFAULT_SKINS[6]`.
SKULLS = {
    "skeleton": ("skeleton_skull", "entity/skeleton/skeleton"),
    "wither_skeleton": ("wither_skeleton_skull", "entity/skeleton/wither_skeleton"),
    "player": ("player_head", "entity/player/slim/steve"),
    "zombie": ("zombie_head", "entity/zombie/zombie"),
    "creeper": ("creeper_head", "entity/creeper/creeper"),
    "dragon": ("dragon_skull", "entity/enderdragon/dragon"),
    "piglin": ("piglin_head", "entity/piglin/piglin"),
}

# `CopperGolemStatueSpecialRenderer.Unbaked.getModel`: a pose names a layer.
STATUE_LAYERS = {"standing": "copper_golem", "sitting": "copper_golem_sitting",
                 "star": "copper_golem_star", "running": "copper_golem_running"}

HALF_PI = math.pi / 2.0


def load_models(path):
    return entity_models.load(path) if path is not None else None


def _path(identifier, default=""):
    """An identifier's path, which is all any of these renderers reads."""
    if not isinstance(identifier, str):
        return default
    return identifier.split(":")[-1]


def _texture(identifier, default=""):
    """A texture named as a whole resource path rather than a sprite name."""
    name = _path(identifier, default)
    return name.removeprefix("textures/").removesuffix(".png")


def _colour(name):
    packed = DYE_COLOURS.get(name, 0xFFFFFF)
    return np.array([(packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF, 255],
                    dtype=np.float64) / 255.0


_rot_x = joml.quaternion_x
_rot_y = joml.quaternion_y
_rot_z = joml.quaternion_z


# `Direction.getRotation`: how a face's own space is turned into the world's.
def _direction_rotation(name):
    if name == "down":
        return joml.quaternion_x(math.pi)
    if name == "up":
        return (0.0, 0.0, 0.0, 1.0)
    if name == "south":
        return joml.quaternion_x(HALF_PI)
    turn = {"north": math.pi, "west": HALF_PI, "east": -HALF_PI}[name]
    return joml.quaternion_xyz(HALF_PI, 0.0, turn)


# `Direction.getYRot`: the turn a horizontal face stands at.
Y_ROT = {"north": 180.0, "south": 0.0, "west": 90.0, "east": -90.0}


def item_transform(pose, transform, left_handed=False):
    """`ItemTransform.apply`: where a display context puts the model, to the bit.

    The same as the one tools/item_render.py uses for a block model, except
    that every step here is single precision, because an oracle compares these
    corners with the game's own.
    """
    if transform is None:
        pose.translate(-0.5, -0.5, -0.5)
        return
    rotation = transform.get("rotation", [0.0, 0.0, 0.0])
    translation = [min(max(joml.f32(joml.f32(v) * 0.0625), -5.0), 5.0)
                   for v in transform.get("translation", [0.0, 0.0, 0.0])]
    scale = [min(max(joml.f32(v), -4.0), 4.0) for v in transform.get("scale", [1.0, 1.0, 1.0])]
    sign = -1.0 if left_handed else 1.0
    pose.translate(sign * translation[0], translation[1], translation[2])
    pose.rotate(joml.quaternion_xyz(_radians(rotation[0]), _radians(sign * rotation[1]),
                                    _radians(sign * rotation[2])))
    pose.scale(*scale)
    pose.translate(-0.5, -0.5, -0.5)


def _radians(degrees):
    """`Mth.DEG_TO_RAD` through a float, which is what the deserialiser leaves."""
    return joml.f32(joml.f32(degrees) * joml.f32(math.pi / 180.0))


def chest(node, models, pose, out):
    """`ChestSpecialRenderer.submit`: the single chest, as open as it says."""
    openness = float(node.get("openness", 0.0))
    rig = Rig(models["chest"])
    # `ChestModel.setupAnim`: the lid and its lock turn back together.
    rig.set("lid", x_rot=-joml.f32(openness * joml.f32(HALF_PI)))
    rig.set("lock", x_rot=-joml.f32(openness * joml.f32(HALF_PI)))
    rig.emit(pose, "entity/chest/" + _path(node.get("texture"), "normal"), out)


def shulker_box(node, models, pose, out):
    """`ShulkerBoxRenderer.prepareModel`: stood on the face it names, lid shut."""
    openness = float(node.get("openness", 0.0))
    pose.translate(0.5, 0.5, 0.5)
    pose.scale(0.9995, 0.9995, 0.9995)
    pose.rotate(_direction_rotation(_path(node.get("orientation"), "up")))
    pose.scale(1.0, -1.0, -1.0)
    pose.translate(0.0, -1.0, 0.0)
    rig = Rig(models["shulker_box"])
    # `ShulkerBoxRenderer.ShulkerBoxModel.setupAnim`: the lid lifts and turns.
    rig.set("lid", x=0.0, y=24.0 - openness * 0.5 * 16.0, z=0.0,
            y_rot=joml.f32(joml.f32(270.0 * openness) * joml.f32(math.pi / 180.0)))
    rig.emit(pose, "entity/shulker/" + _path(node.get("texture"), "shulker"), out)


def _bed_pose(pose, foot, facing):
    """`BedRenderer.preparePose`: the piece laid flat and turned to its facing."""
    pose.translate(0.0, 0.5625, -1.0 if foot else 0.0)
    pose.rotate(_rot_x(_radians(90.0)))
    pose.translate(0.5, 0.5, 0.5)
    pose.rotate(_rot_z(_radians(180.0 + Y_ROT[facing])))
    pose.translate(-0.5, -0.5, -0.5)


def bed(node, models, pose, out):
    """`BedRenderer.submitSpecial`: both halves of the bed, head first."""
    texture = "entity/bed/" + _path(node.get("texture"), "white")
    for layer, foot in (("bed_head", False), ("bed_foot", True)):
        piece = pose.copy()
        _bed_pose(piece, foot, "south")
        Rig(models[layer]).emit(piece, texture, out)


def banner(node, models, pose, out, patterns=()):
    """`BannerRenderer.submitSpecial`: pole, bar and flag, two thirds the size."""
    pose.translate(0.5, 0.0, 0.5)
    pose.scale(0.6666667, -0.6666667, -0.6666667)
    Rig(models["standing_banner"]).emit(pose, "entity/banner_base", out)
    flag = Rig(models["standing_banner#flag"])
    # `BannerFlagModel.setupAnim`: the cloth's sway, at phase zero in a slot.
    flag.set("flag", x_rot=joml.f32(joml.f32(-0.0125 + joml.f32(0.01 * math.cos(0.0)))
                                   * joml.f32(math.pi)))
    flag.emit(pose, "entity/banner_base", out)
    # `BannerRenderer.submitPatterns`: the base colour, then a layer per pattern.
    flag.emit(pose, "entity/banner/base", out, kind="paint",
              tint=_colour(_path(node.get("color"), "white")))
    for asset, colour in patterns:
        flag.emit(pose, "entity/banner/" + asset, out, kind="paint", tint=_colour(colour))


def _skull_anim(rig, kind, animation, y_rot):
    """`SkullModel.setupAnim` and the two kinds that move more than their head."""
    rig.set("head", y_rot=joml.f32(joml.f32(y_rot) * joml.f32(math.pi / 180.0)),
            x_rot=0.0)
    if kind == "dragon":
        rig.set("head/jaw", x_rot=(math.sin(animation * math.pi * 0.2) + 1.0) * 0.2)
    elif kind == "piglin":
        rig.set("head/left_ear",
                z_rot=-(math.cos(animation * math.pi * 0.2 * 1.2) + 2.5) * 0.2)
        rig.set("head/right_ear", z_rot=(math.cos(animation * math.pi * 0.2) + 2.5) * 0.2)


def head(node, models, pose, out, kind=None):
    """`SkullBlockRenderer.submitSkull`: on the floor of the block, turned round."""
    kind = kind or _path(node.get("kind"), "skeleton")
    layer, skin = SKULLS[kind]
    pose.translate(0.5, 0.0, 0.5)
    pose.scale(-1.0, -1.0, 1.0)
    rig = Rig(models[layer])
    _skull_anim(rig, kind, float(node.get("animation", 0.0)), 180.0)
    rig.emit(pose, skin, out)


def player_head(node, models, pose, out):
    """`PlayerHeadSpecialRenderer.submit`: a skull with the default skin."""
    head(node, models, pose, out, kind="player")


def conduit(node, models, pose, out):
    """`ConduitSpecialRenderer.submit`: the shell alone, in the block's middle."""
    del node
    pose.translate(0.5, 0.5, 0.5)
    Rig(models["conduit#shell"]).emit(pose, "entity/conduit/base", out)


def decorated_pot(node, models, pose, out, sides=("decorated_pot_side",) * 4):
    """`DecoratedPotRenderer.submit`: the body, then one picture per side."""
    del node
    base = Rig(models["decorated_pot_base"])
    for part in ("neck", "top", "bottom"):
        base.emit(pose, "entity/decorated_pot/decorated_pot_base", out, root=part)
    walls = Rig(models["decorated_pot_sides"])
    for part, picture in zip(("front", "back", "left", "right"), sides):
        walls.emit(pose, "entity/decorated_pot/" + picture, out, root=part)


def copper_golem_statue(node, models, pose, out):
    """`CopperGolemStatueSpecialRenderer.positionModel`: upside down, facing south."""
    pose.translate(0.5, 1.5, 0.5)
    pose.scale(-1.0, -1.0, 1.0)
    rig = Rig(models[STATUE_LAYERS[_path(node.get("pose"), "standing")]])
    # `CopperGolemStatueModel.setupAnim`: the statue's own turn, from its facing.
    rig.set("root", y=0.0, y_rot=_radians(Y_ROT["north"]), z_rot=joml.f32(math.pi))
    rig.emit(pose, _texture(node.get("texture"), "entity/copper_golem/copper_golem"), out)


def shield(node, models, pose, out, patterns=(), base_colour=None):
    """`ShieldSpecialRenderer.submit`: handle and plate, or the plate's patterns."""
    del node
    pose.scale(1.0, -1.0, -1.0)
    rig = Rig(models["shield"])
    dyed = bool(patterns) or base_colour is not None
    picture = "entity/shield_base" if dyed else "entity/shield_base_nopattern"
    rig.emit(pose, picture, out, root="handle")
    if not dyed:
        rig.emit(pose, picture, out, root="plate")
        return
    rig.emit(pose, picture, out)
    rig.emit(pose, "entity/shield/base", out, kind="paint",
             tint=_colour(base_colour or "white"))
    for asset, colour in patterns:
        rig.emit(pose, "entity/shield/" + asset, out, kind="paint", tint=_colour(colour))


BUILDERS = {
    "minecraft:chest": chest,
    "minecraft:shulker_box": shulker_box,
    "minecraft:bed": bed,
    "minecraft:banner": banner,
    "minecraft:conduit": conduit,
    "minecraft:decorated_pot": decorated_pot,
    "minecraft:head": head,
    "minecraft:player_head": player_head,
    "minecraft:shield": shield,
    "minecraft:copper_golem_statue": copper_golem_statue,
}


def quads(node, models, pose, out):
    """One special renderer's whole model, placed by `pose`. False when unknown."""
    builder = BUILDERS.get(node.get("type") if isinstance(node, dict) else None)
    if builder is None:
        return False
    builder(node, models, pose, out)
    return True


def icon(item, node, base, models, textures, tile, render):
    """One special item's picture, or why there is none yet."""
    del item
    if models is None:
        return None, "the game draws it with a block entity's model, and this bake was "\
                     "given no entity model table"
    if base is None:
        return None, "its special renderer names no base model"
    pose = joml.Pose()
    item_transform(pose, base["display"].get("gui"))
    placed = []
    if not quads(node, models, pose, placed):
        kind = node.get("type") if isinstance(node, dict) else None
        return None, f"the renderer {kind} is not built here"
    if not placed:
        return None, "its model draws nothing"
    pixels = render.render(placed, joml.Pose(), textures.get, tile,
                           flat_light=base.get("gui_light") == "front")
    return (pixels, None) if pixels[..., 3].any() else (None, "its model draws nothing")
