#!/usr/bin/env python3
"""Write which blocks carry which block entity, by asking the game.

Usage: export_block_entities.py <out.txt>

Compiles ExportBlockEntities.java against the remapped client jar (the one
tools/oracle/run.py uses) and the libraries its launcher lists, runs it, and
writes what it prints: one `<block> <block entity type>` per line, tab
separated. bake_block_entities.py turns that and the server's block report
into the table the client reads; neither is committed here.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "oracle"))
from export_models import VERSION, classpath  # noqa: E402
from run import LIBRARY_ROOTS, reference_jar  # noqa: E402

HEADER = """\
# Which block carries which block entity, written by
# tools/export/export_block_entities.py.
"""


def main(args):
    if len(args) != 1:
        sys.exit(__doc__)
    out = Path(args[0])
    launcher = next((root.parent for root in LIBRARY_ROOTS
                     if (root.parent / "versions" / VERSION / f"{VERSION}.json").is_file()), None)
    if launcher is None:
        sys.exit(f"no launcher holds {VERSION}'s libraries")
    cp = classpath(launcher, reference_jar())
    source = HERE / "ExportBlockEntities.java"
    with tempfile.TemporaryDirectory() as classes:
        subprocess.run(["javac", "-nowarn", "-d", classes, "-cp", cp, str(source)], check=True)
        with tempfile.TemporaryDirectory() as work:
            result = subprocess.run(["java", "-cp", classes + os.pathsep + cp,
                                     "ExportBlockEntities"], cwd=work, check=True,
                                    capture_output=True, text=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(HEADER + result.stdout)
    print(f"{out}: {result.stdout.count(chr(10))} blocks")


if __name__ == "__main__":
    main(sys.argv[1:])
