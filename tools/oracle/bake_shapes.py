#!/usr/bin/env python3
"""Ask the game for every block state's shapes and write data/collision_shapes.txt.

Usage: bake_shapes.py [out]

Compiles BlockOracle against the player's own remapped client jar, as run.py
does, and lets it write the table.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run import HERE, classpath  # noqa: E402


def main(args):
    out = Path(args[0]) if args else HERE.parent.parent / "data" / "collision_shapes.txt"
    cp = classpath()
    with tempfile.TemporaryDirectory() as classes:
        subprocess.run(["javac", "-nowarn", "-cp", cp, "-d", classes, str(HERE / "BlockOracle.java")],
                       check=True)
        subprocess.run(["java", "-cp", classes + os.pathsep + cp, "BlockOracle", str(out.resolve())],
                       check=True, cwd=classes)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
