#!/usr/bin/env python3
"""Refuse game files in what is about to be committed.

Usage: check_public_tree.py [--all]

Mojang's code and assets are read from the player's own installation and from
~/mc-reference; none of it may enter this repository. Without --all only the
staged files are checked, which is what the pre-commit hook runs.
"""
import re
import subprocess
import sys

BINARY = re.compile(r"\.(jar|class|ogg|nbt|mca|mcmeta)$", re.IGNORECASE)
GAME_PATH = re.compile(r"(^|/)(assets/minecraft|data/minecraft)/")
GAME_SOURCE = re.compile(rb"^\s*package\s+(net\.minecraft|com\.mojang)\b", re.MULTILINE)


def files(everything):
    args = ["git", "ls-files"] if everything else [
        "git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"]
    return [line for line in subprocess.run(args, capture_output=True, text=True,
                                            check=True).stdout.splitlines() if line]


def blob(path, everything):
    spec = f"HEAD:{path}" if everything else f":{path}"
    result = subprocess.run(["git", "show", spec], capture_output=True)
    return result.stdout if result.returncode == 0 else b""


def main(args):
    everything = "--all" in args
    refused = []
    for path in files(everything):
        if BINARY.search(path) or GAME_PATH.search(path):
            refused.append(f"{path}: a game file by its name")
        elif path.endswith(".java") and GAME_SOURCE.search(blob(path, everything)[:4096]):
            refused.append(f"{path}: Mojang's own source")
    for line in refused:
        print(f"refused: {line}", file=sys.stderr)
    return 1 if refused else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
