# Parity: from here to vanilla

**What this document is.** The complete list of everything vanilla Minecraft: Java
Edition 1.21.11 does that Plasma does not, in the order it should be built, at a
granularity where no item is bigger than about a day. It is not a summary and it
is not a wishlist. It is the work queue.

**Why it exists.** Work on this client has been happening piecemeal, and things
keep getting missed. A gap list that is merely complete does not fix that - you
still have to decide what comes next every morning, and that decision is where
things fall through. So the ordering is the deliverable. Every item's
dependencies are already behind it. You can start at the top and work down
without choosing.

**How to read it.**

- Phases run in order. Items within a phase run in the order given unless the
  row says `‖` (parallel-safe: nothing later in the phase depends on it, reorder
  freely).
- `!` in the flag column means **small but blocking**: a short item that a lot of
  later work is waiting on. Do not defer these.
- **Now** is what Plasma has today, verified against the source on 3 November
  2026, not against the README. Where the README and the code disagree, the code
  wins and the disagreement is noted.
- **Size** is one person's working time with no interruptions. `h` = hours,
  `d` = days of about six productive hours, `w` = five such days. Where I am
  guessing, the size is followed by `?`.
- **Who** is which of the two target audiences the item serves: `pvp` for the
  Lunar/Badlion market, `fast` for the "just a faster vanilla" profile, `both`.
  Almost everything in a parity plan is `both`, because the thing both audiences
  are buying first is a client that *works*.

**Scope.** Phases 1 to 11 are vanilla parity and nothing else. Nothing
Plasma-specific appears in them. The Plasma layer - the styled menu, the FPS and
CPS readouts, the feature registry, the two profiles - is Phase 12, on top of a
finished vanilla client, and section 4 lists everything currently in the tree
that has to move behind the profile switch or come out before Phase 1 can claim
to be vanilla. Singleplayer, world generation, world saving, Realms and the
server-management RPC API are out of scope and section 3 says why.

---

## Contents

1. [Where the facts come from](#1-where-the-facts-come-from)
2. [What Plasma actually has today](#2-what-plasma-actually-has-today)
3. [Deliberate exclusions](#3-deliberate-exclusions)
4. [Everything Plasma has that vanilla does not](#4-everything-plasma-has-that-vanilla-does-not)
5. [The seven items that stop it feeling like pasted-in pictures](#5-the-seven-items-that-stop-it-feeling-like-pasted-in-pictures)
6. [Phase 0 - make the foundation true](#phase-0--make-the-foundation-true)
7. [Phase 1 - the data layer](#phase-1--the-data-layer)
8. [Phase 2 - the screen spine](#phase-2--the-screen-spine)
9. [Phase 3 - the world is alive](#phase-3--the-world-is-alive)
10. [Phase 4 - hands and hitting things](#phase-4--hands-and-hitting-things)
11. [Phase 5 - sound](#phase-5--sound)
12. [Phase 6 - block entities and the rest of the world](#phase-6--block-entities-and-the-rest-of-the-world)
13. [Phase 7 - containers](#phase-7--containers)
14. [Phase 8 - the whole HUD](#phase-8--the-whole-hud)
15. [Phase 9 - options, keys and debug](#phase-9--options-keys-and-debug)
16. [Phase 10 - packs, fonts, languages](#phase-10--packs-fonts-languages)
17. [Phase 11 - the long tail](#phase-11--the-long-tail)
18. [Phase 12 - the Plasma layer](#phase-12--the-plasma-layer)
19. [Totals, and an honest word about them](#totals-and-an-honest-word-about-them)
20. [Appendix A - every packet](#appendix-a--every-packet)
21. [Appendix B - every GUI sprite](#appendix-b--every-gui-sprite)
22. [Appendix C - every language key prefix](#appendix-c--every-language-key-prefix)
23. [Appendix D - what cannot be read out of the jar](#appendix-d--what-cannot-be-read-out-of-the-jar)
24. [Appendix E - what the Horizon study is good for](#appendix-e--what-the-horizon-study-is-good-for)

---

## 1. Where the facts come from

Everything below is checked against one of four sources. Nothing is from memory.

**1. Mojang's own datagen reports, protocol 774.** Generated fresh for this
document:

```sh
curl -sO https://piston-data.mojang.com/v1/objects/64bb6d763bed0a9f1d632ec347938594144943ed/server.jar
java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports --output out774
```

`out774/reports/` gives `packets.json` (251 packets across all phases),
`registries.json` (95 registries), `blocks.json`, `items.json`, `commands.json`,
`datapack.json`, `biome_parameters/` and `json-rpc-api-schema.json`. This is
observable output of the game, not decompiled code. `version.json` inside that
jar confirms `protocol_version: 774`.

Counts that matter, straight out of `registries.json`:

| registry | entries | what it decides |
|---|---:|---|
| `sound_event` | 1838 | the whole sound system |
| `item` | 1505 | item models, tooltips, creative tabs |
| `block` | 1166 | 29,671 states |
| `entity_type` | 157 | every model that has to exist |
| `particle_type` | 115 | every particle provider |
| `data_component_type` | 104 | the item codec (see P1.5) |
| `custom_stat` | 77 | the statistics screen |
| `game_rule` | 59 | the LAN/world screens |
| `block_entity_type` | 49 | the block-entity render path |
| `mob_effect` | 40 | the effect HUD and tooltips |
| `attribute` | 35 | reach, speed, damage, knockback |
| `menu` | 25 | exactly 25 container screens |
| `villager_profession` | 15 | villager texture compositing |
| `creative_mode_tab` | 14 | the creative inventory |
| `recipe_book_category` | 13 | the recipe book tabs |
| `fluid` | 5 | water, lava and their flowing forms |

**2. The player's own 1.21.11 client jar**, at
`/home/treur/.var/app/com.mojang.Minecraft/.minecraft/versions/1.21.11/1.21.11.jar`.
28,165 entries; 10,307 files under `assets/minecraft`. Read for asset layouts,
GUI sprite names, model formats, animation metadata, font definitions. Walked
systematically for this document, because every directory under
`assets/minecraft/` is evidence of a feature:

| directory | files | what it proves exists |
|---|---:|---|
| `textures/block` | 1210 | the block atlas (done) |
| `textures/item` | 792 | item sprites (done as baked icons) |
| `textures/entity` | 631 | **every entity that must be drawn** |
| `textures/gui` | 565 | **every screen and HUD element** |
| `textures/particle` | 255 | particle sprite sheets |
| `textures/trims` | 57 | armour trim palettes |
| `textures/painting` | 52 | painting variants |
| `textures/mob_effect` | 40 | the effect HUD icons |
| `textures/map` | 37 | map decorations |
| `blockstates` | 1168 | state to model (done) |
| `models/block` | 2390 | block geometry (done) |
| `models/item` | 1283 | item geometry |
| `items` | 1505 | **the item model *selector* layer** |
| `equipment` | 44 | armour layer definitions |
| `atlases` | 15 | **Mojang's own list of what to stitch** |
| `particles` | 104 | particle texture lists |
| `shaders/core` | 75 GLSL | every render type and its vertex format |
| `shaders/post` | 6 chains | blur, spider, entity outline, creeper, invert, transparency |
| `lang/en_us.json` | 7767 keys | **every button, option, message and subtitle** |
| `texts` | 4 | splashes, credits, end poem |

**3. The launcher's asset object store**, at
`~/.var/app/com.mojang.Minecraft/.minecraft/assets/`. This is not optional and
it was nearly missed. Significant things are **not in the jar**:

- `sounds.json` itself. There is no sound registry in the jar at all.
- All 4,405 `.ogg` files.
- The title-screen panorama. The six faces in the jar are **1x1 pixel, 69-byte
  stubs**; the real 1024px faces are store-only. A jar-only client has no menu
  background.
- All 142 non-`en_us` locales, and `pack.mcmeta`, which is the 143-locale
  language table the Language screen needs (5 of them right-to-left: `ar_sa`,
  `he_il`, `fa_ir`, `yi_de`, `zlm_arab`).
- `unifont.zip`, `unifont_jp.zip`, `unifont_pua.zip`. The jar's
  `font/include/unifont.json` is literally `{"providers":[]}`. Without the store
  there is no CJK text.
- The two built-in resource packs, and the Realms slideshow.

The chain is: the version's `1.21.11.json` names `assetIndex` id `29`, which is
`assets/indexes/29.json`, which maps a virtual path such as
`minecraft/sounds.json` to `assets/objects/<first two hex>/<sha1>`. Objects have
**no file extension**, so a reader has to sniff content. All 4,591 objects of
index 29 are present on this machine; nothing needs downloading.

**4. Plasma's own source.** Read in full for this document:
`src/tools/client_main.cpp` (2399 lines, the whole client loop),
`src/session.cpp` (853), `src/ui/menu.cpp` (793), `src/gpu/*`, `src/world/*`,
`tools/bake_*.py`. 16,556 lines across 81 files.

**Clean-room is absolute and nothing below breaks it.** No decompiled game code
is read or used. Where a feature genuinely cannot be built from these four
sources, Appendix D says so and prices the clean-room alternative.

---

## 2. What Plasma actually has today

Verified against source, not against the README. This is the baseline every
"Now" column below is measured from.

### Protocol

**Done.** Handshake. Login including encryption (RSA wrap, AES/CFB8) and the
full Microsoft device-code flow through Xbox Live, XSTS, Mojang and the profile
request. Compression. Configuration phase with registry data (biome only), known
packs, code of conduct, keep-alive, ping. Play phase: keep-alive, ping,
`login` (entity id only), `player_position` with teleport confirm and
`player_loaded`, `level_chunk_with_light`, `chunk_batch_finished`,
`player_chat`, `system_chat`, `start_configuration`, `add_entity`,
`entity_position_sync`, `move_entity_pos`, `move_entity_pos_rot`,
`remove_entities`, `light_update`, `block_update`, `section_blocks_update`,
`block_changed_ack`, `game_event` (only sub-event 3, game mode), `set_health`,
`set_experience`, `set_held_slot`, `container_set_content` (container 0 only),
`container_set_slot`, `player_info_update`, `disconnect`.

**That is 26 of 139 clientbound play packets.** Serverbound, 15 of 66 are ever
sent. Appendix A is the full table.

**Partial, and worth knowing which:**

- `container_set_content` / `container_set_slot` read a stack's id and count and
  then **give up the moment the stack carries data components**, marking
  everything after it in the packet unreadable. In practice almost every real
  inventory has such a stack. This is the single largest protocol hole.
- `move_entity_pos_rot` reads the three position deltas and stops - the rotation
  and the on-ground flag are never read.
- `add_entity` reads id, uuid, type and position, and stops - pitch, yaw, head
  yaw, the data field and the velocity are never read.
- `game_event` reads only the game-mode sub-event. Rain start/stop, thunder
  level, the "no respawn block" message, the win game credits, puffer-fish
  sting, elder guardian curse and the rest are discarded.
- `registry_data` parses **one** registry out of the 95 the server sends:
  `minecraft:worldgen/biome`. Dimension type, damage type, chat type, painting
  variant, wolf variant, enchantment, trim material, jukebox song, banner
  pattern, instrument and the rest are read past and thrown away.
- `player_info_update` is parsed only to pull the player's own latency out of it.
  The skin property is read and discarded; the display name, game mode and
  listed flag are read and discarded.

**Two outright bugs found while writing this:**

1. `src/session.cpp` has the `light_update` branch **twice** (lines 540 and 552).
   The second is unreachable. The first calls `events_.on_light`, which nothing
   in `client_main.cpp` ever sets; `client_main.cpp` sets `on_light_update`,
   which is only called from the dead branch. **Light updates are silently
   dropped in the running client.** The hole you mine leaves a shadow.
2. `forget_level_chunk` is named in the generated id table and never handled.
   Chunks are never unloaded: the `world` map, the GPU buffers and the `seen`
   set all grow without bound, and the client stops accepting geometry when the
   384 MB vertex reservation fills.

And one divergence that reads as a bug: on `set_health` reaching zero the client
**immediately sends `client_command` 0 to respawn**. Vanilla shows the death
screen and waits. See section 4.

### Rendering

**Done.** Vulkan 1.3 dynamic rendering, reverse-Z depth, own pipeline cache on
disk. Chunk mesher per 16-cube section with cullface culling, smooth lighting
from the server's own sky and block light, ambient occlusion, element rotation,
`shade`/`ambientocclusion` model flags, the padded 18-cube neighbour window.
16-byte vertex. 1098-layer array atlas with four mip levels, baked from the
player's jar. Seven biome tint kinds against the game's own colour maps. Sky
gradient, sun, cloud deck, linear fog. Block outline. A translucent pass after
the opaque one. Frustum culling against the four side planes. Streaming append
and in-place section replace.

**Absent from the renderer entirely:** any entity, any block entity, any
particle, the held item, weather, a day/night cycle, the moon and stars,
animated textures (the first frame of a strip is baked and that is it), the
void fog, underwater fog and overlay, the nether and end sky, translucent
depth sorting, the three mipmap strategies in the jar's own metadata
(`strict_cutout` on 28 flowers, `dark_cutout` on 13 leaves, `mean` on 5 glass
and redstone), and the six post-processing chains.

### Game logic

**Done.** AABB physics on a fixed 20 Hz tick with vanilla drag and acceleration,
measured by test at 4.317 walk and 5.612 sprint. Camera interpolated between
ticks. View bobbing (sway, nod, lean). Creative flight. DDA raycast to a fixed
4.5 blocks. Mining and placing with local prediction and the same remesh path as
a server-sent change.

**Known-wrong, by its own admission in `physics.hpp`:** only full cubes collide.
A slab, a stair, a fence, a carpet, a chest, a cauldron, a wall - the player
walks straight through all of them. Mojang's reports do not publish collision
shapes. This is P1.9 and Appendix D.

Also absent: block hardness (the dig timer is a flat 0.25 s for every block),
attributes of any kind (reach, speed, knockback), swimming, climbing, water and
lava movement, elytra, riding, sneaking's hitbox change, fall damage,
knockback from the server, the sprint window, auto-jump.

### Interface

**Done.** The game's own hotbar, hearts, food, armour and experience bar from
the real sprites. The real 2414-glyph bitmap font with the game's shadow. 584
item sprites and 419 pre-rendered block cube icons. An inventory screen that
displays 46 slots and does nothing when clicked. Chat display and input. A
Plasma-styled feature menu.

**The number that matters:** `tools/bake_gui.py` already bakes **every** sprite
under `textures/gui/sprites/` plus all 23 legacy container sheets plus the font,
into a 16.8 MB atlas. The HUD uses **13 of them**. The assets are there; the
screens are not. That makes Phase 2 and Phase 7 much cheaper than they look,
and it is the single most encouraging fact in this document.

One correction to that tool: its docstring says *"Nine-slice data is not in the
jar, so sprites that stretch are recorded at their natural size and stretched by
the caller."* That is wrong. **38 sprites carry `nine_slice` metadata** in
`.png.mcmeta` beside them, with a declared width, height and border - scalar, or
per-side for tabs and toasts. `widget/button.png.mcmeta` is 200x20 border 3;
`tooltip/background.png.mcmeta` is 100x100 border 9. Stretching a button
instead of nine-slicing it is why a vanilla-looking widget looks subtly wrong at
any size but 200x20.

### Absent wholesale

No title screen. No server list. No direct connect. No options screen of any
kind. No pause menu. No death screen. No container screens. No creative
inventory. No crafting. No advancements, statistics or social interactions. No
entity rendering. No sound. No particles. No resource packs. No F3. No
perspectives. No screenshots. No tab list. No boss bars, scoreboard, titles or
action bar. No keybind configuration - the keys are hardcoded in
`src/gpu/window.cpp`.

---

## 3. Deliberate exclusions

Named here so they are not holes. A deliberate exclusion is a decision; an
unnoticed one is a bug.

| Excluded | Why |
|---|---|
| **Singleplayer and world generation** | The client is multiplayer-only by design. Worldgen is the one area where, as `docs/ARCHITECTURE.md` already notes, nothing corrects you - there is no server acting as referee - and it is where a clean-room implementation would need an oracle and therefore a decision about Mojang's official mappings. Horizon's worldgen is 56,797 lines across 126 files for one version. Out of scope, and the commercial product does not need it. |
| **World saving and DataFixerUpper** | Follows from the above. No local worlds, nothing to load or upgrade. |
| **Realms** | 353 `mco.*` and 26 `realms.*` language keys, 9 `gui/realms/` textures, 5 `sprites/realm_status/`, 4 `sprites/pending_invite/`. A paid Mojang service with its own authenticated API. Not reachable by a third-party client in any supported way. |
| **Server management RPC** | `json-rpc-api-schema.json`, 69 `incoming_rpc_methods` and 17 `outgoing_rpc_methods` in the registry. Server-side only; a client never speaks it. |
| **The `test_*` family** | `test_block`, `test_instance_block`, `test_environment_definition_type`, `game_test_highlight_pos`, `test_instance_block_status`, `test_instance_block_action`, 45 `test.*` language keys. Mojang's internal game-test harness, reachable only on a server with it enabled and an operator. Parked at the very end of Phase 11 rather than excluded, because a server *can* send `test_instance_block_status` and the client must not fall over. |
| **Telemetry** | 46 `telemetry.*` keys, the `options.telemetry` screen. Plasma sends Mojang no telemetry and will not. The screen is replaced by a one-line statement that this client collects nothing. This is the one place where diverging from vanilla is the right answer, and it belongs in section 4 as a known, defended divergence. |
| **Abuse and chat reporting** | 66 `gui.abuseReport.*`, 26 `gui.chatReport.*`, 27 `gui.banned.*`, 7 `gui.chatSelection.*` keys. The reporting pipeline talks to a Mojang service with a signed session. A third-party client cannot submit reports and should not pretend it can. The `menu.playerReporting` button is removed and the Social Interactions screen keeps its mute and hide functions, which are local. |
| **Demo mode** | 19 `demo.*` keys, `gui/demo_background.png`. Needs an unlicensed session; the client checks ownership before it does anything. |
| **Java mod loading** | Not a parity item. |

Everything else in the jar and in `packets.json` is assigned to a phase below.

---

## 4. Everything Plasma has that vanilla does not

The rule is *niks erin wat er normaal niet in zit*. In the vanilla profile the
client must contain nothing vanilla does not have. The profile switch already
exists - `features::Profile::{Vanilla, Plasma}` and `Registry::shows()` in
`include/mcpp/features.hpp` - but only six HUD features consult it, and several
divergences bypass it entirely.

**Gate behind the Plasma profile** (keep the code, draw nothing in vanilla):

| Thing | Where | Status |
|---|---|---|
| The feature menu on `m` | `src/ui/menu.cpp` lines 600-793 | **ungated**: `draw_menu` is called whenever `menu_open`, with no profile check. In vanilla, `m` must do nothing and Escape must open the pause menu. |
| FPS counter | `menu.cpp:311` | gated ✓ |
| Ping display | `menu.cpp:314` | gated ✓ |
| CPS counter | `menu.cpp:317` | gated ✓ |
| Coordinates overlay | `menu.cpp:571` | gated ✓ (vanilla shows coordinates only in F3) |
| Keystroke display | `menu.cpp:578` | gated ✓ |
| Plasma hotbar styling | `menu.cpp:327` | gated, but falls back to the Plasma look when `!quads.has_sprites()`. In vanilla, a missing sprite atlas is a fatal error, not a restyle. |
| GPU-ms and CPU-ms readouts | `client_main.cpp:1060-1062` into `HudState` | **ungated** as state; only drawn under gated features, but remove from the vanilla build's HUD state entirely. |
| Rounded-rectangle SDF widgets | `src/gpu/ui.cpp`, `shaders/ui.frag` | Vanilla has no rounded corners and no glow anywhere. The `Quad` struct's `radius`, `border`, `glow`, `glow_rgba` and the gradient `fill_top`/`fill_bottom` are all non-vanilla. Keep the renderer, forbid the parameters in the vanilla profile. |
| The 1600x900 fixed design space | `menu.hpp`, `draw_menu` | Vanilla scales the GUI by integer steps (see P2.2). A fixed design space scaled to fit is a different thing and gives non-integer sprite scales, which is why the HUD looks soft. **This has to come out of the vanilla path, not be gated** - the HUD shares it. |

**Remove or replace with vanilla behaviour** (these are not styling, they are
different game behaviour):

| Divergence | Now | Vanilla | Fixed by |
|---|---|---|---|
| Auto-respawn on death | `session.cpp:594`, sends `client_command` 0 the moment health hits 0 | Death screen, waits for the player | P2.11 |
| `f` toggles creative flight locally | `client_main.cpp`, `input.toggle_fly` | Double-tap jump, and only if `player_abilities` granted it | P1.11, P4.2 |
| Flight physics are invented | `client_main.cpp:836-857`, `kFlyAccel 0.1`, `kFlyDrag 0.91`, vertical drag `0.6` | Vanilla's own flying drag and the fly-speed attribute | P1.11 |
| Flat 0.25 s dig time | `client_main.cpp:938` | Hardness, tool, efficiency, haste, fatigue, water, ground | P4.6 |
| Reach is a constant 4.5 | `client_main.cpp:81` `kReach` | The `block_interaction_range` attribute (4.5 survival, 5.0 creative) and `entity_interaction_range` 3.0 | P1.10 |
| Sprint FOV hardcoded 1.15x | `client_main.cpp:889` | Driven by the movement-speed attribute and scaled by the `fovEffectScale` option | P1.10, P9.x |
| View bobbing always on | `client_main.cpp:872-881` | An option, and it is suppressed in third person and while flying | P9.x |
| Face always 1 (up) for dig and place | `client_main.cpp:933`, `send_place(..., 1, 0.5f, 1.0f, 0.5f)` | The face the ray actually entered, and the real cursor position on it | P4.5 |
| `client_information` is hardcoded | `session.cpp:272-283`: `en_us`, view distance 8, chat enabled, all skin parts, right hand, no filtering, listings on, particles all | The player's own options, and re-sent when they change | P9.3 |
| Zero known packs reported | `session.cpp:300` | The client's own built-in pack versions | P10.2 |
| Resource packs declined outright | `session.cpp:347`, always sends "declined" | Prompt, download, verify, apply | P10.3 |
| Chat sent unsigned | `session.cpp:816`, salt 0, no signature | Signed with the session key when the server has secure chat on. An unsigned client is kicked by `enforce-secure-chat=true` servers, which is most of them. | P8.9 |
| Code of conduct auto-accepted | `session.cpp:356` | A screen the player reads and accepts | P2.12 |
| Telemetry screen | absent | Present in vanilla | **Defended divergence**: replaced by a statement that this client sends none. |

**Development tooling that is fine to keep but is not a vanilla client.** These
are subcommands, not behaviour: `bench`, `walktest`, `worldshot`, `hudshot`,
`menushot`, `showcase`, `gpu`, `join`, `play`, `-DMCPP_DEV_OFFLINE`,
`debug_packets`, `chunk_dump`. Keep every one of them - `showcase` in particular
is how rendering bugs get found here, and the equivalent for entities is P3.14.
But the shipped vanilla profile launches to a title screen with no arguments
(P2.4), and the current situation where the only way in is
`plasma-client play <host>` is itself a parity gap.

**One thing to decide now, because it changes Phase 12.** `features.hpp` says
the feature list must contain nothing giving information or input vanilla does
not have, and then lists `Hitboxes` and `Fullbright`. Vanilla has both: F3+B
draws hitboxes, and the Brightness slider plus the Night Vision effect is
fullbright's honest ceiling. So those two are parity items (P9.9, P9.x), not
Plasma features, and should move out of the registry into the options screen
where vanilla keeps them. `ToggleSprint`, `ToggleSneak` and `Zoom` have no
vanilla equivalent and stay in Phase 12.
