#!/usr/bin/env python3
"""Write animations-<protocol>.txt by asking the game for its keyframe animations.

Usage: export_animations.py <out.txt>

Compiles ExportAnimations.java against the remapped client jar (the one
tools/oracle/run.py uses) and the libraries its launcher lists, runs it, and
writes what it prints, with a header saying what the file is. bake_all puts
it in the data directory; it is never committed.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "oracle"))
from run import reference_jar  # noqa: E402
from export_models import classpath, LIBRARY_ROOTS, VERSION  # noqa: E402

HEADER = """\
# Keyframe animations, one [section] per `AnimationDefinition` the game holds in
# net.minecraft.client.animation.definitions, written by
# tools/export/export_animations.py.
#
#   [<Class>.<FIELD>]
#   length <seconds>
#   loop                                             only when it loops
#   channel <bone> <position|rotation|scale>
#     key <timestamp> <x> <y> <z> <linear|catmullrom>
#     key <timestamp> <x> <y> <z> -> <x> <y> <z> <linear|catmullrom>
#
# A key's timestamp is in seconds and its three floats are what
# KeyframeAnimations.posVec, .degreeVec and .scaleVec already made of the
# animator's numbers: pixels with y negated, radians, and scale minus one.
# The second form is a keyframe whose pre-target differs from its post-target,
# the first three floats being the pre-target. Bones are the short part names
# ModelPart.createPartLookup answers to, not paths; channels of one bone are in
# the order the definition added them, bones in name order.
"""


def main(args):
    if len(args) != 1:
        sys.exit(__doc__)
    out = Path(args[0])
    launcher = next((root.parent for root in LIBRARY_ROOTS
                     if (root.parent / "versions" / VERSION / f"{VERSION}.json").is_file()), None)
    if launcher is None:
        sys.exit(f"no launcher holds {VERSION}'s libraries")
    jar = reference_jar()
    source = Path(__file__).with_name("ExportAnimations.java")
    cp = classpath(launcher, jar)
    with tempfile.TemporaryDirectory() as classes:
        subprocess.run(["javac", "-nowarn", "-d", classes, "-cp", cp, str(source)], check=True)
        with tempfile.TemporaryDirectory() as work:
            result = subprocess.run(
                ["java", "-cp", classes + os.pathsep + cp, "ExportAnimations", str(jar)],
                cwd=work, check=True, capture_output=True, text=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(HEADER + result.stdout)
    keys = sum(line.lstrip().startswith("key ") for line in result.stdout.splitlines())
    print(f"{out}: {result.stdout.count(chr(10) + '[')} animations, {keys} keyframes")


if __name__ == "__main__":
    main(sys.argv[1:])
