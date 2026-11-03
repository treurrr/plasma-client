#!/usr/bin/env python3
"""Ask the game for every block state's shapes and write collision_shapes-<protocol>.txt.

Usage: bake_shapes.py [out]

By default the table goes into the data directory beside the other baked
data; it is never committed.

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
    if args:
        out = Path(args[0])
    else:
        base = os.environ.get("XDG_DATA_HOME") or (Path.home() / ".local" / "share")
        out = Path(base) / "plasma-client" / "data" / "collision_shapes-774.txt"
    out.parent.mkdir(parents=True, exist_ok=True)
    cp = classpath()
    with tempfile.TemporaryDirectory() as classes:
        subprocess.run(["javac", "-nowarn", "-cp", cp, "-d", classes, str(HERE / "BlockOracle.java")],
                       check=True)
        subprocess.run(["java", "-cp", classes + os.pathsep + cp, "BlockOracle", str(out.resolve())],
                       check=True, cwd=classes)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
