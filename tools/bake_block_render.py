#!/usr/bin/env python3
"""Ask the game itself how each block state is lit, layered and offset.

Usage: bake_block_render.py <out.txt>

Compiles tools/java/BlockRenderExport.java against the player's own remapped
client jar (~/mc-reference/<version>/client-named.jar) and the launcher's
libraries, runs it headless and writes its table: one line per block state
with its render layer, the flags ambient occlusion reads, its light emission
and opacity, its random offset and its fluid. The numbers are the game's;
nothing of the game is copied into this repository.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "oracle"))
from run import classpath  # noqa: E402


def main(args):
    if len(args) != 1:
        sys.exit(__doc__)
    out = Path(args[0])
    cp = classpath()
    with tempfile.TemporaryDirectory() as classes:
        subprocess.run(["javac", "-cp", cp, "-d", classes, str(HERE / "java" / "BlockRenderExport.java")],
                       check=True)
        table = subprocess.run(["java", "-Djava.awt.headless=true", "-cp", classes + os.pathsep + cp,
                                "BlockRenderExport"], capture_output=True, text=True, check=True,
                               cwd=classes).stdout
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(table)
    print(f"{out}: {table.count(chr(10))} states")


if __name__ == "__main__":
    main(sys.argv[1:])
