#!/usr/bin/env python3
"""Write data/entity_models.txt by asking the game for its model layers.

Usage: export_models.py <client jar with Mojang's names> <launcher dir> <out.txt>

The launcher dir is the one holding versions/ and libraries/ (~/.minecraft).
Compiles ExportModels.java against the jar and its libraries, runs it, and
writes what it prints, with a header saying what the file is.
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

VERSION = "1.21.11"
HEADER = """\
# Entity model layers, one [section] per layer as the game builds it, written by
# tools/export/export_models.py. Model space: pixels, y down, facing -z.
#
#   texture <width> <height>
#   part <path> [pivot x y z] [rotate x y z] [scale x y z]   radians
#   box x y z  w h d  uv u v [grow g | grow x y z] [mirror] [texsize w h] [faces <dunswe>]
"""


def classpath(launcher, jar):
    description = json.loads((launcher / "versions" / VERSION / f"{VERSION}.json").read_text())
    paths = []
    for library in description["libraries"]:
        artifact = library.get("downloads", {}).get("artifact")
        if artifact is None:
            continue
        path = launcher / "libraries" / artifact["path"]
        if path.exists():
            paths.append(str(path))
    paths.append(str(jar))
    return os.pathsep.join(paths)


def main(args):
    if len(args) != 3:
        sys.exit(__doc__)
    jar, launcher, out = Path(args[0]), Path(args[1]), Path(args[2])
    source = Path(__file__).with_name("ExportModels.java")
    cp = classpath(launcher, jar)
    with tempfile.TemporaryDirectory() as classes:
        subprocess.run(["javac", "-nowarn", "-d", classes, "-cp", cp, str(source)], check=True)
        with tempfile.TemporaryDirectory() as work:
            result = subprocess.run(["java", "-cp", classes + os.pathsep + cp, "ExportModels"],
                                    cwd=work, check=True, capture_output=True, text=True)
    out.write_text(HEADER + result.stdout)
    print(f"{out}: {result.stdout.count(chr(10) + '[')} layers")


if __name__ == "__main__":
    main(sys.argv[1:])
