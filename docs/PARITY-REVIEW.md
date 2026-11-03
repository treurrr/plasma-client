# Review of docs/PARITY.md

Reviewed against HEAD `2be9974` (3 November 2026), the protocol 774 reports
(`out774/reports/registries.json`, `packets.json`), the 1.21.11 server jar's
datapack (`data/minecraft/worldgen/biome`, `dimension_type`, `timeline`), the
1.21.11 client jar listing, a vanilla `options.txt`, and the recording
`corpus/spawn-200.plrc` (23 `registry_data` packets, 329 chunks, 52 entities).

The plan is good where it enumerates (packets, sprites, language prefixes all
check out against the jar and the reports). It is wrong in three ways: its
"Now" column is a day stale, it describes the pre-1.21.11 sky/fog/music model,
and a few counts and layouts are off. Section 5 gives the parallel split.

Line numbers below are at `2be9974`; the tree is being committed to several
times a day, so treat them as anchors, not addresses.

---

## 1. Errors in the plan

### 1.1 "Now" claims that are no longer true

| Item | Plan says | Code at `2be9974` |
|---|---|---|
| P0.1 dead `light_update` branch | broken | One branch, `session.cpp:906`, calls `on_light_update`, which `client_main.cpp:762` sets. Fixed. |
| P0.2 `forget_level_chunk` | none | `session.cpp:898`; the client drops the chunk and frees its GPU room (`test_span_allocator`). Done. |
| P0.3 free GPU room | none | Done with P0.2 (commit "Hand room on the gpu back when a chunk is let go"). |
| P0.4 chunk cache centre/radius | ignored | `session.cpp:885`, `on_chunk_area` drops chunks outside the area. Done. |
| P0.6 `respawn` | none | `session.cpp:662` reads spawn info, clears entities, fires `on_respawn`; `client_main.cpp:729` drops the world. Done. |
| P0.7 `add_entity` full read | partial | Read to the last byte (`session.cpp:798`). Done, and see 1.3 for the layout. |
| P0.8 / P0.9 `move_entity_pos_rot`, `move_entity_rot` | partial / none | One branch handles all three (`session.cpp:834`). Done. |
| P0.10 `bundle_delimiter` | ignored | `in_bundle_` toggled at `session.cpp:881`; the client applies a bundle as one. Done. |
| P0.13 nine-slice | wrong; docstring says data is not in the jar | `tools/bake_gui.py:120` reads `gui.scaling`, `UiRenderer::nine_slice` draws it, docstring corrected. Done. |
| P0.15 fixed chunk corpus | none | `plasma-client record`, `worldshot replay:<file>`, `corpus/spawn-200.plrc` and `glass.plrc`. Done. |
| P1.10 attributes, P1.12 effects, P1.13 tags, P1.15 difficulty | none / ignored | Commit `b0331b5` reads all four; `session.cpp:627` applies add / multiply-base / multiply-total; reach now comes from `block_interaction_range` (`client_main.cpp:1078`). Done, except that nothing but reach consumes them yet. |
| P1.11 `player_abilities` | none | `session.cpp:651`; flight is granted by the server, toggled by double-tap jump (`window.cpp:303`). Done. The `f` key no longer exists; the README still says it does. |
| P1.6 item names | partial, title-cased | `item.minecraft.<name>` looked up in the language file (commit `d3fa822`). Done. |
| P11.14 `disguised_chat` | none | `session.cpp:771`. Done. |
| P8.17 action bar | ignored | `system_chat`'s overlay flag is routed to `on_action_bar` (`session.cpp:783`). Protocol half done; drawing is not. |
| P2.15 status ping | "four packets Plasma has never spoken" | `src/tools/mc_ping.cpp:137-157` speaks all four (handshake next-state 1, `status_request`/`status_response`, `ping_request`/`pong_response`). Appendix A marks them *ignored*; they are done in a tool and need moving into the library. |
| P2.2 integer GUI scale, section 5 item 6 | "the HUD is drawn in a 1600x900 design space ... an 8-pixel hotbar at 1.37 px/px" | The vanilla HUD path already picks the integer scale (`menu.cpp:328-333`, largest `s` with `width/s >= 320`, `height/s >= 240`). Only the Plasma overlay and feature menu use `kDesignWidth` (`menu.cpp:17`). Two divergences remain: the scale is capped at 4 where vanilla's auto is unbounded, and the cap is not the `guiScale` option. P2.2 is 1 h, not 3 h, and P8.1 is mostly done. |
| Section 2 "`registry_data` parses one of the 95 registries" | | All 23 the server sends are kept generically (`registries_.set`, `session.cpp:498`). Done (P1.1). |
| Section 2 "client_main.cpp (2399 lines)" | | 2916 lines. Every `client_main.cpp:NNN` in section 4 has drifted by 12 to 200 lines: `kReach` is gone (reach is the attribute); flight constants `:836-857` are gone; bobbing `:892-904` is `:1085-1095`; sprint FOV `:914` is `:1107`; dig `:967` is `:1162`; face `:976` is `:1157/1171`; `gpu_ms` `:1069` is `:1270`. `session.cpp:604` (auto-respawn) is `:960`; `:278-286` (client_information) is `:393-403`; `:838` (unsigned chat) is `:1259`. `menu.cpp:571/578` are `:579/586`; `600-793` is `630-801`. |

Phase 0 is therefore finished except P0.12 (mipmap strategies, `bake_atlas.py` has none) and P0.14 (fluid corner interpolation, `mesher.cpp:289-319` still lowers only the top block).

### 1.2 Counts

| Where | Plan | Verified |
|---|---|---|
| P9.2, Appendix C `key.` | "59 gameplay and UI binds, 9 hotbar, 21 debug" | 59 is the **total**: 29 gameplay/UI + 9 hotbar + 21 debug. The `key.*` prefix is 164 keys = 29 + 9 + 21 + 8 `key.category.*` + 88 `key.keyboard.*`/`key.mouse.*` names + `key.mouse`. A vanilla `options.txt` has exactly 59 `key_` lines. |
| P1.3 `damage_type` | "40-odd" | 50 entries sent (`corpus/spawn-200.plrc`). |
| P3.72, P11.6 paintings | "102 variants" / "102 painting.minecraft.* keys" | 51 variants (registry sent); 102 keys = 51 titles + 51 authors. |
| P1.5 component list | 104 types, "list is exact" | The row names 93, two of them twice (`writable_book_content`, `break_sound`). `hide_tooltip` does not exist in 774 (replaced by `tooltip_display`). 25 variant components are misnamed: the registry spells them `cat/variant`, `wolf/collar`, `tropical_fish/pattern` etc. 15 real components are missing: `attack_range`, `container_loot`, `custom_data`, `damage_type`, `enchantable`, `kinetic_weapon`, `lodestone_tracker`, `map_color`, `map_post_processing`, `minimum_attack_charge`, `piercing_weapon`, `repairable`, `swing_animation`, `use_effects`, `zombie_nautilus/variant`. The advice to generate the table from `registries.json` is right; the hand list is not. |
| P5.16 "the 8 `instrument.*` note-block instruments" | | The `instrument` registry (8, sent) is goat horns. Note-block instruments are 16 values of the `instrument` block-state property, not a registry. |
| P3.53 variant registries | wolf, cat, frog, axolotl, pig, cow, chicken | Also `zombie_nautilus_variant` (2, sent) and `wolf_sound_variant` (7, sent; that one is sound, P5). |
| Section 1 "95 registries the server sends" | | `registries.json` lists 95 static registries; the server sends 23 dynamic ones (already known). |

### 1.3 Byte layouts

| Packet | Plan | Verified |
|---|---|---|
| `add_entity` (P0.7, section 2) | "pitch, yaw, head yaw, the data int and the three velocity shorts" | In 774 velocity comes **before** the angles and is a packed 48-bit vector plus an optional varint scale, not three shorts (`session.cpp:44-64`, `read_velocity`, measured against the server). The code is right; the plan would send the next agent to read it wrong. |
| `move_player_pos_rot` (Appendix A) | "on_ground hardcoded false" | `send_position` writes the real `on_ground` into the flags byte (`session.cpp:1209`). Horizontal-collision bit still never set. |
| `player_info_update` (P1.18) | "8 actions" | Correct; `session.cpp:1019` enumerates them, and the hat action is read. Still only latency is kept. |

### 1.4 Items marked as Plasma divergences that are not

- "`f` toggles creative flight locally" (section 4): gone. Flight is double-tap jump within `abilities().may_fly`, which is vanilla.
- "Flight physics are invented": gone; `physics.cpp` takes `flying_speed` from `player_abilities`.
- `Hitboxes` and `Fullbright` in `features.hpp`: the plan is right that these are F3+B and the Brightness slider; still in the registry.

---

## 2. Gaps: systems the plan does not cover

### 2.1 `environment_attribute` and `timeline` (the big one)

The plan never mentions either word. In 1.21.11 the sky, fog, clouds, sun, moon,
stars, sky light, music and ambient sound are **data**, and the server sends it:

- `minecraft:environment_attribute` is a 45-entry static registry: 21
  `visual/*` (`sky_color`, `fog_color`, `fog_start_distance`, `fog_end_distance`,
  `sky_fog_end_distance`, `cloud_fog_end_distance`, `water_fog_color`,
  `water_fog_start/end_distance`, `sunrise_sunset_color`, `cloud_color`,
  `cloud_height`, `sun_angle`, `moon_angle`, `star_angle`, `moon_phase`,
  `star_brightness`, `sky_light_color`, `sky_light_factor`,
  `default_dripstone_particle`, `ambient_particles`), 4 `audio/*`
  (`background_music`, `music_volume`, `ambient_sounds`, `firefly_bush_sounds`)
  and 20 `gameplay/*` (`bed_rule`, `respawn_anchor_works`, `sky_light_level`,
  `monsters_burn`, ...).
- Every biome carries an `attributes` block beside the old `effects` block.
  `plains.json`: `attributes: {visual/sky_color: "#78a7ff"}`; `swamp.json`
  adds `audio/background_music`, `visual/water_fog_color`,
  `visual/water_fog_end_distance: {argument: 0.85, modifier: multiply}`.
  The `effects` block still holds `grass_color`, `foliage_color`,
  `dry_foliage_color`, `grass_color_modifier`, `water_color` - so the biome
  parser at `session.cpp:454-493` is **right for tints and blind to everything
  else**. `sky_color`, `fog_color`, `water_fog_color` and music are never read.
- `dimension_type` carries its own `attributes` (overworld: `cloud_color`,
  `cloud_height: 192.33`, `fog_color: #c0d8ff`, `sky_color`,
  `audio/ambient_sounds` with the cave mood rule, `audio/background_music`
  with `creative` and `default`, `bed_rule`, ...), plus `timelines:
  "#minecraft:in_overworld"`, `skybox` (`none` in the nether),
  `cardinal_light` and `has_fixed_time`. The plan's P1.2 field list
  (`effects`, `bed_works`, `piglin_safe`, `cloud_height`) is the 1.21.4 shape.
- `minecraft:timeline` is a **dynamic registry the server sends** (4 entries:
  `day`, `moon`, `early_game`, `villager_schedule`) with a `timeline` tag
  registry (`in_overworld` = `universal` + `day` + `moon` + `early_game`). A
  timeline is `period_ticks` plus keyframe tracks per attribute, each with an
  `ease` (`constant`, `cubic_bezier [..]`, linear) and a `modifier` (`multiply`,
  `maximum`, `or`, replace). `day` (period 24000) drives `sun_angle` (0 to 360
  over the day with a bezier ease, 180 at tick 6000), `moon_angle`,
  `star_angle`, `star_brightness` (12 keyframes), `sunrise_sunset_color` (32
  keyframes), `sky_color` / `fog_color` / `cloud_color` as day/night
  multipliers (`#ffffff` by day, `#000000` / `#0f0f16` / `#191919` by night,
  switching between ticks 11867 and 13670), `sky_light_factor` (1.0 to 0.24),
  `sky_light_color` (`#ffffff` to `#7a7aff`), `firefly_bush_sounds`,
  `monsters_burn`. `moon` (period 192000) drives `moon_phase` and the slime
  spawn chance.

What Plasma does instead: `BiomeTable::sky()` (`biomes.cpp:75`) derives the
sky from temperature with an HSV formula, the fog is the constant `0xC0D8FF`
at `client_main.cpp:1251`, and `sky.cpp:21` hardcodes the cloud height. Those
numbers happen to equal the overworld attributes, which is why nothing looks
wrong today and why the nether and the end will.

Consequences for the plan:

- A new item is needed in Phase 1, call it **P1.30 environment attributes and
  timelines**: parse the biome and dimension `attributes` blocks, the
  `timeline` registry and its tags, resolve a dimension's active timelines,
  and evaluate every attribute at a tick (keyframe interpolation with the three
  ease kinds, then the modifier stack dimension -> timelines -> biome). About
  2 d. It is unit-testable from the JSON alone: `sun_angle` at tick 6000 is
  180, `sky_color` multiplier at 13670 is black, `star_brightness` at 627 is 0.
- P6.10 (day and night), P6.11 (weather sky), P6.12 (nether), P6.13 (end),
  P6.18 (cloud height), P6.19 (void fog), P5.12 (music), P5.13 (ambient
  sounds), P5.14 (weather sound) and P8.15's fog all depend on it. P6.10 as
  written ("from `set_time`: the sun and moon positions, the 8 moon phases,
  the sky colour gradient") would hand-code curves that the server has just
  sent as data. `set_time` (P1.17) only supplies the tick the timeline is
  evaluated at.
- P5.12's "each biome's own music" and P5.13's "random dark air block" rule are
  in the registry (`audio/background_music`, `audio/ambient_sounds.mood`
  with `block_search_extent`, `tick_delay`, `offset`), not in a hand table.
- Appendix D does not list this, correctly: it is all data.

### 2.2 Dimension parsing is thinner than P1.2 claims done

`read_spawn_info` (`session.cpp:533`) keeps `min_y`, `height`, `has_skylight`,
`has_ceiling`, `ambient_light`. Not read: `coordinate_scale`, `logical_height`,
`has_fixed_time`, `skybox`, `cardinal_light`, `timelines`, `attributes`,
`infiniburn`, `monster_spawn_*`. `Dimension` (`session.hpp:57`) has no room
for them. P1.2 should stay open for the 774 fields.

### 2.3 Keybinds

Against the 59 `key_` lines of a vanilla `options.txt`, `window.cpp:255-331`
hardcodes 15 and matches the defaults for all of them (W/A/S/D, space, left
shift, left control, Q, E, T, 1-9, left/right mouse, double-tap forward
sprint, double-tap jump fly). Missing: `playerlist` Tab, `pickItem` middle
mouse, `command` `/`, `socialInteractions` P, `toggleGui` F1, `screenshot` F2,
`debug.overlay`/`debug.modifier` F3 and the 19 chords behind it,
`toggleSpectatorShaderEffects` F4, `togglePerspective` F5, `fullscreen` F11,
`swapOffhand` F, `saveToolbarActivator` C, `loadToolbarActivator` X,
`advancements` L, `quickActions` G, `spectatorHotbar`. Non-vanilla: `M` opens
the feature menu (`window.cpp:329`) and Escape opens it instead of the pause
menu (`window.cpp:298`). The plan's P9.2 covers the registry; it should also
say that the vanilla *defaults* are the `options.txt` table above, so a
first-run Plasma matches a first-run vanilla.

### 2.4 Smaller gaps

| Gap | Where it belongs |
|---|---|
| `dry_foliage_color` fixed override in `effects` (swamp) is not read; only the colormap is used (`biomes.cpp:82`). | P1.2 / tints |
| `minecraft:dialog` is a dynamic registry the server sends (3 entries) and has a tag registry; P7.25 talks only about the packets. | P7.25 |
| `test_environment` and `test_instance` registries are sent (1 each); P11.20 should say they must parse, which P1.1 already guarantees. | P11.20 |
| The `horizontal collision` bit of the movement flags byte is never set (`session.cpp:1209`). | P4.27 |
| `move_player_pos_rot` is sent every tick even when nothing moved; vanilla sends `move_player_status_only` and re-sends position at most every 20 ticks when idle. P4.27 says "cheapest of four" but not the 20-tick rule. | P4.27 |
| `chunk_batch_received` reports a constant 32 chunks per tick (`kChunksPerTick`); vanilla measures the batch and reports a rate, which bounds how fast the server streams. | P0.3 / Phase 6 |
| `select_known_packs` answers zero (`session.cpp:433`), so the server sends every registry whole. Vanilla reports `minecraft:core` at the pack version and gets registries without entry bodies. Plan has it (P10.2) but it also doubles join time, which is a P12.7 number. | P10.2 |
| The README's "In the window" line and section 2's packet counts are stale. | docs |

---

## 3. Ordering

| Scheduled | Needed first | Fix |
|---|---|---|
| P6.10 day/night "needs P1.17" | P1.30 (timelines) and the biome/dimension `attributes` | Add P1.30 to Phase 1; make P6.10-P6.13, P6.18, P6.19, P5.12-P5.14 depend on it. |
| P2.21 death screen "needs P1.3" and P8.23 `player_combat_kill` "needs P2.21" | The death message is in `player_combat_kill`; `set_health` at zero only says you died. P8.23 is the input to P2.21, not its consumer. | Move P8.23 into Phase 2 before P2.21. |
| P10.9 styled text components, Phase 10 | P2.19 (disconnect reason), P2.21 (death message), P7.4 (tooltips), P8.8 (chat), P8.13 (scoreboard), P8.16 (titles) all render components with colour and style. Today everything goes through `json::strip_formatting`. | Move P10.9 into Phase 2 next to P2.3; it is the text half of the widget framework. |
| P5.1 "needs P2.1" (object store reader) | True, but P2.1 is 4 h and Phase 5 is otherwise independent. | Pull P2.1 into wave 0 (section 5) so Sound does not wait for Screens. |
| P1.18 `player_info_update` in Phase 1 (data), P3.21 skins in Phase 3 | Fine as a sequence; as parallel work P1.18/P1.19 belong with the entity block, because the skin property is only useful there and the list screen (P8.12) comes later. | Reassign in section 5. |
| P4.6 dig timer "needs P1.14, P1.10, P1.12" | Also needs P1.13 tags (`mineable/pickaxe` etc.) which it gets through P1.14. Now landed. | OK. |
| P1.21 tick clock, P3.5 interpolation | Fine. | |
| P0.12 mipmap strategies, P6.14 animated textures | Both change the atlas format and `bake_atlas.py`; do them in one block or they conflict. | Same owner (section 5, block B). |
| P4.1-P4.4 first-person hand "needs P3.17" | Correct, and it means Hands cannot be in the first wave. | Wave 2. |
| P7.7 inventory player model "needs P3.17" | Correct; Phase 7 is wave 2. | |

---

## 4. Verifying the plan's three open leads

- **(a) A 45-entry registry never mentioned**: `minecraft:environment_attribute`,
  45 entries in `registries.json`. Confirmed, see 2.1.
- **(b) Biome parser reads the old `effects` block**: half right. `effects`
  still exists in 1.21.11 and still carries the tint fields the parser reads;
  what moved is sky, fog, water fog, music and ambient sound, into
  `attributes` on biomes and dimension types, driven by `timeline`. Plasma
  invents sky from temperature and hardcodes fog. Confirmed, see 2.1.
- **(c) `client_main.cpp` line numbers drifted**: the file is 2916 lines; see
  1.1, last row.

---

## 5. Parallel blocks

Six blocks for six worktrees, after a short wave 0. The split is by file
ownership, not by phase; every block owns its own `src/session_<area>.cpp`
and `src/client/<area>.cpp` and touches the two shared files only at a
marked hook. Sizes are for one agent working alone.

### Wave 0, one agent, 1-2 days, before anyone branches

1. **Split `session.cpp`.** `handle_play` becomes a fixed chain of
   `if (handle_world(packet)) return true; if (handle_entities(packet)) ...`
   with one member function per area declared in `session.hpp` and defined in
   `src/session_world.cpp`, `src/session_entities.cpp`,
   `src/session_player.cpp`, `src/session_ui.cpp`, `src/session_sound.cpp`,
   `src/session_data.cpp`. `Events` becomes six nested structs
   (`events_.world.on_chunk`, `events_.entities.on_add`, ...). State that an
   area owns (`entities_`, `inventory_`, `vitals_`) moves into a per-area
   struct member. After this, a block adds packets without touching
   `session.cpp` at all.
2. **Split `client_main.cpp`.** `do_play` moves to `src/client/play.cpp` with
   a `Game` struct; each area gets `src/client/<area>.cpp` with
   `void <area>_bind(Game&, Events&)`, `void <area>_tick(Game&)`,
   `void <area>_draw(Game&)`, called from `play.cpp` in a fixed order. The
   subcommands stay in `client_main.cpp`, which then nobody edits.
3. **P2.1 object store reader** (`src/assets/object_store.cpp`): resolves
   `assets/indexes/<id>.json` to `objects/xx/<sha1>`. Screens and Sound both
   need it.
4. **Move the status ping** from `mc_ping.cpp` into `src/status.cpp` so
   Screens can call it.
5. **A conventions note** in `docs/` (section 6 below) and `corpus/README`:
   recordings are named `<area>-<what>.plrc`, one per block, so binary files
   never collide.

### Wave 1: six blocks with no dependencies on each other

| | Block | PARITY items | Owns | Shared files it touches | Depends on | Verified by | Size |
|---|---|---|---|---|---|---|---|
| **A** | **Data codecs** | P1.5 (generated from `registries.json`, see 1.2), P1.7, P1.8, P1.20 (the hand table only), P1.22, P1.23, P1.24, P1.25, P1.26, P1.27, P1.28, P1.29, P10.13, P10.14, P2.15's packet half | `src/proto/components*.{hpp,cpp}`, `src/proto/metadata*.{hpp,cpp}`, `src/proto/commands*.{hpp,cpp}`, `tools/gen_components.py`, `tools/gen_metadata.py`, `src/session_data.cpp`, `src/client/inspect.cpp` (the `inspect` subcommand, the P1 demonstrable) | `session.hpp`: fields of `ItemStack` and a `Metadata` bag on `Entity`. Announce the struct shape in the first commit and do not reshape it after. | wave 0 | Unit tests on bytes cut from `corpus/data-inventory.plrc` recorded with a full survival inventory (every component type the test server can produce via `/give`); the `inspect` printout against the vanilla client at the same spot; `part_read_` must report nothing. | 3 w |
| **B** | **World and sky** | P1.30 (new, section 2.1), P1.2 remainder, P1.17, P1.16, P0.12, P0.14, P6.10-P6.14, P6.18-P6.23, P6.1 decode, P1.29 (if A has not) | `src/world/environment.{hpp,cpp}` (attributes + timelines), `src/world/biomes.cpp`, `src/world/mesher.cpp`, `src/gpu/sky.cpp`, `src/gpu/world_renderer.cpp`, `shaders/world.*`, `shaders/sky.*`, `tools/bake_atlas.py`, `tools/bake_sky.py`, `src/session_world.cpp`, `src/client/world.cpp` | `session.hpp`: `Dimension` grows; `Biome` grows. `block_table.hpp` if the atlas gains an animation dimension. | wave 0 | Unit test evaluating timelines from the datapack JSON (section 2.1 gives three fixed points); `worldshot replay:corpus/spawn-200.plrc` at ticks 0, 6000, 13000, 18000 against vanilla screenshots from `tools/reference_views.py`; nether and end recordings (`corpus/world-nether.plrc`, `corpus/world-end.plrc`). | 3 w |
| **C** | **Screens** | P2.2 remainder, P2.3-P2.14, P2.16-P2.25, P10.9, P9.1, P9.2 (with the `options.txt` defaults, 2.3), P9.3, P9.4, P8.16, P8.17 draw, P8.18, P8.23 (move, see section 3), P2.21 | `src/ui/**` (new `src/ui/screen.{hpp,cpp}`, `src/ui/widgets/*`, `src/ui/screens/*`, `src/ui/text_component.cpp`), `src/gpu/ui.cpp`, `src/gpu/text.cpp`, `src/gpu/window.cpp` (key mapping only), `tools/bake_gui.py`, `src/options.{hpp,cpp}`, `src/keybinds.{hpp,cpp}`, `src/session_ui.cpp`, `src/client/screens.cpp` | `features.hpp`: gate `draw_menu` behind the profile. `play.cpp`: the input dispatch goes through the screen stack; this is the one place C and E both want, so C owns `play.cpp`'s input section and E calls into it. | wave 0 | `hudshot`/`menushot`-style offscreen renders of each screen at gui scale 2 against vanilla screenshots; unit tests for the text-component renderer (JSON and NBT forms, nested styles) and for `options.txt` round-trip; `servers.dat` read against the player's real file. | 4 w |
| **D** | **Entities** | P3.1-P3.9, P3.10-P3.16, P3.17-P3.27, P3.28-P3.39, P3.46, P1.18, P1.19, P1.20 dimensions (from the exporter), P1.21, P3.11 decision | `src/entity/**`, `src/gpu/entity_renderer.{hpp,cpp}`, `shaders/entity.*`, `tools/bake_entities.py`, `tools/export/` (the Java exporter, Route B), `src/skins.{hpp,cpp}`, `src/session_entities.cpp`, `src/client/entities.cpp`, `src/client/entityshot.cpp` | `session.hpp`: `Entity` grows (previous position, head yaw, pose). A's metadata bag lands on `Entity` too: A defines the bag type, D reads it; agree the type name on day one (`proto::Metadata`). | wave 0; P3.40-P3.45 (equipment, armour, held items, glint) wait for A | `entityshot <type>` front/side/top/angle against vanilla F5 screenshots; `corpus/entities-lobby.plrc` recorded on a busy server; P3.21 tested with a real skin on a real server, per the plan's warning. | 5 w |
| **E** | **Interaction and physics** | P1.9, P1.14, P4.5-P4.9, P4.14-P4.21, P4.22 decode, P4.25-P4.31, P4.10 once D exposes bounding boxes | `src/world/physics.{hpp,cpp}`, `src/world/shapes.{hpp,cpp}` and `tools/oracle/bake_shapes.py`, `data/hardness.txt`, `src/world/dig.{hpp,cpp}`, `src/particle/**`, `src/gpu/particle_renderer.cpp`, `shaders/particle.*`, `src/gpu/outline.cpp` (crack overlay), `src/session_player.cpp`, `src/client/player.cpp` | `play.cpp`'s tick order: physics tick, then dig, then send. Agree with C that input arrives as a `FrameInput` already filtered by the screen stack. | wave 0 | The server is the referee: `walktest`-style runs on the test server where a wrong face or cursor gets a `block_changed_ack` rollback and a wrong dig time gets no block drop; `test_physics_*` cases for each shape family (stand on a slab, walk into a fence, climb stairs) with positions compared to the vanilla client at the same coordinates; particle counts checked by eye in `worldshot`. | 4 w |
| **F** | **Sound** | P5.1-P5.18 except P5.14 (weather, wave 2) | `src/audio/**`, `third_party/miniaudio.h`, `third_party/stb_vorbis.c`, `data/block_sounds.txt`, `src/session_sound.cpp`, `src/client/sound.cpp` | `CMakeLists.txt` (one `add_subdirectory`); `play.cpp` one hook for listener position. | wave 0 (P2.1) | Unit tests: `sounds.json` parse, weighted pick by the packet's seed, attenuation curve, category multiply; `corpus/sound-anvil.plrc` replayed with the mixer running against the "no output" device and each voice's gain/pitch/position logged. Music and ambient rules land in wave 2 once B's attributes exist. | 2.5 w |

The dependency between A and D is the only cross-block one in the wave and it
is a type, not a feature: D can build everything through nametags and the
humanoid with no metadata, using the pose field only once A's bag exists.

### Wave 2, after A, C and D land

| Block | PARITY items | Needs |
|---|---|---|
| Hands and items | P4.1-P4.4, P4.11-P4.13, P3.40-P3.45, P7.3, P8.21 | D (humanoid), A (components) |
| Containers | P7.1-P7.25 | C (screens), A (components), P4.12 |
| HUD and chat | P8.2-P8.15, P8.19-P8.27, P8.8, P8.9 chat signing, P8.10-P8.12 | C, A (commands tree), D (heads in the tab list) |
| Block entities | P6.2-P6.9, P7.20 | D (model pipeline), A (NBT bodies) |
| Options, F3, perspectives | P9.5-P9.20 | C, D (third person) |
| Packs, fonts, languages | P10.1-P10.8, P10.10-P10.12 | C |
| Long tail | Phase 11 | the above |

Chat signing (P8.9) is independent of everything but `auth.cpp` and could be a
seventh wave-1 block if an agent is free; it is a week and it is what keeps
the client off most public servers.

---

## 6. Conventions that keep the worktrees mergeable

- **Nobody edits `session.cpp`, `client_main.cpp` or `play.cpp` outside their
  marked hook** after wave 0. New packets go in `session_<area>.cpp`; new
  per-frame work goes in `client/<area>.cpp`.
- **`session.hpp` and shared structs**: a block that needs a new field on
  `Entity`, `ItemStack`, `Dimension` or `Biome` adds it at the **end** of the
  struct, in one commit, with the block letter in the commit subject, and
  rebases onto main the same day. Two blocks appending to the same struct at
  the end merge cleanly; two blocks editing the middle do not.
- **Packet ids**: nobody regenerates `packet_ids_gen.cpp`; it is complete for
  774 and 776.
- **Generated tables** (`components_gen.cpp`, `metadata_gen.cpp`) are
  generated by a script in `tools/` and checked in, like `packet_ids_gen.cpp`.
- **Recordings** are append-only in `corpus/`, one per block, named by area.
- **Tests**: each block adds `tests/test_<area>.cpp` and one line in
  `CMakeLists.txt`; `test_protocol.cpp` is not extended further.
- **Hand tables** (`data/hardness.txt`,
  `data/block_sounds.txt`, `data/entity_dimensions.txt`) are text, one row per
  block or entity, sorted by registry name, so two people editing different
  rows merge.
- **Line references in PARITY.md** should be replaced by function names or
  removed; they were wrong within a day.
