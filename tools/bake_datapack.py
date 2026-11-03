#!/usr/bin/env python3
"""Copy the parts of the built-in datapack the client checks itself against.

Usage: bake_datapack.py <client.jar> <out-dir>

A server sends its dimension types, biomes and timelines in configuration, so
the client never needs these to play. The tests do: they evaluate the same
timelines from the same JSON the server reads, without a server. The files are
Mojang's, read from the player's own jar and never committed here.
"""
import sys
import zipfile
from pathlib import Path

PARTS = ("timeline", "dimension_type", "worldgen/biome", "tags/timeline")


def main(args):
    if len(args) != 2:
        sys.exit(__doc__)
    jar, out = Path(args[0]), Path(args[1])
    copied = 0
    with zipfile.ZipFile(jar) as archive:
        for name in archive.namelist():
            for part in PARTS:
                prefix = f"data/minecraft/{part}/"
                if name.startswith(prefix) and name.endswith(".json") and "/" not in name[len(prefix):]:
                    target = out / part / name[len(prefix):]
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.read(name))
                    copied += 1
    print(f"{out}: {copied} files")


if __name__ == "__main__":
    main(sys.argv[1:])
