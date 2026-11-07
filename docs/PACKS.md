# Server resource packs

CubeCraft and most public networks push their own resource pack. Until now
Plasma could not load one at all: everything it draws comes from tables baked
ahead of time out of the player's own jar (`tools/bake_*.py` →
`~/.local/share/plasma-client/data`), and there was no way for a stranger's zip
to land on top of those while the game is running. This is that way.

The code is in `include/mcpp/packs/` and `src/packs/`:

| file | what it is |
| --- | --- |
| `pack_protocol.*` | the packets, the answers, the prompt setting, the state machine (`ServerPackManager`) |
| `zip.*` | a zip read from memory, with caps, nothing unpacked to disk |
| `pack_cache.*` | the download, the sha1, the cache keyed by hash (`DownloadQueue`, `HttpUtil.downloadFile`) |
| `resource_pack.*` | `pack.mcmeta`, overlays, and the stack several packs make |
| `mipmap.*` | the game's own mip chain for one sprite, in C++ |
| `pack_apply.*` | the pack on top of the baked tables, and off again |
| `pack_system.*` | all of it behind the four calls a client makes |

## The protocol half

`ClientboundResourcePackPushPacket` carries a uuid, a url, a sha1 (forty hex
digits or nothing), whether declining disconnects the player, and a line the
server wants shown. `ClientboundResourcePackPopPacket` carries one uuid or
none, which means all of them. Both arrive in the configuration phase and in
play, and `src/session_data.cpp` handles them in the one place the two phases
share. Without a handler every pack is declined, which is what the client did
before.

The player's answers are `ServerboundResourcePackPacket.Action`, eight of them,
and the two that are not terminal are progress rather than an answer:

```
accepted -> downloaded -> successfully_loaded
accepted -> failed_download
accepted -> downloaded -> failed_reload
declined / invalid_url / discarded
```

`ServerPackManager` is translated method for method, including the parts that
are easy to get wrong:

- One failure in a download batch discards the whole batch (`onDownload`): the
  packs that did arrive are answered `discarded`, not applied. A server that
  stacks two packs gets the look it asked for or none of it.
- A pack is forgotten only once it is off screen **and** the server has been
  told why (`cleanupRemovedPacks`). `server_removed` and `server_replaced` are
  the two reasons that need no answer, because the server already knows.
- The same uuid pushed again marks the old one `server_replaced`.
- Several packs stack in the order the server pushed them: the first is at the
  bottom, so the last one pushed wins where they overlap. That is what
  `PackStack::read` does, reading from the top down.
- `serverResourcePacks` is prompt / always / never
  (`ServerData.ServerPackStatus`). On never, a pack is declined the moment it
  arrives — unless it is required, which prompts anyway. Declining a required
  pack disconnects the player
  (`multiplayer.requiredTexturePrompt.disconnect`).
- A url whose scheme is not http or https is answered `invalid_url` and never
  fetched (`parseResourcePackUrl`).

## Fetching

`PackCache` is `DownloadQueue` plus `HttpUtil.downloadFile`. A pack lives under
its own sha1 in `~/.local/share/plasma-client/server-resource-packs/`, so the
same pack from a hundred servers is one download; a cache hit touches the
file's modification time and the twenty newest survive a vacuum
(`DownloadCacheCleaner.vacuumCacheDir`, `MAX_KEPT_PACKS`). The download is
capped at vanilla's 262144000 bytes, both through curl's own
`CURLOPT_MAXFILESIZE_LARGE` and by counting bytes as they arrive, and the hash
is checked before the file is moved into place. A mismatch is a failure, not a
pack. The five `X-Minecraft-*` headers and the `Minecraft Java/1.21.11`
user agent go out as vanilla sends them, because some servers key their pack on
them.

Vanilla leaves the socket timeout to the JDK, which on a stalled host is no
timeout at all. That is not behaviour worth copying, so there is a two minute
ceiling, a fifteen second connect timeout and a low-speed cutoff.

The zip is read in memory, never unpacked. Everything the file claims about
itself is checked against a cap before a byte is allocated: the archive size,
one entry's inflated size, the whole archive's inflated size, the entry count.
Each entry's crc32 is verified. A name that is absolute or climbs out of the
pack fails the whole archive — nothing is written to disk, so it could not
matter, but a name like that means the file is not what it says it is. Zip64 is
read, stored and deflated entries both.

## Applying it, at run time

This is the part that decides whether any of it is usable, and the baked
tables' shape is what makes it cheap.

**Block textures.** `atlas-774.plat` is not a grid atlas. It is a texture array
with **one layer per texture**, every layer the same 32-pixel tile, six mip
levels, and the layer index *is* the texture id the block table hands the
mesher. So a pack that replaces one texture replaces one layer's mip chain and
nothing else:

- no atlas is re-stitched and nothing is re-packed,
- no uv moves, so **every mesh already on the gpu stays exactly as valid as it
  was** — no re-mesh, no re-bake, no stall,
- the upload is one `vkCmdCopyBufferToImage` per level per layer, all of them in
  one staging buffer and one submit (`WorldRenderer::patch_atlas`).

Vanilla cannot do this. Its atlas is stitched from sprites of whatever size the
pack ships, so a pack changes the sheet's layout, every sprite's uv, and
therefore every baked model: `Minecraft.reloadResourcePacks` tears down and
rebuilds the model manager and re-meshes the world. We do not have to.

The mip chain is not a quick box filter. `src/packs/mipmap.cpp` is
`MipmapGenerator.generateMipLevels` and its helpers in C++, the same port
`tools/bake_atlas.py` already made in Python: `ARGB.meanLinear` averaging
through the two 1024-entry sRGB rounding tables, `TextureUtil.solidify`
breadth-first in the game's column-first order, `fillEmptyAreasWithDarkColor`,
`darkenedAlphaBlend`, and `alphaTestCoverage` / `scaleAlphaToCoverage` with
their five bisection steps. A pack's leaves thin out at distance exactly as much
as the jar's do, and a replaced texture's `.mcmeta` `mipmap_strategy` and
`alpha_cutoff_bias` are honoured.

**Item icons.** `items-774.plit` holds *rendered* icons: the baker resolves each
item to its model and renders it once at bake time. A rendered picture cannot be
composed again from different textures. But for the items whose model is layers
of flat sprite — `item/generated` and its children, 716 of the 1505 items — the
whole render is one nearest resample and one tint per layer, so the recipe is
enough. `tools/bake_item_layers.py` writes that recipe down
(`item_layers-774.txt`, 728 layers over 716 items) and `PackApplier` composes
the icon again from the pack's own pictures, exactly the way
`bake_items.py flat_icon` composed it from the jar's. One array layer per icon,
so the upload is again one copy.

**Interface sprites.** `gui-774.plgu` is one 2048x2048 sheet of named
rectangles. A replaced sprite is written back into the rectangle it was packed
into, so nothing is re-packed and no uv moves. A pack sprite of a different size
is resampled to that rectangle. The mapping from a pack's path to the name the
baker packed it under (`gui/sprites/...`, `gui/container/...`,
`mob_effect/...`, the sheets at the top of `gui/`, `misc/unknown_server`) is
reproduced in `PackApplier::gui_sprite_name_of`.

**Taking it off.** The baked atlas is mmapped, so the original mip chain of any
layer is already in memory: reverting is the same patch call with the baked
pixels. The baked `.plit` and `.plgu` pixel blocks are read lazily — twenty-odd
megabytes nothing else needs — and only the icons and rectangles that were
written over are written back. `PackApplier` remembers exactly what it touched,
so a pop costs what the apply cost and nothing more, and no restart is needed.

**Where a pack lands in the pipeline.** `GpuPackTarget` sits in front of
`WorldRenderer` and `UiRenderer`; `PackTarget` is the interface, so the unit
test runs the whole apply path with no gpu at all.

## What is not done

Said plainly, because the brief asked for it:

- **Models.** A pack that ships its own `models/block/*.json` or
  `blockstates/*.json` is read but not applied. Block geometry is baked into
  `blocks-774.plbk` by `tools/bake_block_table.py`, which resolves models,
  bakes quads and assigns texture ids in Python; the client cannot run that
  mid-session. Doing it would mean porting the model resolver and the quad
  baker to C++ and re-meshing every loaded section. A pack that only retextures
  — which is what most server packs are — is unaffected.
- **Fonts.** `gui-774.plgu` carries the default font's glyphs as sprites with
  their advances read out of the sheet's own ink. A pack's `font/default.json`
  providers, its glyph sheets and its space advances are not read, so a pack
  that ships a font still draws in the baked one. The custom negative-space
  fonts servers use for custom HUDs therefore do not show.
- **Sounds.** `sounds.json` and a pack's own `.ogg` files are not applied. The
  audio path reads `block_sounds-774.txt` and the game's own sound events.
- **Overlays** are selected correctly (`min_format`/`max_format`, `formats`,
  the directory-name check) and resolved correctly for every resource the
  applier does handle. The `pack_format` validation vanilla does — the exact
  errors for a missing `supported_formats`, a mismatched range — is collapsed
  into "the range is empty, so the pack is not compatible". A pack that is not
  compatible is still applied, the way a server pack is.
- **Animated textures.** A pack may replace an animated texture, and its frames
  land in the extra layers the baked atlas already allocated for that texture.
  A pack with *fewer* frames than the baked one repeats its last frame; a pack
  with *more* loses the rest, because there are no layers to put them in, and a
  pack that animates a texture the jar does not animate gets only its first
  frame. The `.mcmeta` `animation` section's own frame order and frametimes are
  not re-read.
- **A pack texture larger than the atlas tile** (32 pixels here, which is what
  the jar's largest block texture forces) is taken from the level of its own
  chain that matches 32 — the texels the game would show if its atlas were this
  size — rather than growing the atlas. Growing it would mean rebuilding all
  1886 layers at the new tile size. That is a pure re-upload in this layout
  (still no re-mesh), so it is the natural next step, on a worker thread; it is
  not done here.
- **An item icon with more than one flat layer** is only recomposed when the
  pack replaces *all* of its layers, because the layer it leaves alone lives in
  the jar and the client does not read the jar. Twelve of the 716 flat items
  have more than one layer.
- **Rendered item icons** — a block item, a shield, a trident — are not
  recomposed at all, for the same reason models are not: the render is a bake.
- Packs are applied on the calling thread. `PackSystem::tick` downloads and
  applies where it is called, so a client that does not want to block a frame
  calls it off the render thread. Vanilla does the download on an io pool and
  the reload on the main thread.

## Proof

```
~/plasma-work/cap ctest --test-dir build-r6 -R packs
plasma-client packshot ~/plasma-work/packs
plasma-client packserve 127.0.0.1:25601 PlasmaR <x,y,z> ~/plasma-work/packs
```

`tests/test_packs.cpp` builds its own zip in memory and runs the lot: the
action numbers and their order, the prompt settings, the url and hash checks,
the state machine's four endings, two packs stacking, the zip caps and the
unsafe-name refusal, `pack.mcmeta` with and without overlays, the cache keyed
by hash with a hit and a mismatch, the mip chain's sizes and the identity level,
and the whole apply-and-pop path against the real baked tables with a target
that records what it was given.

`packshot` writes the same scene before, after and after the pop, counts the
pixels that changed, and fails if a scene looks the same with the pack on or
does not go back to the baked look. `packserve` joins a server, lets it push
the pack over http, applies it mid-session, and renders the world and the
interface with it and without.
