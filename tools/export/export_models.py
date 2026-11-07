#!/usr/bin/env python3
"""Write entity_models-<protocol>.txt by asking the game for its model layers.

Usage: export_models.py <out.txt>

Compiles ExportModels.java against the remapped client jar (the one
tools/oracle/run.py uses) and the libraries its launcher lists, runs it, and
writes what it prints, with a header saying what the file is. bake_all puts
it in the data directory; it is never committed.
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "oracle"))
from run import LIBRARY_ROOTS, reference_jar  # noqa: E402

VERSION = "1.21.11"
HEADER = """\
# Entity model layers, one [section] per layer as the game builds it, written by
# tools/export/export_models.py. Model space: pixels, y down, facing -z.
#
#   texture <width> <height>
#   part <path> [pivot x y z] [rotate x y z] [scale x y z]   radians
#
# The layer's own root is ".": a layer may have a part named "root" of its own,
# as the allay, the frog, the vex, the creaking and the nautilus do, and the
# two must not collide.
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
    if len(args) != 1:
        sys.exit(__doc__)
    out = Path(args[0])
    launcher = next((root.parent for root in LIBRARY_ROOTS
                     if (root.parent / "versions" / VERSION / f"{VERSION}.json").is_file()), None)
    if launcher is None:
        sys.exit(f"no launcher holds {VERSION}'s libraries")
    jar = reference_jar()
    source = Path(__file__).with_name("ExportModels.java")
    cp = classpath(launcher, jar)
    with tempfile.TemporaryDirectory() as classes:
        subprocess.run(["javac", "-nowarn", "-d", classes, "-cp", cp, str(source)], check=True)
        with tempfile.TemporaryDirectory() as work:
            result = subprocess.run(["java", "-cp", classes + os.pathsep + cp, "ExportModels"],
                                    cwd=work, check=True, capture_output=True, text=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(HEADER + result.stdout)
    print(f"{out}: {result.stdout.count(chr(10) + '[')} layers")


if __name__ == "__main__":
    main(sys.argv[1:])
