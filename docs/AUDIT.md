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

Ordered by what a player feels first. `fixed` entries are this pass; `open`
entries are what is left, in the same order. What is still open, shortest
first: biome blend (9), the session's packet reads
(10), `environment.cpp` (11), the shape of the light engine (8), elytra (12).

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

### 5. `LivingEntity.handleDamageEvent` - `fixed`, and far smaller than it looked

| | |
|---|---|
| ours | `Living::damaged`, `src/entity/motion.cpp` |
| vanilla | `LivingEntity.handleDamageEvent` |
| wrong | read against the Java: the walk kick and `hurtTime` are both there and right. The two fields it leaves out, `hurtDuration` and `invulnerableTime`, have no reader on our side - vanilla's only assignment to `hurtDuration` is the same 10 the hand's `bob_hurt` already uses, and `invulnerableTime` is only read where damage is applied, which a client does not do. Adding them would be dead state, so they stay out and this says why. **The hurt sound this row asked for is almost all the server's**, which is not what it said: vanilla's `handleDamageEvent` does play `getHurtSound(source)` at `getSoundVolume()` with pitch `(random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F`, but through `Entity.playSound`, which hands `null` to `ClientLevel.playSeededSound`, and that plays nothing unless the entity it is given is the player at the keyboard. For every mob and every other player the server has already played the same sound and broadcast it, so it arrives as a `sound` packet and we have been playing it all along. Measured on the test server: a cow hurt twice and killed sends `entity.cow.hurt` twice and `entity.cow.death`, each at volume 0.4, and a zombie the same at 1.0. So no per entity type table is needed, and building one would have been dead code. `Player.playSound` is the one override that hands itself over instead, and the server's broadcast leaves that player out, so **the player's own hurt and death sound exist nowhere but in the client** - and those really were silent. They are now played from `sound_bind`, picked by the damage type that `damage_event` carries, which was also being read one id short of its own registry. Rows in `tools/port_map/voices.tsv`. |

### 6. `Utf8String.read` - `fixed`

| | |
|---|---|
| ours | `ByteBuffer::read_string`, `src/byte_buffer.cpp` |
| vanilla | `Utf8String.read` |
| wrong | uncited, and it stopped after the byte-length check. Vanilla also refuses a string whose decoded length in UTF-16 units is over the maximum, which is the check that keeps a 32767-character limit a character limit and not a 131068-byte one. |

### 7. The block outline and the translucent pass - `fixed`

| | |
|---|---|
| ours | `WorldRenderer::record` / `draw_geometry` / `draw_outline`, `src/gpu/world_renderer.cpp` |
| vanilla | `LevelRenderer.renderBlockOutline`, `LevelRenderer.extractBlockOutline`, `ChunkSectionLayer.sortOnUpload` |
| wrong | vanilla offers the outline twice, once before the translucent group and once after it, and `renderBlockOutline` keeps the pass whose `isTranslucent` matches the aimed block's chunk render type. Ours always drew it last, after the translucent pass had written its depth, so an ordinary block seen through glass, ice or water had **no outline at all** - not lines drawn over the glass, as the lead read it, but lines thrown away by the depth test. Fixed by recording it inside `draw_geometry` before the translucent draw for the layers that do not sort on upload. Wave 2 agent C's lead, confirmed with the direction corrected. |

### 8. The light engine - `fixed` in its two values, the engine itself still ours

| | |
|---|---|
| ours | `LightEngine::opacity`, `LightEngine::relight`, `src/world/light.cpp` |
| vanilla | `BlockBehaviour.BlockStateBase.getLightBlock`, `LightEngine.getOpacity`, `BlockLightEngine.checkNode`, `SkyLightEngine`, `ChunkSkyLightSources` |
| wrong | two things were provably off and are fixed. `opacity` decided a block's light block from its registry name (`_leaves`, `cobweb`) and from whether it occludes, while vanilla reads a per-state value that `RenderInfo::light_block` already holds: ice came out 0 where the game says 1, so sky light did not fade under ice or the half-transparent blocks. And `relight` never put the block's own `getLightEmission` back after taking the old light away, so a torch placed in a dark room lit nothing but its own cell until the server's `light_update` arrived. What is left open is the shape of the engine: two flood fills of ours against vanilla's `DynamicGraphMinFixedPoint` queue and its per-column `ChunkSkyLightSources`. With the two values right they agree on everything tried so far, and the "sky light at 15 falls straight down while opacity is 0" rule is `ChunkSkyLightSources`' rule in another form, but it has not been proved equal in general. |

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
| wrong | not ported. It is a gap, not a bad port: noted here because *3* above names fall-flying in the condition it fixes, and that half of the condition can never be true until this exists. Rows in `tools/port_map/audit.tsv`. |

## What is proved, and how

Every test below was run once against the broken code first, to see it fail,
and then against the fix.

| what | proof |
|---|---|
| `Player.onClimbable` while flying | `test_player.cpp` `test_flying_climbable`: flown through a line of vines, the body keeps flying speed and its height. `handleOnClimbable` cuts horizontal speed to 0.15 a tick, flight settles far above that. Fails without the fix. |
| `Player.getBlockSpeedFactor` while flying | `test_player.cpp` `test_flying_speed_factor`: the same flight over soul sand and over stone covers the same ground to 1e-12, while on foot soul sand is still the slower of the two. Fails without the fix. |
| `RangedAttribute.sanitizeValue` | `test_entities.cpp` `test_attribute_range`: the registered ranges of seven attributes, NaN, an unknown attribute, and the three operations before the range. |
| `DeltaTracker.Timer.advanceGameTime` | `test_entities.cpp` `test_clock`: a thousand 7 ms frames are exactly 140 ticks (a clock that drops its remainder reaches 125), a 260 ms frame is five ticks and a fifth, a two-second stall is ten. |
| `Utf8String.read` | `test_data.cpp` `test_utf8_limits`: a field of three characters takes three two-byte letters or one emoji and one letter, but not two emoji. |
| `getLightBlock`, `BlockLightEngine.checkNode` | `test_world.cpp` `test_relight`: a torch lights its own cell to 14 and fades a level a block, ice takes one level out of the sky light falling down a shaft. All six checks fail with the old rule. |
| the outline's pass | pixels. `plasma-client outlineshot <dir> --through=minecraft:blue_stained_glass minecraft:oak_slab minecraft:blue_stained_glass`, rendered once with the old order and once with the new. Aiming at the glass itself: byte-identical, it still draws after the translucent pass. Aiming at the slab behind the glass: 2376 pixels changed, each one a line pixel that the glass's depth used to throw away and that now shows through the glass. `~/plasma-work/audit/outline-old`, `outline-fixed`, `outline-plain`. |

`ctest` (14 suites) passes at every commit, and
`port_ledger.py stale` is clean.

The server is no referee for any of these: creative flight is not simulated
server side, so it cannot tell the two movement fixes apart from what we did
before, and light and the outline never leave the client.
