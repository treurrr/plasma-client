#!/usr/bin/env python3
"""Locate a Minecraft client jar in an installation the player already has.

Usage: find_minecraft.py [version]

Prints the path of a jar whose version.json matches, or exits non-zero. The
client reads assets from the player's own copy; it never ships them and should
not download the game itself.
"""
import json
import os
import sys
import zipfile
from pathlib import Path

HOME = Path.home()

# Each launcher keeps jars somewhere different; the glob is matched, then the
# jar's own version.json decides.
SEARCH = [
    HOME / ".minecraft/versions",
    HOME / ".var/app/com.mojang.Minecraft/.minecraft/versions",
    HOME / ".var/app/com.modrinth.ModrinthApp/data/ModrinthApp/meta/versions",
    HOME / ".local/share/ModrinthApp/meta/versions",
    HOME / ".local/share/PrismLauncher/libraries/com/mojang/minecraft",
    HOME / ".var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/libraries/com/mojang/minecraft",
    HOME / ".local/share/multimc/libraries/com/mojang/minecraft",
    Path(os.environ.get("APPDATA", "/nonexistent")) / ".minecraft/versions",
]


def jar_version(path):
    try:
        with zipfile.ZipFile(path) as archive:
            return json.loads(archive.read("version.json"))
    except (OSError, KeyError, ValueError, zipfile.BadZipFile):
        return None


def candidates():
    for root in SEARCH:
        if not root.is_dir():
            continue
        for jar in sorted(root.rglob("*.jar")):
            yield jar


def main(args):
    wanted = args[0] if args else None
    for jar in candidates():
        info = jar_version(jar)
        if not info:
            continue
        if wanted and info.get("id") != wanted:
            continue
        print(jar)
        print(f"  {info.get('id')}  protocol {info.get('protocol_version')}", file=sys.stderr)
        return 0

    print(f"no client jar found for {wanted or 'any version'}", file=sys.stderr)
    print("searched in:", file=sys.stderr)
    for root in SEARCH:
        print(f"  {root}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
