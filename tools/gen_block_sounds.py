#!/usr/bin/env python3
"""Writes data/block_sounds.txt: which sound type every block has, and that
type's five sound events with its volume and pitch.

The game's reports do not say. The source is the Minecraft Wiki's public "Block
sound type" page (which type each block uses, and its volume and pitch) and the
"Sound table/Block/<type>/JE" templates it loads (each type's events), fetched
once into a cache directory. Display names are turned into registry names with
the client jar's own en_us.json; every event is checked against the protocol's
sound_event registry, and every block of the registry gets exactly one row.

Usage: gen_block_sounds.py <cache dir> <client.jar> <reports/registries.json> <out.txt>
"""
import json
import re
import sys
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

API = "https://minecraft.wiki/api.php"
COLORS = ["white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
          "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"]
COLOR = "(?:" + "|".join(COLORS) + ")"
AGE = "(?:waxed_)?(?:exposed_|weathered_|oxidized_)?"
CORALS = "(?:tube|brain|bubble|fire|horn)"
# The wiki writes a family as "All <family>"; these are the registry names it means.
FAMILIES = {
    "All Banners": rf"{COLOR}_(?:wall_)?banner",
    "All Beds": rf"{COLOR}_bed",
    "All Carpets": rf"{COLOR}_carpet",
    "All Chiseled Copper": rf"{AGE}chiseled_copper",
    "All Command Blocks": r"(?:chain_|repeating_)?command_block",
    "All Concrete": rf"{COLOR}_concrete",
    "All Concrete Powder": rf"{COLOR}_concrete_powder",
    "All Copper Bars": rf"{AGE}copper_bars",
    "All Copper Blocks": r"copper_block|waxed_copper_block|(?:waxed_)?(?:exposed|weathered|oxidized)_copper",
    "All Copper Bulbs": rf"{AGE}copper_bulb",
    "All Copper Chains": rf"{AGE}copper_chain",
    "All Copper Chests": rf"{AGE}copper_chest",
    "All Copper Doors": rf"{AGE}copper_door",
    "All Copper Golem Statues": rf"{AGE}copper_golem_statue",
    "All Copper Grates": rf"{AGE}copper_grate",
    "All Copper Lanterns": rf"{AGE}copper_lantern",
    "All Copper Trapdoors": rf"{AGE}copper_trapdoor",
    "All Coral": rf"{CORALS}_coral",
    "All Coral Blocks": rf"{CORALS}_coral_block",
    "All Coral Fans": rf"{CORALS}_coral(?:_wall)?_fan",
    "All Cut Copper": rf"{AGE}cut_copper",
    "All Cut Copper Slabs": rf"{AGE}cut_copper_slab",
    "All Cut Copper Stairs": rf"{AGE}cut_copper_stairs",
    "All Dead Coral": rf"dead_{CORALS}_coral",
    "All Dead Coral Blocks": rf"dead_{CORALS}_coral_block",
    "All Dead Coral Fans": rf"dead_{CORALS}_coral(?:_wall)?_fan",
    "All Glazed Terracotta": rf"{COLOR}_glazed_terracotta",
    "All Lightning Rods": rf"{AGE}lightning_rod",
    "All Mushroom Blocks": r"brown_mushroom_block|red_mushroom_block|mushroom_stem",
    "All Shelves": r"[a-z_]+_shelf",
    "All Shulker Boxes": rf"(?:{COLOR}_)?shulker_box",
    "All Stained Glass": rf"{COLOR}_stained_glass",
    "All Stained Glass Panes": rf"{COLOR}_stained_glass_pane",
    "All Wool": rf"{COLOR}_wool",
    "All Stained Terracotta": rf"{COLOR}_terracotta",
    "All Mushrooms": r"brown_mushroom|red_mushroom",
}
# Names the wiki spells differently from the language file.
ALIASES = {
    "Wheat": ["wheat"], "Redstone Dust": ["redstone_wire"], "Smooth Quartz": ["smooth_quartz"],
    "Potted Azalea Bush": ["potted_azalea_bush"],
    "Potted Flowering Azalea Bush": ["potted_flowering_azalea_bush"],
    "All Stained Terracotta": ["terracotta"],
}
# Blocks the wiki lists under two types, and the one this table takes.
CHOSEN = {"attached_melon_stem": "wood", "attached_pumpkin_stem": "wood"}
# Blocks the wiki does not list that are a variant of one it does.
LIKE = {"copper_torch": "torch", "copper_wall_torch": "wall_torch"}
KINDS = {"Once the block has broken": "break", "Once the block has been broken": "break",
         "Breaking the block": "break", "Walking on the block": "step",
         "When the block is placed": "place", "Placing the block": "place",
         "While the block is in the process of being broken": "hit",
         "Not normally encounterable": "hit", "Falling on the block with fall damage": "fall"}


def cache_name(title):
    return re.sub(r"[^A-Za-z0-9]+", "_", title) + ".txt"


def fetch(cache, titles):
    cache.mkdir(parents=True, exist_ok=True)
    pages = {}
    missing = []
    for title in titles:
        path = cache / cache_name(title)
        if path.exists():
            pages[title] = path.read_text()
        else:
            missing.append(title)
    for start in range(0, len(missing), 50):
        batch = missing[start:start + 50]
        query = urllib.parse.urlencode({
            "action": "query", "prop": "revisions", "rvprop": "content", "rvslots": "main",
            "format": "json", "titles": "|".join(batch)})
        request = urllib.request.Request(f"{API}?{query}", headers={"User-Agent": "plasma-gen"})
        with urllib.request.urlopen(request, timeout=60) as reply:
            data = json.load(reply)
        normal = {n["to"]: n["from"] for n in data["query"].get("normalized", [])}
        for page in data["query"]["pages"].values():
            title = normal.get(page["title"], page["title"])
            text = page["revisions"][0]["slots"]["main"]["*"] if "revisions" in page else ""
            (cache / cache_name(title)).write_text(text)
            pages[title] = text
    return pages


def in_java(note):
    """Whether a wiki line's edition and version notes put it in this Java release."""
    if re.search(r"\{\{only\|(?:be|bedrock)", note, re.I):
        return False
    return not re.search(r"\{\{upcoming", note, re.I)


def block_links(cell):
    links = []
    for line in cell.split("\n"):
        match = re.search(r"\{\{BlockLink\|([^}]*)\}\}(.*)", line)
        if not match or not in_java(match.group(2)) or "(shattered)" in match.group(2):
            continue
        parts = [p for p in match.group(1).split("|") if not p.startswith("link=")]
        ident = next((p[3:] for p in parts if p.startswith("id=")), None)
        names = [p for p in parts if not p.startswith("id=")]
        if ident and ident.startswith("All "):
            links.append(ident)
        elif names and names[-1].startswith("All "):
            links.append(names[-1])
        else:
            links.append(names[0] if names else ident)
    return links


def java_type(cell):
    for part in cell.split("<br>"):
        match = re.search(r"\{\{va\|([a-z_]+)\}\}", part)
        if match and in_java(part):
            return match.group(1)
    return None


def number(cell):
    match = re.match(r"\s*([0-9.]+)", cell)
    return float(match.group(1)) if match else 1.0


def sound_lines(text):
    lines = {}
    for body in re.split(r"\{\{SoundLine", text)[1:]:
        fields = dict(re.findall(r"\|\s*([a-z0-9]+)\s*=\s*([^|\n}]*)", body))
        description = re.sub(r"\{\{.*", "", fields.get("description", "")).strip()
        kind = KINDS.get(description)
        if kind and kind not in lines:
            event = re.sub(r"<!--.*?-->", "", fields.get("id", "")).strip()
            lines[kind] = event.removeprefix("minecraft:")
    return lines


def main(args):
    if len(args) != 4:
        sys.exit(__doc__)
    cache, jar, registries_path, out = Path(args[0]), args[1], args[2], Path(args[3])
    with zipfile.ZipFile(jar) as archive:
        lang = json.loads(archive.read("assets/minecraft/lang/en_us.json"))
        flowers = json.loads(archive.read("data/minecraft/tags/block/small_flowers.json"))
    registries = json.loads(Path(registries_path).read_text())
    blocks = [b.removeprefix("minecraft:") for b in registries["minecraft:block"]["entries"]]
    events = {e.removeprefix("minecraft:") for e in registries["minecraft:sound_event"]["entries"]}
    by_display = {}
    for block in blocks:
        by_display.setdefault(lang.get(f"block.minecraft.{block}", ""), []).append(block)
    FAMILIES["All Flowers"] = "|".join(v.removeprefix("minecraft:") for v in flowers["values"]) + \
        "|sunflower|lilac|rose_bush|peony"

    page = fetch(cache, ["Block sound type"])["Block sound type"]
    table = page[page.index("== List =="):]
    table = table[:table.index("\n|}")]
    rows = []
    for row in table.split("\n|-\n")[1:]:
        cells = re.split(r"\n\|(?!\})", "\n" + row)[1:]
        if len(cells) < 5:
            continue
        name = java_type(cells[0])
        template = re.search(r"\{\{LoadPage\|(Template:Sound table/Block/[^|]*/JE)", cells[1])
        if not name or not template:
            continue
        rows.append((name, template.group(1), block_links(cells[2]), number(cells[3]),
                     number(cells[4])))

    templates = fetch(cache, sorted({r[1] for r in rows}))
    assigned = {}
    problems = []
    kinds = ("break", "step", "place", "hit", "fall")
    for name, template, links, volume, pitch in rows:
        lines = sound_lines(templates[template])
        found = []
        for link in links:
            matched = ([b for b in blocks if re.fullmatch(FAMILIES[link], b)]
                       if link in FAMILIES else by_display.get(link, []))
            matched += ALIASES.get(link, [])
            if not matched:
                problems.append(f"{name}: no block is called {link!r}")
            found += matched
        if not found:
            continue
        sounds = []
        for kind in kinds:
            event = lines.get(kind, f"block.{name}.{kind}")
            if event not in events:
                problems.append(f"{name}: no {kind} event, {event} is not in the registry")
                event = "-"
            sounds.append(event)
        for block in found:
            if block in assigned and assigned[block][0] != name:
                if CHOSEN.get(block) == name:
                    assigned[block] = (name, volume, pitch, sounds)
                elif block not in CHOSEN:
                    problems.append(f"{block}: both {assigned[block][0]} and {name}")
                continue
            assigned[block] = (name, volume, pitch, sounds)

    for block, like in LIKE.items():
        if block not in assigned and like in assigned:
            assigned[block] = assigned[like]
    stone = next(r for r in rows if r[0] == "stone")
    stone_lines = sound_lines(templates[stone[1]])
    defaulted = [b for b in blocks if b not in assigned]
    for block in defaulted:
        assigned[block] = ("stone", 1.0, 1.0, [stone_lines[k] for k in kinds])

    with open(out, "w") as file:
        file.write("# block\ttype\tvolume\tpitch\tbreak\tstep\tplace\thit\tfall\n")
        file.write("# generated by tools/gen_block_sounds.py; sorted by registry name\n")
        for block in sorted(blocks):
            name, volume, pitch, sounds = assigned[block]
            file.write(f"minecraft:{block}\t{name}\t{volume:g}\t{pitch:g}\t" +
                       "\t".join(sounds) + "\n")
    for line in problems:
        print("!", line, file=sys.stderr)
    print(f"{len(blocks)} blocks, {len(rows)} sound types, {len(defaulted)} take the default "
          f"(stone): {' '.join(defaulted)}", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
