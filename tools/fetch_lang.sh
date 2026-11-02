#!/usr/bin/env bash
# Copy Minecraft's English language file out of a client jar you already own.
# The file is a game asset and is deliberately not part of this repository.
set -euo pipefail

jar=${1:-}
if [[ -z $jar ]]; then
    echo "gebruik: $0 <pad/naar/client.jar>" >&2
    echo "bijvoorbeeld: $0 ~/.minecraft/versions/1.21.11/1.21.11.jar" >&2
    exit 2
fi

dest=${XDG_DATA_HOME:-$HOME/.local/share}/crit-client/lang
mkdir -p "$dest"
unzip -o -j "$jar" 'assets/minecraft/lang/en_us.json' -d "$dest"
echo "geplaatst in $dest/en_us.json"
