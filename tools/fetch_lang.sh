#!/usr/bin/env bash
# Copy Minecraft's English language file out of a client jar you already own.
# The file is a game asset and is deliberately not part of this repository.
set -euo pipefail

jar=${1:-}
if [[ -z $jar ]]; then
    echo "usage: $0 <path/to/client.jar>" >&2
    echo "for example: $0 ~/.minecraft/versions/1.21.11/1.21.11.jar" >&2
    exit 2
fi

dest=${XDG_DATA_HOME:-$HOME/.local/share}/plasma-client/lang
mkdir -p "$dest"
unzip -o -j "$jar" 'assets/minecraft/lang/en_us.json' -d "$dest"
echo "placed in $dest/en_us.json"
