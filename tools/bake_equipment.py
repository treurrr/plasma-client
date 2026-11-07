#!/usr/bin/env python3
"""Bake what the game draws over a wearer, and what equips as what.

Usage: bake_equipment.py <client.jar> <reports/registries.json>
                         <reports/items.json> <protocol> <out.txt>
                         [<out_trims.plat>]

Two tables in the text file. The first is every assets/minecraft/equipment/*.json
read the way `EquipmentClientInfo.CODEC` reads it: per layer type, the layers in
order, each with the texture `EquipmentClientInfo.Layer.getTextureLocation`
resolves to, whether it is dyeable, the colour it takes undyed and whether it
draws the wearer's own texture instead. The second is every item whose
`minecraft:equippable` names an asset, by its protocol id, so a stack straight
off the wire says what to draw without a registry walk.

The optional second output holds the armour trim sprites: the atlas source
assets/minecraft/atlases/armor_trims.json is a `paletted_permutations`, and the
sprites it makes exist nowhere in the jar - the game builds each one by remapping
a base image's colours through two palettes, which `PalettedPermutations` does at
load time and this does once, at bake time.

All of it is read from the installation the player already owns; none of it is
committed here or shipped.
"""
import json
import struct
import sys
import zipfile
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

TRIM_MAGIC = b"PLTR"
TRIM_VERSION = 1
# `EquipmentClientInfo.LayerType`, in the enum's own order: a layer type the
# file names outside this list is a resource pack's, not the game's.
LAYER_TYPES = [
    "humanoid", "humanoid_leggings", "wings", "wolf_body", "horse_body", "llama_body",
    "pig_saddle", "strider_saddle", "camel_saddle", "camel_husk_saddle", "horse_saddle",
    "donkey_saddle", "mule_saddle", "zombie_horse_saddle", "skeleton_horse_saddle",
    "happy_ghast_body", "nautilus_saddle", "nautilus_body",
]
# `EquipmentSlot`, in the enum's own order; its ordinal is what the table writes,
# not its `id`, which numbers the slots the wire order instead.
SLOTS = ["mainhand", "offhand", "feet", "legs", "chest", "head", "body", "saddle"]
EQUIPMENT = "assets/minecraft/equipment/"
ENTITY = "assets/minecraft/textures/entity/"
TRIM_ATLAS = "assets/minecraft/atlases/armor_trims.json"

HEADER = """\
# What the game draws over a wearer and which item equips as what, written by
# tools/bake_equipment.py for protocol {protocol}.
#
# [asset <id>]   one assets/minecraft/equipment/<id>.json, as
#                `EquipmentClientInfo` reads it
#   layer <type> <texture> <dyeable> <colour> <player>
#     <type>     the layer type's serialised name, "humanoid_leggings"
#     <texture>  `Layer.getTextureLocation`, keyed the way
#                tools/bake_entities.py keys its textures: the path under
#                textures/entity/ with no extension
#     <dyeable>  1 when the layer carries a `dyeable` block
#     <colour>   `ARGB.opaque(color_when_undyed)` in hex, 00000000 when the
#                layer names none - which is what
#                `EquipmentLayerRenderer.getColorForLayer` falls back to
#     <player>   1 when `use_player_texture` draws the wearer's own cape
#   The layers of one type follow in the order the asset lists them, which is
#   the order they are drawn in.
#
# [items]
#   item <id> <asset> <slot>
#     <id>       the item's protocol {protocol} id
#     <asset>    the `minecraft:equippable` component's asset_id
#     <slot>     `EquipmentSlot`'s ordinal: mainhand 0, offhand 1, feet 2,
#                legs 3, chest 4, head 5, body 6, saddle 7
#   An item whose equippable names no asset_id is left out:
#   `HumanoidArmorLayer.shouldRender` draws nothing for it.
"""


def strip_namespace(reference, what):
    namespace, _, path = reference.rpartition(":")
    if namespace not in ("", "minecraft"):
        sys.exit(f"{what}: {reference} is not in the minecraft namespace")
    return path


def undyed_colour(dyeable):
    """`ExtraCodecs.RGB_COLOR_CODEC` then `ARGB.opaque`: the colour an undyed
    layer is drawn in, 0 when it names none."""
    if dyeable is None or "color_when_undyed" not in dyeable:
        return 0
    value = dyeable["color_when_undyed"]
    if isinstance(value, list):
        channels = [max(0, min(255, round(float(c) * 255.0))) for c in value]
        value = channels[0] << 16 | channels[1] << 8 | channels[2]
    return (int(value) | 0xFF000000) & 0xFFFFFFFF


def asset_rows(jar, names):
    """Every equipment asset, by id, as a list of (layer type, layers)."""
    assets = []
    layers_total = 0
    for name in sorted(n for n in names if n.startswith(EQUIPMENT) and n.endswith(".json")):
        asset = name[len(EQUIPMENT):-5]
        info = json.loads(jar.read(name))
        rows = []
        for layer_type in LAYER_TYPES:
            listed = info["layers"].get(layer_type)
            if not listed:
                continue
            for layer in listed:
                path = strip_namespace(layer["texture"], f"{asset}.{layer_type}")
                full = f"{ENTITY}equipment/{layer_type}/{path}.png"
                if full not in names:
                    print(f"  {asset}: {full} is not in the jar")
                rows.append((layer_type, full[len(ENTITY):-4], layer.get("dyeable"),
                             layer.get("use_player_texture", False)))
        unknown = set(info["layers"]) - set(LAYER_TYPES)
        if unknown:
            sys.exit(f"{asset}: unknown layer types {sorted(unknown)}")
        layers_total += len(rows)
        assets.append((asset, rows))
    return assets, layers_total


def equippable_rows(registries_path, items_path):
    """Every item that equips as an asset: its protocol id, asset and slot."""
    entries = json.loads(Path(registries_path).read_text())["minecraft:item"]["entries"]
    items = json.loads(Path(items_path).read_text())
    rows = []
    skipped = 0
    for name, item in items.items():
        equippable = item.get("components", {}).get("minecraft:equippable")
        if equippable is None:
            continue
        if "asset_id" not in equippable:
            skipped += 1
            continue
        entry = entries.get(name)
        if entry is None:
            sys.exit(f"{name} equips as something but is in no item registry")
        rows.append((entry["protocol_id"], strip_namespace(equippable["asset_id"], name),
                     SLOTS.index(equippable["slot"])))
    rows.sort()
    return rows, skipped


def write_table(out_path, protocol, assets, items):
    with open(out_path, "w") as lines:
        lines.write(HEADER.format(protocol=protocol))
        for asset, rows in assets:
            lines.write(f"\n[asset {asset}]\n")
            for layer_type, texture, dyeable, player in rows:
                lines.write(f"layer {layer_type} {texture} {int(dyeable is not None)} "
                            f"{undyed_colour(dyeable):08x} {int(bool(player))}\n")
        lines.write("\n[items]\n")
        for item, asset, slot in items:
            lines.write(f"item {item} {asset} {slot}\n")


def argb(jar, reference):
    """`NativeImage.read` then `NativeImage.getPixels`: one image as ARGB ints.

    Every PNG the game reads comes back as four channels whatever the file's own
    colour type is, so a greyscale or paletted palette image is widened here the
    same way.
    """
    path = f"assets/minecraft/textures/{strip_namespace(reference, 'texture')}.png"
    with jar.open(path) as handle:
        pixels = np.array(Image.open(handle).convert("RGBA"), dtype=np.uint32)
    return (pixels[..., 3] << 24 | pixels[..., 0] << 16 | pixels[..., 1] << 8 | pixels[..., 2])


def palette_mapping(key, permutation):
    """`PalettedPermutations.createPaletteMapping`: which colour becomes which.

    The key's transparent pixels name no colour at all, and a key colour that
    repeats keeps the last permutation entry, as putting it in a map does.
    """
    if key.size != permutation.size:
        sys.exit(f"palette mapping has different sizes: {key.size} and {permutation.size}")
    lit = (key >> 24) != 0
    return dict(zip((key[lit] & 0xFFFFFF).tolist(), permutation[lit].tolist()))


def remap(image, mapping):
    """`PalettedPermutations$PalettedSpriteSupplier.get` by way of
    `NativeImage.mappedCopy`: the base image through one palette.

    A pixel keeps its own alpha scaled by the substitute's; one the key does not
    name keeps its own colour, opaque, which is what the game's
    `getOrDefault(.., ARGB.opaque(..))` leaves it as.
    """
    alpha = image >> 24
    colour = image & 0xFFFFFF
    unique, inverse = np.unique(colour, return_inverse=True)
    substituted = np.array([mapping.get(int(c), int(c) | 0xFF000000) for c in unique],
                           dtype=np.uint32)[inverse].reshape(image.shape)
    scaled = alpha * (substituted >> 24) // 255
    return np.where(alpha == 0, image, scaled << 24 | substituted & 0xFFFFFF)


def trim_sprites(jar):
    """Every sprite assets/minecraft/atlases/armor_trims.json asks for.

    Named by the path `ArmorTrim.layerAssetId` builds, which is the base
    sprite's id with the material's suffix on it, so a `minecraft:trim`
    component resolves to one by name and nothing else.
    """
    sources = json.loads(jar.read(TRIM_ATLAS))["sources"]
    sprites = []
    for source in sources:
        if source["type"] != "minecraft:paletted_permutations":
            print(f"  {TRIM_ATLAS}: ignored a {source['type']} source")
            continue
        separator = source.get("separator", "_")
        key = argb(jar, source["palette_key"]).reshape(-1)
        mappings = {name: palette_mapping(key, argb(jar, palette).reshape(-1))
                    for name, palette in source["permutations"].items()}
        for texture in source["textures"]:
            base = argb(jar, texture)
            path = strip_namespace(texture, "trim sprite")
            for name, mapping in mappings.items():
                sprites.append((f"{path}{separator}{name}", remap(base, mapping)))
    sprites.sort()
    return sprites


def write_trims(out_path, sprites):
    """The same container tools/bake_entities.py writes its textures in: a
    magic, a version, the unpacked size and one deflate stream."""
    body = bytearray(struct.pack("<I", len(sprites)))
    for name, image in sprites:
        encoded = name.encode()
        height, width = image.shape
        rgba = np.stack([image >> 16 & 0xFF, image >> 8 & 0xFF, image & 0xFF, image >> 24],
                        axis=-1).astype(np.uint8)
        body += struct.pack("<H", len(encoded)) + encoded + struct.pack("<HH", width, height)
        body += rgba.tobytes()
    Path(out_path).write_bytes(TRIM_MAGIC + struct.pack("<II", TRIM_VERSION, len(body))
                               + zlib.compress(bytes(body), 6))
    return len(body)


def main(args):
    if not 5 <= len(args) <= 6:
        sys.exit(__doc__)
    jar_path, registries_path, items_path, protocol, out_path = args[:5]
    protocol = int(protocol)
    trims_path = Path(args[5]) if len(args) == 6 else None

    items, skipped = equippable_rows(registries_path, items_path)
    with zipfile.ZipFile(jar_path) as jar:
        names = set(jar.namelist())
        assets, layers = asset_rows(jar, names)
        sprites = trim_sprites(jar) if trims_path else []
    write_table(out_path, protocol, assets, items)

    print(f"{out_path}: {len(assets)} assets, "
          f"{len({t for _, rows in assets for t, *_ in rows})} layer types, {layers} layers")
    print(f"  {len(items)} items equip as an asset, {skipped} as nothing")
    if trims_path:
        size = write_trims(trims_path, sprites)
        print(f"{trims_path}: {len(sprites)} trim sprites, {size / 1e6:.2f} MB "
              "before compression")


if __name__ == "__main__":
    main(sys.argv[1:])
