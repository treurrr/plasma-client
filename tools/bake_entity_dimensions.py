#!/usr/bin/env python3
"""Ask the game for every entity type's base size.

Usage: bake_entity_dimensions.py <out.txt>

Compiles tools/java/EntityDimensionsExport.java against the player's own
remapped client jar (the class path tools/oracle/run.py uses), runs it
headless and writes one row per entity type, sorted by registry name: width,
height, eye height, mob category, whether /summon may make it, and
fire_immune where it is. Kept beside the other baked data, never committed.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "oracle"))
from run import classpath  # noqa: E402

HEADER = """\
# Every entity type's base size in blocks, from the game's own entity type
# table: width, height, eye height (0.85 of the height where the game sets
# none), mob category, whether /summon may make it, and fire_immune where it
# is. A baby or a scaled entity multiplies these; pose sizes are not here.
# name width height eye_height category summonable [fire_immune]
"""


def main(args):
    if len(args) != 1:
        sys.exit(__doc__)
    out = Path(args[0])
    cp = classpath()
    with tempfile.TemporaryDirectory() as build:
        subprocess.run(["javac", "-cp", cp, "-d", build, str(HERE / "java" / "EntityDimensionsExport.java")],
                       check=True)
        printed = subprocess.run(["java", "-Djava.awt.headless=true", "-cp", build + os.pathsep + cp,
                                  "EntityDimensionsExport"],
                                 capture_output=True, text=True, check=True, cwd=build).stdout
    rows = [line[line.find("ROW ") + 4:] for line in printed.splitlines() if "ROW " in line]
    if len(rows) < 100:
        sys.exit(f"the game printed {len(rows)} entity types; nothing written")
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(".tmp")
    tmp.write_text(HEADER + "\n".join(rows) + "\n")
    tmp.replace(out)
    print(f"{out}: {len(rows)} entity types")


if __name__ == "__main__":
    main(sys.argv[1:])
