#!/usr/bin/env python3
"""Bake what breaking a block shows besides the block: its terrain particles
and the ten crack textures.

Usage: bake_particles.py <client.jar> <reports/blocks.json> <out.txt> <cracks dir>

Out comes one line per block state,
    <id> <terrain> <invisible> <colour> <particle texture>
the first three asked of the game itself (tools/java/ParticleExport.java, see
there), the texture the particle icon of the state's block model, the way the
model shaper picks it: the chosen variant's first model, or for a multipart
block the first part in the file whatever the state. "-" when there is none.
The crack textures are copied as they are into <cracks dir>, which stays
beside the other baked data and is never committed.
"""
import json
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "oracle"))
sys.path.insert(0, str(HERE))
from run import classpath  # noqa: E402
from resolve_block_models import Assets, deref_texture, pick_entries  # noqa: E402


def particle_texture(assets, name, properties):
    blockstate = assets.blockstate(name)
    if blockstate is None:
        return "-"
    if "multipart" in blockstate:
        parts = blockstate["multipart"]
        if not parts:
            return "-"
        apply = parts[0]["apply"]
        entry = apply if isinstance(apply, dict) else apply[0]
    else:
        entries = pick_entries(blockstate, properties)
        if not entries:
            return "-"
        entry = entries[0]
    model = assets.resolve_model(entry["model"])
    if model is None:
        return "-"
    texture = deref_texture("#particle", model["textures"])
    return texture if isinstance(texture, str) and not texture.startswith("#") else "-"


def main(args):
    if len(args) != 4:
        sys.exit(__doc__)
    jar, report, out, cracks = args[0], Path(args[1]), Path(args[2]), Path(args[3])
    cp = classpath()
    with tempfile.TemporaryDirectory() as classes:
        subprocess.run(["javac", "-cp", cp, "-d", classes, str(HERE / "java" / "ParticleExport.java")],
                       check=True)
        asked = subprocess.run(["java", "-Djava.awt.headless=true", "-cp", classes + os.pathsep + cp,
                                "ParticleExport"], capture_output=True, text=True, check=True,
                               cwd=classes).stdout.splitlines()

    assets = Assets(jar)
    states = {}
    for name, info in json.loads(report.read_text()).items():
        for state in info.get("states", []):
            states[state["id"]] = (name, state.get("properties", {}))
    lines = []
    for line in asked:
        state = int(line.split(" ", 1)[0])
        name, properties = states[state]
        texture = particle_texture(assets, name, properties)
        if texture != "-" and ":" not in texture:
            texture = "minecraft:" + texture
        lines.append(f"{line} {texture}\n")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("".join(lines))
    print(f"{out}: {len(lines)} states")

    cracks.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(jar) as archive:
        for stage in range(10):
            name = f"destroy_stage_{stage}.png"
            (cracks / name).write_bytes(archive.read(f"assets/minecraft/textures/block/{name}"))
    print(f"{cracks}: 10 crack textures")


if __name__ == "__main__":
    main(sys.argv[1:])
