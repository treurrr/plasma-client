#!/usr/bin/env python3
"""Bake everything the client reads from the game, in one go.

Usage: bake_all.py <client.jar> <reports/blocks.json> <protocol> [out-dir]

Out come block geometry, the texture array, the colour maps biome tints are
looked up in, the sky, items, the gui, the particles a broken block throws,
the crack textures, the built-in registries and the language.
All of it is read from the installation the player already owns; none of it is
committed here or shipped. The tables only the game's code knows (entity sizes
and the like) come from exporters compiled against the remapped client jar,
~/mc-reference/<version>/client-named.jar or $PLASMA_REFERENCE_JAR, and are
skipped, saying so, without it or a JDK.
"""
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent


def default_directory():
    base = os.environ.get("XDG_DATA_HOME") or (Path.home() / ".local" / "share")
    return Path(base) / "plasma-client" / "data"


def run(script, *args):
    print(f"-- {script}")
    subprocess.run([sys.executable, str(TOOLS / script), *map(str, args)], check=True)


def ask_the_game(script, *args):
    """A bake that runs an exporter against the remapped client jar, skipped
    with the reason when that jar or a JDK is not there."""
    sys.path.insert(0, str(TOOLS / "oracle"))
    from run import reference_jar
    if not reference_jar().is_file():
        print(f"-- {script} skipped: no {reference_jar()} (run ~/mc-reference/decompile.sh "
              "or set PLASMA_REFERENCE_JAR)")
        return False
    if not (shutil.which("javac") and shutil.which("java")):
        print(f"-- {script} skipped: no JDK (javac and java) on the PATH")
        return False
    run(script, *args)
    return True


def main(args):
    if not 3 <= len(args) <= 4:
        sys.exit(__doc__)
    jar, report, protocol = args[0], args[1], int(args[2])
    out = Path(args[3]) if len(args) == 4 else default_directory()
    out.mkdir(parents=True, exist_ok=True)

    table = out / f"blocks-{protocol}.plbk"
    run("bake_block_table.py", jar, report, protocol, table)
    sprites = out / f"particle_sprites-{protocol}.txt"
    run("bake_particle_sprites.py", jar, sprites)
    run("bake_atlas.py", jar, table, out / f"atlas-{protocol}.plat", sprites)
    run("bake_colormaps.py", jar, out / f"colormaps-{protocol}.plcm")
    run("bake_sky.py", jar, out / f"sky-{protocol}.plsk")
    # The entity model layers first: the items the game draws with a block
    # entity's model - a chest, a bed, a banner - have no icon without them.
    models = out / f"entity_models-{protocol}.txt"
    has_models = ask_the_game("export/export_models.py", models)
    item_args = ["--entity-models", models] if has_models else []
    run("bake_items.py", jar, Path(report).parent / "registries.json",
        Path(report).parent / "items.json", protocol,
        out / f"items-{protocol}.plit", *item_args)
    run("bake_held.py", jar, Path(report).parent / "registries.json", protocol,
        out / f"held-{protocol}.plhd")
    run("bake_gui.py", jar, protocol, out / f"gui-{protocol}.plgu",
        Path(report).parent / "registries.json", Path(report).parent / "items.json")
    run("bake_datapack.py", jar, out / f"datapack-{protocol}")
    ask_the_game("bake_block_render.py", out / f"block-render-{protocol}.txt")
    ask_the_game("oracle/bake_shapes.py", out / f"collision_shapes-{protocol}.txt")
    ask_the_game("bake_entity_dimensions.py", out / f"entity_dimensions-{protocol}.txt")
    ask_the_game("gen_block_sounds.py", out / f"block_sounds-{protocol}.txt")
    ask_the_game("bake_creative.py", out / f"creative-{protocol}.txt")
    ask_the_game("bake_particles.py", jar, report, out / f"particles-{protocol}.txt",
                 out / f"cracks-{protocol}")
    if has_models:
        run("bake_entities.py", jar, models, out / f"entities-{protocol}.plen")
    else:
        print("-- bake_entities.py skipped: it needs the model layers")
    # Which blocks carry which block entity is the game's own answer, so the
    # table of states with a renderer of their own waits on the exporter.
    blocks_with_entities = out / f"block_entity_blocks-{protocol}.txt"
    if ask_the_game("export/export_block_entities.py", blocks_with_entities):
        run("bake_block_entities.py", report, blocks_with_entities,
            out / f"block_entities-{protocol}.txt")
    else:
        print("-- bake_block_entities.py skipped: it needs the block entity types")
    # The registries the client has built in, in id order: what an attribute,
    # an effect or an entity type number means.
    registries = json.loads((Path(report).parent / "registries.json").read_text())
    with open(out / f"registries-{protocol}.txt", "w") as lines:
        for name, registry in registries.items():
            ordered = sorted(registry["entries"].items(), key=lambda e: e[1]["protocol_id"])
            for entry, _ in ordered:
                lines.write(f"{name}\t{entry}\n")
    print(out / f"registries-{protocol}.txt")
    # The language file, without which every server message that names a key
    # - a death, a chat format, an advancement - shows the key itself.
    lang = out.parent / "lang"
    lang.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(jar) as archive:
        (lang / "en_us.json").write_bytes(archive.read("assets/minecraft/lang/en_us.json"))
    print(f"{lang / 'en_us.json'}")
    # The splash lines the title screen picks from, read at start.
    texts = out.parent / "texts"
    texts.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(jar) as archive:
        (texts / "splashes.txt").write_bytes(archive.read("assets/minecraft/texts/splashes.txt"))
    print(texts / "splashes.txt")
    print(f"\n{out} is ready for protocol {protocol}")


if __name__ == "__main__":
    main(sys.argv[1:])
