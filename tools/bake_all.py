#!/usr/bin/env python3
"""Bake everything the client reads from the game, in one go.

Usage: bake_all.py <client.jar> <reports/blocks.json> <protocol> [out-dir]

Four files come out: block geometry, the texture array, the colour maps biome
tints are looked up in, and the sky. All of it is read from the installation
the player already owns; none of it is committed here or shipped.
"""
import os
import subprocess
import sys
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
    print(f"\n{out} is ready for protocol {protocol}")


if __name__ == "__main__":
    main(sys.argv[1:])
