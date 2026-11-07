#!/usr/bin/env python3
"""Refuse game files, and the private add-on, in what is about to be committed.

Usage: check_public_tree.py [--all]

Mojang's code and assets are read from the player's own installation and from
~/mc-reference; none of it may enter this repository. Neither may the owner's
private add-on: this client promises in public that it carries nothing a server
would ban, and one leaked file would make that promise worthless. Without --all
only the staged files are checked, which is what the pre-commit hook runs.
"""
import re
import subprocess
import sys

BINARY = re.compile(r"\.(jar|class|ogg|nbt|mca|mcmeta)$", re.IGNORECASE)
GAME_PATH = re.compile(r"(^|/)(assets/minecraft|data/minecraft)/")
GAME_SOURCE = re.compile(rb"^\s*package\s+(net\.minecraft|com\.mojang)\b", re.MULTILINE)
# The private add-on lives outside this tree and stays there. Its directory, and
# anything naming one of its features, is refused by name.
PRIVATE_PATH = re.compile(r"(^|/)(plasma-private|private)/")
PRIVATE_NAME = re.compile(
    r"\b(trigger[\s_-]*bot|aim[\s_-]*assist|auto[\s_-]*clicker|kill[\s_-]*aura"
    r"|(player|chest|mob|item|portal|barrier|base)[\s_-]*esp|x[\s_-]*ray"
    r"|auto[\s_-]*(crystal|anchor|steal|totem)|inv[\s_-]*totem|freecam"
    r"|reach[\s_-]*hack|sword[\s_-]*assist|(combat|mace)[\s_-]*trigger"
    r"|stun[\s_-]*slam|wind[\s_-]*pearl|spear[\s_-]*swap|base[\s_-]*finder)\b",
    re.IGNORECASE)
# Where we promise in public that we do none of it, saying the words is the point.
ALLOWED_TO_NAME = ("README.md", "docs/GAMEPLAN.md", "tools/check_public_tree.py",
                   "docs/PARITY.md", "docs/PARITY-REVIEW.md",
                   "include/mcpp/features.hpp", "src/tools/client_main.cpp")


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
        elif PRIVATE_PATH.search(path):
            refused.append(f"{path}: the private add-on does not live in this repository")
        elif path not in ALLOWED_TO_NAME and not path.startswith("brand/"):
            hit = PRIVATE_NAME.search(blob(path, everything).decode("utf-8", "ignore"))
            if hit is not None:
                refused.append(f"{path}: names a private feature ({hit.group(0)})")
    for line in refused:
        print(f"refused: {line}", file=sys.stderr)
    return 1 if refused else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
