# Architecture

## Why this document exists

Native Minecraft clients do not die of their renderer. They die of protocol
maintenance: every new version shifts packet IDs and field layouts, and a
codebase that was not built for that has to be half rewritten on every release.
AltCraft is still on 1.12.2; Polymer can only watch. Version isolation is
therefore not a later cleanup here but the first design decision.

## The evidence that this is needed

Measured between protocol 774 (1.21.11) and 776 (26.2), from Mojang's own packet
reports:

| Phase                   | Packets | Changed IDs  |
|-------------------------|---------|--------------|
| login, clientbound      | 6       | 0            |
| login, serverbound      | 5       | 0            |
| configuration, both     | 30      | 0            |
| play, clientbound       | 139     | **102**      |
| play, serverbound       | 66      | **68 of 69** |

Login and configuration sit still; the play phase shifts almost entirely. A
client that hardcodes one ID table therefore works for exactly one version.

And it is not only the IDs. `accept_teleportation` was a single VarInt in 774 and
776; in 26.3 position and rotation fields were added. Layouts are as
version-bound as numbers.

## Where the numbers come from

Not from the wiki, and not by hand. `tools/gen_packet_ids.py` reads the
`reports/packets.json` that the official server jar generates itself:

```
java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports
python3 tools/gen_packet_ids.py 774=774/packets.json 776=776/packets.json \
    > src/proto/packet_ids_gen.cpp
```

That is observable output of the game, not decompiled code. It is also
demonstrably more reliable than the documentation: while this was being built,
the wiki described `select_known_packs` as 0x0F and `code_of_conduct` as 0x14,
where 774 and 776 use 0x0E and 0x13. The wiki had run ahead onto a newer
snapshot. Anyone who had copied those numbers across would have built a client
that logs in nowhere.

Adding a version is therefore: grab the jar, generate the report, run the
generator again. No handwork, no forgotten packet.

Field layouts do come from the protocol spec, but they are checked against real
bytes. `player_chat` was read out that way, and it turned up a detail that
appears in no spec: Mojang's NBT codec wraps a bare number in a compound under
the empty key, so `{"": 5}` rather than `5`. Without that find you get "There
are  of a max of  players online".

## Layers

```
byte_buffer     wire format: big-endian ints, LEB128 varints, strings
tcp_socket      blocking socket, TCP_NODELAY, timeouts
packet_io       framing and zlib compression
json / nbt      text components in both forms
translations    Minecraft's language file, %s and %N$s
proto/          one generated ID table per version
session         the state machine: handshake -> login -> configuration -> play
features        what the player can switch on and off
```

Only `session` knows about versions. The layers below it are version-free; the
layer above it (the renderer, milestone 3) will be too.

## What the session does

Handshake with the real protocol number, then login. Compression is switched on
as soon as the server sends a threshold for it. Unknown login packets are not
ignored but rejected with a readable error, because quietly carrying on past a
packet you do not understand is exactly how you lose hours.

In the configuration phase we report zero known packs, so that the server sends
the full registries instead of assuming we already have them. Resource packs are
refused; the code of conduct is handed to the UI.

In the play phase we answer keep-alive, ping and teleport confirmations, and read
chat in both forms (`system_chat` and `player_chat`).

## Still missing

- **Online mode.** A server in online mode asks for encryption, and that needs a
  Microsoft session. The client now says so in as many words instead of
  crashing.
- **Movement.** We send no position. The vanilla server requires
  `client_tick_end` before a position packet, so that goes together with the
  player simulation.
- **World.** Chunks, blocks, entities: milestone 3, together with the renderer.
- **1.8.9.** Protocol 47 has no configuration phase. The table already has room
  for that: a missing packet is -1 rather than a hole in the code.

## Chunks

A chunk arrives as `level_chunk_with_light`: coordinates, heightmaps, a blob of
sections, block entities and light data. Each section holds the number of
non-air blocks and two paletted containers, one for blocks and one for biomes.

A paletted container is bit-packed into longs, and entries **never** straddle a
long boundary: at 5 bits, 12 fit in a long and the top 4 bits stay padding.
Three forms: `bits == 0` is a single value for the whole section, `bits <= 8`
(blocks) or `<= 3` (biomes) is indirect through a palette, and above that the
block state IDs sit in the data directly.

Two things the spec does not say (clearly), which came out of the bytes:

1. **The data array of a paletted container has no length prefix.** The
   heightmaps above it do. The length of the container data follows from the
   number of entries and the bits per entry. Read a varint here and you read the
   first long as a length and get stuck.
2. **The number of sections is not in the packet.** It follows from the height of
   the dimension, which lives in a registry we do not track. Reading sections
   until the blob is empty does not need that registry.

### How this was checked

That it parses without an exception proves nothing: shifted bit unpacking yields
plausible-looking nonsense. The check is the block distribution of 329 chunks of
real overworld, with the IDs turned into names through `blocks.json`:

| block state | blocks | block |
|-------------|--------|-------|
| 0 | 21,939,649 | air |
| 27722 | 4,247,954 | deepslate |
| 1 | 2,773,842 | stone |
| 86 | 1,157,748 | water |
| 23250 | 351,876 | tuff |
| 85 | 253,914 | bedrock |

Deepslate below stone, the stone types in the right proportion, and bedrock at
253,914 blocks = 256 x 3.0 layers per chunk, which lines up exactly with the
random pattern of roughly three layers deep. Those numbers cannot come out right
by accident.

## Block models

This is the part I underestimated in the planning, so it was the first piece of
milestone 3 to be worked out: it needs no GPU and can be checked all the way
through.

A block state becomes geometry in four steps:

1. `blocks.json` maps a state ID to a block name plus properties.
2. `assets/minecraft/blockstates/<block>.json` picks a model. Two forms:
   `variants` matches on the whole property set, `multipart` collects every part
   whose `when` condition holds (AND across keys, OR across `|`-separated
   values, plus nested `OR`/`AND`).
3. The model inherits through `parent`. Textures merge child-first; `elements`
   come from the nearest ancestor that defines them. Texture names such as `#all`
   are followed through the merged map until they reach a real path.
4. The variant also supplies `x`/`y` rotation and `uvlock`.

`tools/resolve_block_models.py` does this for all 29,671 states of 1.21.11 and
reads assets only from a local installation.

### The result, and why "empty" is not the same as "wrong"

| | states |
|---|---|
| with geometry | 26,706 (356,952 quads, 13.4 on average) |
| no model: air, fluids, barrier, light | 84 |
| legitimately empty | 2,881 |
| unresolved | 0 |

Those 2,881 were walked through rather than waved away, because an invisible
block is exactly the kind of mistake you only notice months later. Three groups:

- **Block entities.** Signs, banners, beds, chests, shulkers, heads, conduit,
  end_portal, decorated_pot, copper_golem_statue. Vanilla draws these with a
  renderer of their own; their model holds nothing but a particle texture. That
  is a separate render path, still to come.
- **Unreachable wall states.** `cobblestone_wall` has 324 states with geometry;
  only the combination of `up=false` with nothing connected is empty, and that
  exists in the registry but never in the world, because the server then sets
  `up` to true.
- **Crops that are not fully grown.** `pitcher_crop` half=upper is empty in
  stages 0-2 and has 4 quads in 3-4. That is not a bug but exactly what vanilla
  does: the plant is not yet tall enough to reach into the upper block.

## The pre-baked block table

On startup vanilla reads 1168 blockstate files and 2390 models, resolves
inheritance and texture references, and builds objects out of them. For a fixed
game version that work gives the same answer every time, so we do it once, up
front.

`tools/bake_block_table.py` writes everything into a single file that the client
`mmap`s and indexes in place. No JSON, no allocations, no object graph.

```
header        magic, version, protocol, counts
quad_offsets  an index into the quad array per state (CSR)
quads         28 bytes per quad, fixed layout
textures      offsets into the string blob
block_names   offsets into the string blob
state_block   which block each state belongs to
state_flags   no model / has geometry / full cube
strings       all names end to end, nul-terminated
```

Measured on 774: **10.34 MB, 29,671 states, 356,952 quads, 1096 textures**. The
`mmap` costs **20 µs** and walking the whole table **60 µs**. Baking takes 2.2
seconds, but that is build time, not startup time.

Coordinates are stored as `int16` in units of 1/256 of a model unit. Across all
356,952 quads the largest rounding error is **0.0019 model units = 0.00012
blocks**: on a block 100 pixels wide on screen, 0.012 pixels. The range that
actually occurs is -8 to 24 model units, comfortably inside what `int16` holds at
this scale (-128 to 128).

The `full cube` flag is what the mesher needs: 2457 states are a completely solid
cube and may cut away the adjoining face of their neighbour.

The file holds only geometry derived from a local installation. No Mojang assets
in the repository, and `tools/bake_block_table.py` needs a client jar belonging
to the player.

## Vulkan foundations

Raw Vulkan 1.3, no wrapper library. That is not stubbornness: the claims about
startup time and frame pacing depend on owning the pipeline cache, the
submission path and the memory layout ourselves.

Two choices fixed from the start:

**SPIR-V is compiled at build time and linked in.** `glslangValidator` turns it
into `.spv`, `tools/spv_to_header.py` turns that into a header with the words in
it. The client therefore compiles no shader and reads none off disk.

**The pipeline cache lives on disk** and is written out at shutdown. Measured
with one trivial pipeline:

| | building the pipeline |
|---|---|
| cold, empty cache | 6.7 ms |
| warm, 15 kB cache read in | 0.4 ms |

Seventeen times faster, and that for one pipeline. A real client has dozens, so
this scales with it.

What cannot be optimised away is creating the device itself: **175-215 ms**,
almost entirely driver initialisation. That is the floor under any startup time
we are going to claim, and vanilla pays it just as much.

### Offscreen rendering as a means of checking

The first render path is not a window but an image: render into an image, copy it
back to a host buffer and write it out as PNG (`src/image_png.cpp`, the zlib we
were linking anyway). That gives a frame you can look at instead of only count,
it runs without throwing a window across somebody else's screen, and it works on
`llvmpipe` as well when there is no gpu.

Timestamp queries measure the gpu time inside the frame itself. A 1280x720 frame
with a clear, a triangle and a full copy back costs **0.31 ms** on the 4070, most
of which is the copy back that a real frame does not do.

## Meshing

The mesher turns a chunk plus the block table into triangles. A face with a
`cullface` pointing at a neighbouring block that is a full cube is left out.

Measured on 329 chunks of real overworld:

| | |
|---|---|
| quads kept | 3,703,149 |
| quads culled | 51,961,629 (**93.3%**) |
| triangles | 7,406,298 |
| meshing | 2.08 ms per chunk |

Those 2.08 ms started at **5.97 ms**. The difference sat almost entirely in
`rotate_face`: it did floating-point maths per quad and then a linear search
through six directions, while for by far the most blocks the rotation is zero.
Short-circuiting at zero rotation, plus looking up the six neighbouring blocks
once per block instead of once per quad, is worth nearly a factor of three.

That the 93.3% is right was not settled by feel. A test builds a section of
16x16x16 full cubes and demands exactly 1536 remaining quads (six sides of 16x16)
and exactly 24,576 - 1536 culled. A silent mistake in the neighbour indexing
changes that number straight away.

### What is still not right here

Two things, both deliberately left open:

1. **503 MB of vertex data for 329 chunks.** A vertex is currently 28 bytes with
   `float` positions. That has to come down to 16 bytes with fixed-point
   positions per section, which also makes drawing per section and frustum
   culling possible.
2. **Chunk edges count as open.** Every chunk therefore emits all four of its
   side walls, even where the neighbouring chunk sits right against it. That
   calls for meshing with knowledge of the neighbours.

## Rendering the world, and the bug that nearly got through

`crit-client worldshot <server> <out.png> [chunks] [view]` logs in, collects
chunks, meshes them with their neighbours, uploads everything to the gpu and
writes out a frame. On 200 chunks: 1.9 million triangles in **1.14 ms** of gpu
time.

Handing the neighbours to the mesher halved that immediately: without knowledge
of the neighbouring chunk, every chunk emits all four of its side walls.

| | triangles | vertex data | gpu time |
|---|---|---|---|
| without neighbours | 2,675,158 | 182 MB | 1.75 ms |
| with neighbours | 1,162,650 | 79 MB | 0.86 ms |

### Why this would not have been found without pictures

The first image showed cross-sections of the terrain instead of its surface.
Three explanations looked plausible and all three were wrong: the wrong winding
order (ruled out by switching culling off: the image stayed the same), chunk
edges (ruled out: the fix halved the triangles but did not change the image), and
a camera below ground.

The real cause sat in the projection matrix. It produced **standard** depth,
near=0 and far=1, while the depth buffer was cleared to 0 and the test was
`VK_COMPARE_OP_GREATER`. That lets every fragment further away win: the client
was drawing the back of the world.

The matrix is now genuinely reversed - near=1, far=0 - which is what belongs with
that clear and that test, and which on top of that puts the float precision where
you need it.

This is the reason the first render path writes a PNG instead of opening a
window. The test passed, a frame came out, the timings were good, and it was
utterly wrong. Only looking found it.

## Textures

Like the block table, the atlas is baked up front, out of an installation the
player owns themselves. `tools/find_minecraft.py` tracks it down - Modrinth App,
Prism, MultiMC, `~/.minecraft` - and reads the version out of `version.json`
inside the jar itself rather than going by the directory name.

`tools/bake_atlas.py` reads the texture list **out of the block table**, so that
the indices of the two files run together by construction. Tiles are uniform (the
largest native size in the pack, smaller ones scaled up with nearest), which is
also what the game itself does, and which means the position of a tile is a
multiplication rather than a lookup. From an animation strip the first frame is
taken.

Result for 1.21.11: **2048x576, 32px tiles, 1096 textures, 4.73 MB**, baked in
0.2 seconds. The client maps that file and copies it to the gpu.

Not one Mojang asset sits in this repository or belongs in a download of the
client. Every player bakes from their own jar.

### Two mistakes that surfaced here

**`heavy_core.json` refers to `"texture": "all"` without the `#`.** That is a
quirk in Mojang's own asset; their loader accepts it, mine did not, and that
produced exactly one missing texture. The resolver now follows a name without the
hash as well if it is a key in the texture map.

**The first textured frames were far too dark.** The atlas sits on the gpu as
sRGB, so sampling gives linear values, and those were written into a UNORM target
that a viewer then reads as sRGB again. The render target is now sRGB too, so
that the conversion happens on write.

### Still missing

- **Water is not drawn.** Fluids have no block model and need a render path of
  their own.
- **Biome colours are fixed.** Grass, leaves and stems are multiplied by one flat
  colour. The biomes are already read out of the chunk but not yet turned into
  colours.
- **No mipmaps and no ambient occlusion.** The first gives shimmering in the
  distance, the second makes the image flatter than vanilla.

## Holes in the ground

The first textured image had holes. Two causes, both real.

**Leaves, glass and ice were culling their neighbours.** The `full cube` flag
meant "six faces that fill the whole block", and that is true of those three -
but you look through them, so nothing was left behind a tree. Geometry says
nothing about opacity. There is now a separate flag: a state may only cull once
all six of its textures are fully opaque, checked on the alpha channel while
baking. Of the 2457 full cubes, **2134** are left that may do so.

**Water was not drawn at all.** Fluids have no block model, so they sat with air
on the "no geometry" list. They now get manufactured quads: a cube with only its
top face lowered, the height taken from the `level` property. A fluid also hides
against its own kind, otherwise an ocean is full of faces between adjacent water
blocks.

That last part is not vanilla-exact yet: real fluid heights interpolate between
neighbours, and water ought to be transparent rather than opaque.

## Mipmaps, and why the atlas became an array texture

Without mipmaps everything went grainy at a distance. With mipmaps and linear
minification something else appeared: a **grid of light lines** on every block
boundary.

That is atlas bleeding. When minifying, the sampler reads past the tile boundary
at the edge of a tile and mixes the neighbouring texture in. Padding softens it
but does not fix it, because every extra mip level asks for twice as much border.

The atlas is therefore no longer a grid but an **array texture**: one layer per
texture, the layer index simply being the texture id out of the block table.
Layers cannot leak into one another by construction, mip levels are built per
texture, and uvs outside 0..1 may now repeat the way some models expect.

1098 layers of 32px with four mip levels is 5.97 MB. The spec only guarantees 256
layers; the cards here do 2048 and 8192, and the client refuses cleanly with a
readable error if a gpu cannot handle enough layers. Falling back to a grid atlas
for such cards is still open.

The sampler magnifies with nearest - that is the blocky look - and minifies with
linear plus anisotropy.

## Water in layers

After that grid, horizontal lines were left over, one per block layer, straight
through every body of water. That was not a filtering problem but the geometry: I
was lowering the top face of **every** water block to 14/16, including blocks
that sit under water. Between two layers there was therefore a 2/16 gap you
looked straight through.

Only the topmost block of a water column has a lowered surface. That is not a
property of the block state but of its neighbour, so it belongs in the mesher,
which does know the neighbours: if there is water above, the block fills its full
height.

## The bug that took away every top face

Every top face in the whole renderer was missing. In a landscape that does not
stand out - you see sides, plants and water and it *looks* like terrain - but a
single block test made it obvious at once: from straight above you looked through
the block to the floor, with only a rim of the side faces.

The cause sat in `frontFace`. The winding of the six faces is consistent, and
that is precisely what sent me the wrong way: working it through in screen space
gave exactly one visible face per camera, neatly. But **Vulkan determines the
winding direction in framebuffer coordinates, where Y points down**. What runs
anticlockwise in ordinary mathematical orientation therefore runs clockwise
there. With `VK_FRONT_FACE_COUNTER_CLOCKWISE` the faces pointing at the camera
were culled and the ones facing away were drawn.

### How this surfaced

Not by looking at landscapes. `crit-client showcase` builds a section from
given block states without a server and renders it from the front, from the side,
from above or at an angle. On blocks whose texture orientation you know - a log,
a grass block, a furnace - you see at a glance what is wrong.

That tool also corrected a wrong diagnosis of mine. The log looked as if it had
two faults, bark in the wrong direction and rings on a side face, until it turned
out that state 136 is `axis=x`: a log lying down, where that is exactly right.

## Full cubes with extra faces

`is_full_cube` demanded exactly six quads. A grass block has ten - six sides plus
four tinted overlays - and so missed out, although it is every bit as solid as
stone. Plain grass therefore culled nothing anywhere. The test now looks per
direction for a quad that covers the whole block, instead of counting quads.

## Meshing per section, and frustum culling

The mesher used to give one buffer per chunk. Now it gives one per **section** of
16x16x16, with positions relative to the corner of that section and a bounding
box of its own. That is the unit the renderer can skip, upload separately and
throw away on its own - a chunk is too coarse for that, being 384 blocks tall and
almost never fully in view.

The renderer packs all sections into one vertex and one index buffer and draws
out of them per section with `vkCmdDrawIndexed(firstIndex, vertexOffset)`. The
corner of the section comes along as a push constant, so that the same geometry
can stand anywhere.

Frustum culling tests the bounding box against the four side planes of the view
volume, taken from the combined matrix (Gribb-Hartmann). A box is only dropped
once it lies entirely behind a plane, so the corner that sticks out furthest in
front of that plane decides.

Measured at 1920x1080, 200 chunks, 2.04 million triangles:

| | without | with |
|---|---|---|
| triangles drawn | 2,039,102 | 843,650 (58% fewer) |
| average frame time | 0.629 ms | **0.331 ms** |
| fps | 1589 | **3017** |
| 1% low | 1453 fps | **2414 fps** |

### Why two renders of the same scene differed anyway

The first picture after the change differed on 0.134% of the pixels, in a wedge
at the horizon. That looked like culling taking too much away.

It was not. Switching culling off gave **exactly the same difference**, and so
did two runs of the same binary: the server does not send the same first 200
chunks every time, because the world is still generating. With and without
culling the image is pixel-identical.

The lesson is in the tooling: comparing images from different runs says nothing
as long as the input is not fixed. A fixed chunk set on disk belongs with the
benchmark harness.

## The vertex, from 28 bytes to 16

Once positions are per section, they no longer need to be floats: a section is 16
blocks wide and a model sticks out of it by at most one block. So fixed point in
1/2048 of a block, offset by one block, in `uint16`. That is coarser than the
baked geometry (1/4096 of a block), but the error stays under 0.0005 blocks - far
under a screen pixel.

```
uint16 position[3]   xyz, fixed point
uint16 packed        face | tint kind | corner shadow
uint16 uv[2]         normalised, the format does the division
uint16 texture       layer in the array texture
uint16 light         reserved for sky and block light
```

The uvs are stored as `R16G16_UNORM`, so that the GPU scales them to 0..1 itself
and no division is needed in the shader. Position and flags share one
`R16G16B16A16`, because a three-component 16-bit vertex format is not supported
everywhere.

## The three steps added up

Same scene, 1920x1080, 200 chunks, 2.04 million triangles, RTX 4070 Laptop:

| step | frame time | fps | 1% low |
|------|------------|-----|--------|
| drawing everything, 28-byte vertex | 0.629 ms | 1589 | 1453 |
| per section + frustum culling | 0.331 ms | 3017 | 2414 |
| vertex down to 16 bytes | **0.247 ms** | **4050** | **3126** |

Two and a half times faster, and the vertex data went from 139 to 90 MB.

This measures the draw path and nothing else: no game logic, no entities, no UI,
and the world sits on the card as one static buffer. So it is not a figure to
advertise with. What it does say is where the work is: **meshing costs 4 ms per
chunk, sixteen times a whole frame.** That is where the next gain lies, not in
the renderer.


## Obfuscation differs by version, and that decides our testing route

Horizon verifies their behaviour with **oracles**: they compile a few lines of
Java against the official jar, run that alongside their own implementation and
demand identical output. That works for their target version 26.3 because it
ships **unobfuscated**: the classes are simply called what they are called.

For our target version things are different. Measured on 2 November:

| | obfuscated | official mappings from Mojang |
|---|---|---|
| 1.21.11 | yes, 10,291 classes (`fzu$a.class`) | client 11.8 MB, server 8.7 MB |
| 26.2 | no | not present, not needed |

Mojang stopped obfuscating with the 26 series and publishes no mappings there any
more. For 1.21.11 they do exist, and that is exactly the kind of choice that must
not stay implicit: using mappings to call an oracle falls in a grey area of our
clean-room rule.

That choice is not urgent now. **The whole networking side needs no oracle** -
there the server is the referee, and it says so immediately when we send
something wrong. It becomes urgent at worldgen, where nothing corrects you.

What is usable straight away, and entirely clean-room: Horizon tests online mode
with a **mock Mojang service**. A local mock of the discovery document and of
`join`/`hasJoined`, a real vanilla server in online mode pointed at that mock
through authlib's own switch, and their client logging in with a fake token.
Vanilla's own code then checks the server hash, the RSA wrapping and the
AES/CFB8 cipher. No Mojang account needed, not a line of their code read.

## The padded cube

Meshing cost 4 ms per chunk, sixteen times a whole frame. The cause was the inner
loop: every block did 27 neighbour lookups, and every lookup worked out a section
with a division and a modulo, tested bounds, and at the edge stepped over into
another chunk object.

Per section, a cube of 18x18x18 is now filled once - the section plus a ring of
one block around it, because ambient occlusion reads diagonals. After that every
lookup in the inner loop is a flat array index, with no bounds test and without a
neighbouring chunk ever coming into view again. Alongside the block states the
"may cull" flag comes along too, so that one is no longer looked up per block
either.

5,832 reads to fill it, against 110,000 with divisions to do the same.

| | per chunk |
|---|---|
| separate lookups | 4.01 ms |
| through the padded cube | **1.35 ms** |

Three times faster, and the image is pixel-identical: the block test gives
exactly the same picture and `meshtest` still exactly 1536 remaining quads on a
solid section.

Horizon does the same with two rings instead of one, because they take the light
into account as well. As soon as we start reading block light that ring goes up
with it.
