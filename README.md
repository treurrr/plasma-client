# Plodder Client

A Minecraft: Java Edition client written from scratch in C++23 and Vulkan.

Clean-room. No decompiled game code is read or used: the protocol comes from the
packet reports the official server jar generates itself, block geometry from the
datapack JSON in the jar, and textures from the player's own installation at run
time. Nothing from Mojang is in this repository, and nothing from Mojang ships
with the client.

![A world rendered by the client](docs/preview/world.png)

## State

It joins a real server, stays in the play state, and renders the world it is
sent. Chat works both ways. There is no window yet, no movement, and no
singleplayer; frames are written to PNG.

Measured on an RTX 4070 Laptop, 1920x1080, 200 chunks of overworld, 2.04 million
triangles:

| | |
|---|---|
| frame | 0.247 ms, 4050 fps |
| 1% low | 0.320 ms, 3126 fps |
| meshing | 1.35 ms per chunk |
| vertex | 16 bytes |

That measures the draw path and nothing else - no game logic, no entities, no
interface, and the world sits in one static buffer - so it is not what a
finished client will show.

Protocols 774 (1.21.11) and 776 (26.2) are supported, meaning a generated packet
table exists and a join against a real server of that version has been tested.

## Building

Needs a C++23 compiler, CMake, Ninja, Vulkan headers and loader, glslang, and
zlib. Debug builds turn on AddressSanitizer and UBSan.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Block geometry and textures are baked once from a Minecraft installation you
already own. `tools/find_minecraft.py` locates one; Modrinth App, Prism, MultiMC
and `~/.minecraft` are searched, and the version is confirmed from the jar rather
than from a directory name.

```sh
jar=$(python3 tools/find_minecraft.py 1.21.11)
python3 tools/bake_block_table.py "$jar" reports/blocks.json 774 blocks-774.plbk
python3 tools/bake_atlas.py "$jar" blocks-774.plbk atlas-774.plat
```

## Running

```sh
plodder-client join <host>[:port] [name]        # join and stay in the play state
plodder-client bench <host>[:port]              # frame time and fps
plodder-client worldshot <host>[:port] out.png  # render the world to a file
plodder-client showcase out.png <state>...      # render given blocks on their own
plodder-client gpu                              # adapters and features
```

`showcase` is how rendering bugs get found here. Blocks whose texture
orientation you know - a log, a grass block, a furnace - show a mistake at a
glance that a landscape hides completely.

![Logs on three axes, and a grass block](docs/preview/blocks.png)

## Packet ids are generated, never typed

The official server jar emits a report of every packet id; a script turns that
into a table.

```sh
java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports --output out
python3 tools/gen_packet_ids.py 774=out/reports/packets.json > src/proto/packet_ids_gen.cpp
```

This is not tidiness. While this was built, the protocol wiki listed
`select_known_packs` as 0x0F and `code_of_conduct` as 0x14, where 774 and 776
use 0x0E and 0x13 - the wiki had moved on to a newer snapshot. A client built on
those numbers logs in nowhere.

Login and configuration ids happen to match between 774 and 776, but 102 of the
139 clientbound play ids differ, and field layouts move too: `accept_teleportation`
was one varint in both, and gained position and rotation fields in 26.3.

## What this is not

It does not ask for your Microsoft password, and never will. Signing in happens
through Microsoft's own flow or a session handed over by a launcher.

It gives no information and no input that the vanilla client does not have. No
reach, no aim assist, no auto-clicker, no velocity changes, no seeing through
blocks. Features a server forbids are switched off by the server, not merely
greyed out in a menu.

It is not ready to play on. See the state section above.

## Notes

`docs/ARCHITECTURE.md` is a running account of the decisions and of the bugs
that were worth writing down - a depth test that drew the back of the world, an
atlas that bled between tiles, a face winding that hid every top face.

## Licence

GPL-3.0; see [COPYING.md](COPYING.md). The Plodder Client name and the marks in
`brand/` are excluded - fork the code, not the identity.
