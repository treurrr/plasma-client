#!/usr/bin/env python3
"""Generate the entity data table from the protocol wiki's Entity metadata page.

Usage: gen_metadata.py <wikitext> <reports/registries.json>

Writes src/proto/metadata_gen.cpp: for every entity type in the registry, what
each set_entity_data index means, inherited fields included. The page is the
wiki's own source text for 1.21.11 (revision 3343420):

    curl -A plasma 'https://minecraft.wiki/api.php?action=query&prop=revisions&\
revids=3343420&rvprop=content&rvslots=main&format=json' | jq -r '.query.pages[].revisions[0].slots.main["*"]'

An entity type the page does not describe, a section that inherits from one
that does not exist, or a serializer this script does not know stops the run.
"""
import json
import os
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src/proto/metadata_gen.cpp"

TYPES = {
    "Byte": "Byte", "VarInt": "VarInt", "VarLong": "VarLong", "Float": "Float",
    "String": "String", "Text Component": "Component",
    "Optional Text Component": "OptionalComponent", "Slot": "ItemStack", "Boolean": "Boolean",
    "Rotations": "Rotations", "Position": "BlockPos", "Optional Position": "OptionalBlockPos",
    "Direction": "Direction", "Optional Living Entity Reference": "OptionalLivingEntity",
    "Block State": "BlockState", "Optional Block State": "OptionalBlockState",
    "Particle": "Particle", "Particles": "Particles", "Villager Data": "VillagerData",
    "Optional VarInt": "OptionalUnsignedInt", "Pose": "Pose", "Cat Variant": "CatVariant",
    "Cow Variant": "CowVariant", "Wolf Variant": "WolfVariant",
    "Wolf Sound Variant": "WolfSoundVariant", "Frog Variant": "FrogVariant",
    "Pig Variant": "PigVariant", "Chicken Variant": "ChickenVariant",
    "Zombie Nautilus Variant": "ZombieNautilusVariant",
    "Optional Global Position": "OptionalGlobalPos", "Painting Variant": "PaintingVariant",
    "Sniffer State": "SnifferState", "Armadillo State": "ArmadilloState",
    "Copper Golem State": "CopperGolemState", "Weathering Copper State": "WeatheringCopperState",
    "Vector3": "Vector3", "Quaternion": "Quaternion", "Resolvable Profile": "ResolvableProfile",
    "Humanoid Arm": "HumanoidArm",
}

# Where the page's serializer is not the one the server writes, checked against
# set_entity_data's own serializer ids: (section, field position) -> type.
CORRECTIONS = {
    ("Avatar", 0): "HumanoidArm",
}

STOP = {"the", "a", "an", "of", "if", "is", "this", "that", "used", "to", "for", "in"}


def slug(text, used):
    text = re.sub(r"\[\[(?:[^]|]*\|)?([^]]*)\]\]", r"\1", text)
    text = re.sub(r"<[^>]+>|''+|\{\{[^}]*\}\}", "", text)
    text = re.split(r"[(,.:;]| - |\bwhen\b|\bwhich\b", text, maxsplit=1)[0]
    words = re.findall(r"[a-z0-9]+", text.lower())
    if len(words) > 4:
        words = [w for w in words if w not in STOP] or words
    name = "_".join(words[:5]) or "value"
    base, n = name, 2
    while name in used:
        name, n = f"{base}_{n}", n + 1
    used.add(name)
    return name


def entity_table(text):
    rows = {}
    for match in re.finditer(r"\|-[^\n]*\n\|\s*(\d+)\s*\n\|\s*([^\n]+?)\s*\n\|[^\n]*\n\|[^\n]*\n"
                             r"\|\s*<code>(minecraft:[a-z_]+)</code>", text):
        rows.setdefault(match.group(2), []).append(match.group(3))
    return rows


def sections(text):
    """Title -> (parent title, [(serializer, meaning)])."""
    out = {}
    parts = re.split(r"^=== (.+?) ===\s*$", text, flags=re.M)
    for title, body in zip(parts[1::2], parts[2::2]):
        inherit = re.search(r"\{\{Metadata inherit\|([^|}]+)(?:\|inherits=([^}]+))?\}\}", body)
        if not inherit:
            continue
        fields = []
        lines = body.splitlines()
        for i, line in enumerate(lines):
            kind = re.search(r"\{\{Metadata type\|([^}]+)\}\}", line)
            if not kind:
                continue
            if kind.group(1) not in TYPES:
                sys.exit(f"{title}: unknown serializer {kind.group(1)!r}")
            meaning = "flags"
            for after in lines[i + 1:]:
                after = after.strip()
                if after.startswith("|-") or after.startswith("|}"):
                    break
                if after.startswith("!"):
                    break
                if after.startswith("|"):
                    meaning = re.sub(r'^\|(?:\s*(?:colspan|rowspan)="\d+"\s*\|)?', "", after).strip()
                    break
            fields.append((CORRECTIONS.get((title, len(fields)), TYPES[kind.group(1)]), meaning))
        out[title] = (inherit.group(2).strip() if inherit.group(2) else None, fields)
    return out


def quote(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    text = Path(sys.argv[1]).read_text()
    registry = json.loads(Path(sys.argv[2]).read_text())["minecraft:entity_type"]["entries"]
    names = entity_table(text)
    listed = {entity for entities in names.values() for entity in entities}
    if listed != set(registry):
        sys.exit("the page's entity table is not the registry: "
                 + ", ".join(sorted(listed ^ set(registry))))
    described = sections(text)

    def chain(title):
        fields = []
        while title is not None:
            if title not in described:
                sys.exit(f"no section {title!r}")
            parent, own = described[title]
            fields = own + fields
            title = parent
        return fields

    tables = {}
    for title in described:
        for entity in names.get(title, []):
            tables[entity] = chain(title)
    missing = sorted(set(registry) - set(tables))
    if missing:
        sys.exit("no section for " + ", ".join(missing))

    out = ["// Generated by tools/gen_metadata.py. Do not edit by hand.",
           '#include "metadata_gen.hpp"', "", "namespace mcpp::proto::generated {", "",
           "const std::map<std::string_view, std::vector<MetaField>, std::less<>>& metadata_fields() {",
           "    using T = MetaType;",
           "    static const std::map<std::string_view, std::vector<MetaField>, std::less<>> table = {"]
    for entity in sorted(tables):
        used = set()
        out.append(f"        {{{quote(entity)}, {{")
        for index, (kind, meaning) in enumerate(tables[entity]):
            out.append(f"            {{{index}, T::{kind}, {quote(slug(meaning, used))}}},")
        out.append("        }},")
    out += ["    };", "    return table;", "}", "", "} // namespace mcpp::proto::generated", ""]
    handle, temp = tempfile.mkstemp(dir=SOURCE.parent, prefix=SOURCE.name + ".")
    with os.fdopen(handle, "w") as stream:
        stream.write("\n".join(out))
    os.replace(temp, SOURCE)
    print(f"{SOURCE.relative_to(ROOT)}: {len(tables)} entity types")


if __name__ == "__main__":
    main()
