# Audit: hand-written code next to the Java

The client was written before the port ledger existed, so a part of it mirrors
a vanilla method without ever having been translated from one. This document
is the list of those places, ordered by what a player notices, and what
happened to each. It is kept next to `PORT-LEDGER.md`: the ledger says where a
port is, this says which of our code should be a port and is not.

## How the list was made

`python3 tools/port_ledger.py build` on `ab1f678`: 8910 classes (5301 in
scope), 2529 of our functions, 897 citations, 50 map rows. Every function
without a citation was read and put in one of two groups.

**Ours by right, cites nothing** (`METHOD.md`: how things reach the GPU, and
the plumbing around it). Not in the work list:

- `src/gpu/*`, `shaders/*` - Vulkan devices, buffers, pipelines, batching,
  offscreen targets, the span allocator, the blur.
- `src/tcp_socket.cpp`, `src/crypto.cpp`, `src/auth.cpp`, `src/skins.cpp`,
  `src/assets/*`, `src/json.cpp`, `src/image_png*.cpp` - sockets, encryption,
  the session server, the asset store, JSON, PNG.
- `src/proto/*_gen.cpp`, `src/proto/packet_ids*` - generated tables.
- `src/world/chunk.cpp`, `src/world/atlas.cpp`, `src/world/block_table.cpp`,
  `src/world/render_table.cpp`, `src/world/shapes.cpp` (the readers) - our own
  storage for what the bake tools wrote.
- `src/client/*shot.cpp`, `*test.cpp`, `worldbench.cpp`, `inspect.cpp`,
  `src/tools/*`, `src/audio/soundlog_main.cpp`, `tools/oracle/*_probe.cpp` -
  test harnesses and subcommands.
- `src/audio/mixer.cpp`, `decoder.cpp`, `pcm.cpp`, `ring.hpp`,
  `miniaudio/stb_vorbis` glue - how sound reaches the device.
- `src/options.cpp`, `src/keybinds.cpp`, `src/features.cpp`, `src/nbt.cpp`,
  `src/registries.cpp`, `src/translations.*` - file formats and registries we
  read our own way.

**Mirrors a vanilla method, written by hand.** That is the work list below.

## The work list

Ordered by what a player feels first. `fixed` entries are this pass;
`open` entries are what is left, in the same order.

### 1. One tick clock, not three - `fixed`

| | |
|---|---|
| ours | `src/client/play.cpp` frame loop (`tick_accumulator`), `mcpp::entity::TickClock::advance`, `client::player_move`'s own `partial_tick` |
| vanilla | `DeltaTracker.Timer.advanceGameTime`, `Minecraft.runTick` |
| wrong | three clocks over the same `dt`, and the one in `play.cpp` threw its remainder away (`if (tick_due) tick_accumulator = 0`). At 143 fps that is 17.9 ticks a second instead of 20 for everything it drives (`screens_tick`, `hud_tick`, `sound_tick`), and the entity clock's partial differed from the camera's, so other players were drawn at a different point between two ticks than the camera was placed at. Vanilla has one timer and one partial tick. |

### 2. `Player.onClimbable` while flying - `fixed`

| | |
|---|---|
| ours | `Mover::on_climbable`, `src/world/physics.cpp` |
| vanilla | `Player.onClimbable` over `LivingEntity.onClimbable` |
| wrong | the override was missing: vanilla returns false while `abilities.flying`, ours returned true. Flying through a ladder or a vine clamped the fall speed to 0.15 and pinned vertical speed to 0.2 against a wall, so creative flight stuck on ladders and vines. |

### 3. `Player.getBlockSpeedFactor` while flying - `fixed`

| | |
|---|---|
| ours | `Mover::block_speed_factor`, `src/world/physics.cpp` |
| vanilla | `Player.getBlockSpeedFactor` over `LivingEntity.getBlockSpeedFactor` |
| wrong | the override was missing: vanilla returns 1.0 while flying or fall-flying, ours kept slowing. Flying over soul sand or honey slowed the camera down. |

### 4. `AttributeInstance.calculateValue`'s last step - `fixed`

| | |
|---|---|
| ours | `client::value_without` (`src/client/player_rules.cpp`), `Attribute::value` (`include/mcpp/session.hpp`) |
| vanilla | `AttributeInstance.calculateValue`, `RangedAttribute.sanitizeValue` |
| wrong | both stopped before `sanitizeValue`, so nothing clamped the result to the attribute's own range and NaN came through. `movement_efficiency`, `sneaking_speed` and `water_movement_efficiency` are `[0,1]`, `gravity` is `[-1,1]`, `step_height` is `[0,10]`; a server that stacks modifiers past those got a speed, a step or a gravity the server itself does not use, which is a correction a tick later. |

### 5. `LivingEntity.handleDamageEvent` - `fixed` in part

| | |
|---|---|
| ours | `Living::damaged`, `src/entity/motion.cpp` |
| vanilla | `LivingEntity.handleDamageEvent` |
| wrong | ported the walk kick and the hurt time; left out `hurtDuration` (vanilla sets it beside `hurtTime`; `GameRenderer.bobHurt` divides by it) and `invulnerableTime = 20`. Fixed. The hurt sound is still missing: it needs `getHurtSound` per entity type, which no table of ours holds - see *open 5* below. |

### 6. `Utf8String.read` - `fixed`

| | |
|---|---|
| ours | `ByteBuffer::read_string`, `src/byte_buffer.cpp` |
| vanilla | `Utf8String.read` |
| wrong | uncited, and it stopped after the byte-length check. Vanilla also refuses a string whose decoded length in UTF-16 units is over the maximum, which is the check that keeps a 32767-character limit a character limit and not a 131068-byte one. |

### 7. The block outline and the translucent pass - `open`

| | |
|---|---|
| ours | `WorldRenderer::record` / `draw_geometry`, `src/gpu/world_renderer.cpp` |
| vanilla | `LevelRenderer.renderBlockOutline`, `LevelRenderer.extractBlockOutline` |
| wrong | confirmed against the Java. Vanilla draws the outline twice over, once before the translucent group and once after, and `renderBlockOutline` keeps only the pass whose `isTranslucent` matches the aimed block's chunk render type (`ItemBlockRenderTypes.getChunkRenderType(state).sortOnUpload()`). Ours always draws after everything, so the lines show through water, ice and stained glass standing in front of the aimed block. The render table already carries the layer per state, so the fix is to record the outline inside `draw_geometry` before the translucent draw when that layer is not translucent. Wave 2 agent C's lead, confirmed. |

### 8. The light engine - `open`

| | |
|---|---|
| ours | `LightEngine::opacity`, `LightEngine::relight`, `src/world/light.cpp` |
| vanilla | `BlockBehaviour.BlockStateBase.getLightBlock`, `LevelLightEngine.checkBlock`, `BlockLightEngine`, `SkyLightEngine` |
| wrong | the whole file is ours by hand. Two things are provably off. `opacity` decides a block's light block from its registry name (`_leaves`, `cobweb`) and from whether it occludes, while vanilla reads a per-state value - and `RenderInfo::light_block` already holds that baked value, so the hand rule is both wrong and unnecessary (ice and the half-transparent blocks come out 0 instead of 1, so sky light does not fade under them). `relight` never seeds the placed block's own `getLightEmission`: it zeroes the cell and spreads from the neighbours, so a torch placed in a dark room lights nothing but its own cell until the server's `light_update` arrives. |

### 9. Biome blend - `open`

| | |
|---|---|
| ours | `src/world/biomes.cpp`, `src/world/mesher.cpp` tint lookup |
| vanilla | `BiomeColors.getAverageColor`, `BlockTintCache`, `Biome.getGrassColor`/`getFoliageColor`/`getWaterColor` |
| wrong | not done: we tint from the biome of the block itself, so a biome border is one hard line. Vanilla averages the colour over a square of radius `options.biomeBlendRadius` (default 2, so 5x5 = 25 samples) around the block, summing the three channels as ints and dividing by the count, and caches it per chunk in `BlockTintCache`. Wave 2 agent E's lead. |

### 10. `src/session_*.cpp` packet reads - `open`

| | |
|---|---|
| ours | 113 `packet.id ==` arms over seven files, 8 citations between them |
| vanilla | one `ClientboundXPacket` `StreamCodec` each, and the `ClientPacketListener.handleX` beside it |
| wrong | not wrong as far as it was checked, uncited as a body. `Session::handle_player`'s `send_movement` was read against `LocalPlayer.sendPosition` and `sendIsSprintingIfNeeded` in full and matches, field for field and branch for branch, including the 20-tick position reminder and the status-only arm; `player_position`, `set_entity_motion`, `player_abilities`, `set_health`, `set_experience` and `block_destruction` match their codecs. The rest (`session_world.cpp` 22 arms, `session_ui.cpp` 22, `session_data.cpp` 16, `session.cpp` 19) are unread and uncited. The half-read guard (`part_read_`) is what has been standing in for the audit; it catches a field too few, not a field in the wrong order of the same width. |

### 11. `src/world/environment.cpp` - `open`

| | |
|---|---|
| ours | 33 of 54 functions uncited |
| vanilla | `Level.getTimeOfDay`, `DimensionType`, `Level.getSkyDarken`, the `world/timeline` package |
| wrong | the cited half is ported, the rest is a mix of plumbing and arithmetic that follows a vanilla curve without saying which. Needs reading function by function; nothing seen so far is wrong. |

### 12. Local-player elytra flight - `open` (absent, not wrong)

| | |
|---|---|
| ours | `mcpp::world::Body` has no fall-flying state at all |
| vanilla | `Player.isFallFlying`, `LivingEntity.travelFallFlying`, `FireworkRocketItem` boosting |
| wrong | not ported. It is a gap, not a bad port: noted here because *3* above names fall-flying in the condition it fixes, and that half of the condition can never be true until this exists. Belongs in `tools/port_map` as `absent`. |

## What is proved, and how

- `tests/test_player.cpp` - `block_speed_factor` and `on_climbable` while
  flying, the attribute clamp, and the tick clock's residual, all with values
  derived from the Java by hand, not from our code.
- `tests/test_world.cpp` - `read_string`'s two limits.
- `ctest` over the whole tree at every commit.
- The outline order is a pixel question and is listed open with the render it
  needs (`~/plasma-work/audit/`).
