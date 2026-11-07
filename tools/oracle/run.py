#!/usr/bin/env python3
"""Run an oracle against the game's own code and its C++ probe, and compare.

Usage: run.py <Oracle> <probe binary>

The oracle is compiled against the player's own remapped client jar
(~/mc-reference/<version>/client-named.jar) and the launcher's libraries; it
prints "<what> <input> <value>" per line. The probe prints the values alone,
in the same order. Every line must match exactly.
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
VERSION = "1.21.11"
LIBRARY_ROOTS = [
    Path.home() / ".var/app/com.mojang.Minecraft/.minecraft/libraries",
    Path.home() / ".minecraft/libraries",
]


def reference_jar():
    return Path(os.environ.get("PLASMA_REFERENCE_JAR")
                or Path.home() / "mc-reference" / VERSION / "client-named.jar")


def classpath():
    jar = reference_jar()
    if not jar.exists():
        sys.exit(f"{jar} is missing: run ~/mc-reference/decompile.sh first")
    jars = [str(jar)]
    # Only the versions this release lists: the launcher keeps every version
    # it ever fetched, and a newer datafixerupper breaks the game's codecs.
    for root in LIBRARY_ROOTS:
        listing = root.parent / "versions" / VERSION / f"{VERSION}.json"
        if not listing.is_file():
            continue
        for library in json.loads(listing.read_text())["libraries"]:
            artifact = library.get("downloads", {}).get("artifact")
            if artifact and (root / artifact["path"]).is_file():
                jars.append(str(root / artifact["path"]))
        break
    return os.pathsep.join(jars)


def main(args):
    if len(args) != 2:
        sys.exit(__doc__)
    oracle, probe = args
    cp = classpath()
    with tempfile.TemporaryDirectory() as out:
        subprocess.run(["javac", "-cp", cp, "-d", out, str(HERE / f"{oracle}.java")], check=True)
        # oracle.dir so an oracle that reads a table of cases can find it.
        expected = subprocess.run(["java", "-cp", out + os.pathsep + cp,
                                   f"-Doracle.dir={HERE}", oracle],
                                  capture_output=True, text=True, check=True, cwd=out).stdout.splitlines()
    # A probe that decodes what the game wrote reads the oracle's lines on stdin.
    actual = subprocess.run([probe], input="\n".join(expected) + "\n", capture_output=True,
                            text=True, check=True).stdout.splitlines()
    if len(expected) != len(actual):
        print(f"{oracle}: {len(expected)} lines from the game, {len(actual)} from the probe")
        return 1
    wrong = [(e, a) for e, a in zip(expected, actual) if e.rsplit(" ", 1)[1] != a]
    for e, a in wrong:
        print(f"  differs: {e}  probe {a}")
    print(f"{oracle}: {len(expected) - len(wrong)} of {len(expected)} match the game")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
