#!/usr/bin/env python3
"""Run an oracle against the game's own code and its C++ probe, and compare.

Usage: run.py <Oracle> <probe binary>

The oracle is compiled against the player's own remapped client jar
(~/mc-reference/<version>/client-named.jar) and the launcher's libraries; it
prints "<what> <input> <value>" per line. The probe prints the values alone,
in the same order. Every line must match exactly.
"""
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
    for root in LIBRARY_ROOTS:
        if root.is_dir():
            jars += [str(p) for p in root.rglob("*.jar")]
            break
    return os.pathsep.join(jars)


def main(args):
    if len(args) != 2:
        sys.exit(__doc__)
    oracle, probe = args
    cp = classpath()
    with tempfile.TemporaryDirectory() as out:
        subprocess.run(["javac", "-cp", cp, "-d", out, str(HERE / f"{oracle}.java")], check=True)
        expected = subprocess.run(["java", "-cp", out + os.pathsep + cp, oracle],
                                  capture_output=True, text=True, check=True).stdout.splitlines()
    actual = subprocess.run([probe], capture_output=True, text=True, check=True).stdout.splitlines()
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
