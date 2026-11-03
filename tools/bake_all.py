#!/usr/bin/env python3
"""Bake everything the client reads from the game, in one go.

Usage: bake_all.py <client.jar> <reports/blocks.json> <protocol> [out-dir]

Out come block geometry, the texture array, the colour maps biome tints are
looked up in, the sky, items, the gui, the built-in registries and the language.
All of it is read from the installation the player already owns; none of it is
committed here or shipped.
"""
import json
import os
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


def main(args):
    if not 3 <= len(args) <= 4:
        sys.exit(__doc__)
    jar, report, protocol = args[0], args[1], int(args[2])
    out = Path(args[3]) if len(args) == 4 else default_directory()
    out.mkdir(parents=True, exist_ok=True)

    table = out / f"blocks-{protocol}.plbk"
    run("bake_block_table.py", jar, report, protocol, table)
    run("bake_atlas.py", jar, table, out / f"atlas-{protocol}.plat")
    run("bake_colormaps.py", jar, out / f"colormaps-{protocol}.plcm")
    run("bake_sky.py", jar, out / f"sky-{protocol}.plsk")
    run("bake_items.py", jar, Path(report).parent / "registries.json",
        Path(report).parent / "items.json", protocol,
        out / f"items-{protocol}.plit")
    run("bake_gui.py", jar, protocol, out / f"gui-{protocol}.plgu")
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
    print(f"\n{out} is ready for protocol {protocol}")


if __name__ == "__main__":
    main(sys.argv[1:])
