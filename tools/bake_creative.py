#!/usr/bin/env python3
"""Bake the creative inventory's tabs and their items from the game itself.

Usage: bake_creative.py [out.txt]

The tabs' contents are built by the game's own code, so they are asked for
rather than typed in: tools/creative/CreativeTabsExport.java is compiled
against the player's remapped client jar (~/mc-reference/<version>/
client-named.jar) and the launcher's libraries and run headless. Its output
is kept beside the other baked data and never committed.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
VERSION = "1.21.11"
PROTOCOL = 774
LIBRARY_ROOTS = [
    Path.home() / ".var/app/com.mojang.Minecraft/.minecraft/libraries",
    Path.home() / ".minecraft/libraries",
]


def classpath():
    jar = Path.home() / "mc-reference" / VERSION / "client-named.jar"
    if not jar.exists():
        sys.exit(f"{jar} is missing: run ~/mc-reference/decompile.sh first")
    jars = [str(jar)]
    for root in LIBRARY_ROOTS:
        if root.is_dir():
            jars += [str(p) for p in root.rglob("*.jar")]
            break
    return os.pathsep.join(jars)


def default_out():
    base = os.environ.get("XDG_DATA_HOME") or (Path.home() / ".local" / "share")
    return Path(base) / "plasma-client" / "data" / f"creative-{PROTOCOL}.txt"


def main(args):
    out_path = Path(args[0]) if args else default_out()
    cp = classpath()
    with tempfile.TemporaryDirectory() as build:
        subprocess.run(["javac", "-cp", cp, "-d", build,
                        str(HERE / "creative" / "CreativeTabsExport.java")], check=True)
        result = subprocess.run(["java", "-cp", build + os.pathsep + cp, "CreativeTabsExport"],
                                cwd=build, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(result.stderr[-3000:])
    lines = [line for line in result.stdout.splitlines()
             if line.startswith(("tab\t", "item\t", "search\t"))]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n")
    tabs = sum(1 for line in lines if line.startswith("tab\t"))
    items = sum(1 for line in lines if line.startswith("item\t"))
    print(f"{out_path}: {tabs} tabs, {items} stacks")


if __name__ == "__main__":
    main(sys.argv[1:])
