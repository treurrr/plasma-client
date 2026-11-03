# Plasma Client

A Minecraft: Java Edition client written from scratch in C++23 and Vulkan.

Clean-room. No decompiled game code is read or used: the protocol comes from the
packet reports the official server jar generates itself, block geometry from the
datapack JSON in the jar, and textures from the player's own installation at run
time. Nothing from Mojang is in this repository, and nothing from Mojang ships
with the client.

![A world rendered by the client](docs/preview/world.png)

## State

It joins a real server, opens a window, and lets you walk through the world it
streams. Chat works both ways, movement is reported to the server, and chunks
arriving while you play are meshed on a worker and appended to the gpu rather
than rebuilding everything. A heads-up display reports what the frame actually
cost, and a feature menu lists what the client has. Entities are tracked as the
server reports them but not drawn, the menu shows state rather than changing it,
there is no singleplayer, and joining still needs a token from elsewhere: the
encryption handshake is implemented, the Microsoft sign-in that precedes it is
not.

Measured on an RTX 4070 Laptop with 16 threads, 1920x1080, 200 chunks of
overworld, 2.06 million triangles, 862k of them drawn after culling:

| | |
|---|---|
| frame, gpu | 0.257 ms |
| frame, cpu beside it | 0.275 ms mean, 0.116 ms median |
| 1% low, gpu | 0.335 ms |
| meshing | 0.17 ms per chunk, across 16 threads |
| vertex | 16 bytes |

Both halves are given on purpose. The gpu figure alone was published here for
a while, and it was the one part that was never the problem: a command buffer
and a fence were being allocated every frame, costing 0.854 ms of cpu against
0.164 ms of gpu. Quoting the gpu number by itself overstates the framerate by
about three times.

This still measures the draw path and nothing else - no game logic, no
entities, no interface - so it is not what a finished client will show.

Protocols 774 (1.21.11) and 776 (26.2) are supported, meaning a generated packet
table exists and a join against a real server of that version has been tested.

## Signing in

Joining a server in online mode needs a Microsoft account. The whole flow is
here - device code, Xbox Live, XSTS, Mojang, and the profile request that is
the actual proof the account owns the game - but a build has to carry an
application id of its own:

```sh
export PLASMA_MSA_CLIENT_ID=<your azure application id>
plasma-client login
```

Register the application under Microsoft Entra as a public client with the
device-code flow enabled; a client secret is neither needed nor wanted. Mojang
approves applications separately before they may sign players in for real.
Without an id the client says so and refuses rather than pretending.

A build configured with `-DMCPP_DEV_OFFLINE=ON` skips the ownership check and
can only join servers in offline mode. It is for testing and never ships.

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
plasma-client play <host>[:port]               # open a window and walk around
plasma-client join <host>[:port] [name]        # join and stay in the play state
plasma-client bench <host>[:port]              # frame time and fps
plasma-client worldshot <host>[:port] out.png  # render the world to a file
plasma-client showcase out.png <state>...      # render given blocks on their own
plasma-client gpu                              # adapters and features
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

GPL-3.0; see [COPYING.md](COPYING.md). The Plasma Client name and the marks in
`brand/` are excluded - fork the code, not the identity.
