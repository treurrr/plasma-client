#!/usr/bin/env python3
"""Turn a text file in data/ into a header the library compiles in.

Usage: embed_text.py <in.txt> <out.hpp> <symbol>
"""
import sys


def main(source, target, symbol):
    data = open(source, "rb").read()
    body = ",".join(str(b) for b in data)
    with open(target, "w") as out:
        out.write("#pragma once\n#include <string_view>\n")
        out.write(f"inline constexpr char {symbol}_bytes[] = {{{body},0}};\n")
        out.write(f"inline constexpr std::string_view {symbol}{{{symbol}_bytes, {len(data)}}};\n")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
