#!/usr/bin/env python3
"""Write every block state with its block and properties, and every item's
tool rules, as the reports list them.

Usage: bake_states.py <reports dir> <protocol> <out dir>

Collision shapes and dig times depend on a state's properties (a slab's half,
a stair's facing), which the block table does not keep. Out come
states-<protocol>.txt, one state per line:
    <state id> <block registry id> <name> <key=value,...>
and tools-<protocol>.txt, one rule per line, for every item that is a tool:
    <item registry id> <name> <blocks> <speed or -> <drops: 1, 0 or ->
where <blocks> is a #tag, or block names joined by commas. A first line per
item with blocks "*" carries the tool's default speed.
"""
import json
import sys
from pathlib import Path


def main(reports, protocol, out):
    reports = Path(reports)
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    blocks = json.loads((reports / "blocks.json").read_text())
    registries = json.loads((reports / "registries.json").read_text())
    block_ids = {name: entry["protocol_id"]
                 for name, entry in registries["minecraft:block"]["entries"].items()}
    lines = []
    for name, block in blocks.items():
        for state in block["states"]:
            props = ",".join(f"{k}={v}" for k, v in state.get("properties", {}).items())
            lines.append((state["id"], f"{state['id']}\t{block_ids[name]}\t{name}\t{props}"))
    lines.sort()
    states = out / f"states-{protocol}.txt"
    states.write_text("".join(line + "\n" for _, line in lines))
    print(states)

    items = json.loads((reports / "items.json").read_text())
    item_ids = {name: entry["protocol_id"]
                for name, entry in registries["minecraft:item"]["entries"].items()}
    rules = []
    for name, item in items.items():
        tool = item.get("components", {}).get("minecraft:tool")
        if tool is None:
            continue
        item_id = item_ids[name]
        rules.append(f"{item_id}\t{name}\t*\t{tool.get('default_mining_speed', 1.0)}\t-")
        for rule in tool.get("rules", []):
            blocks_of = rule["blocks"]
            if isinstance(blocks_of, list):
                blocks_of = ",".join(blocks_of)
            speed = rule.get("speed", "-")
            drops = rule.get("correct_for_drops")
            drops = "-" if drops is None else ("1" if drops else "0")
            rules.append(f"{item_id}\t{name}\t{blocks_of}\t{speed}\t{drops}")
    tools = out / f"tools-{protocol}.txt"
    tools.write_text("".join(line + "\n" for line in rules))
    print(tools)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2]), sys.argv[3])
