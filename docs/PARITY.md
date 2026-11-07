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

**Superseded on 2026-11-03.** This plan was written under a clean-room rule
that no longer holds: vanilla's client, decompiled locally with Mojang's
official mappings, is now the reference, translated method by method and
tracked by the port ledger (`docs/METHOD.md`, `docs/GAMEPLAN.md`). The
clean-room routes discussed below, Appendix D included, are kept as history.
What has not changed: nothing of Mojang's enters the repository or ships.

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

1. `src/session.cpp` has the `light_update` branch **twice** (lines 542 and 552).
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
into a 16.8 MB atlas. The HUD uses **15 of them**: eleven from `sprites/hud/`, three hearts, and one of the 23 legacy container sheets. The assets are there; the
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
| GPU-ms and CPU-ms readouts | `client_main.cpp:1069-1070` into `HudState` | **ungated** as state; only drawn under gated features, but remove from the vanilla build's HUD state entirely. |
| Rounded-rectangle SDF widgets | `src/gpu/ui.cpp`, `shaders/ui.frag` | Vanilla has no rounded corners and no glow anywhere. The `Quad` struct's `radius`, `border`, `glow`, `glow_rgba` and the gradient `fill_top`/`fill_bottom` are all non-vanilla. Keep the renderer, forbid the parameters in the vanilla profile. |
| The 1600x900 fixed design space | `menu.hpp`, `draw_menu` | Vanilla scales the GUI by integer steps (see P2.2). A fixed design space scaled to fit is a different thing and gives non-integer sprite scales, which is why the HUD looks soft. **This has to come out of the vanilla path, not be gated** - the HUD shares it. |

**Remove or replace with vanilla behaviour** (these are not styling, they are
different game behaviour):

| Divergence | Now | Vanilla | Fixed by |
|---|---|---|---|
| Auto-respawn on death | `session.cpp:604`, sends `client_command` 0 the moment health hits 0 | Death screen, waits for the player | P2.21 |
| `f` toggles creative flight locally | `client_main.cpp`, `input.toggle_fly` | Double-tap jump, and only if `player_abilities` granted it | P1.11, P4.2 |
| Flight physics are invented | `client_main.cpp:836-857`, `kFlyAccel 0.1`, `kFlyDrag 0.91`, vertical drag `0.6` | Vanilla's own flying drag and the fly-speed attribute | P1.11 |
| Flat 0.25 s dig time | `client_main.cpp:967` | Hardness, tool, efficiency, haste, fatigue, water, ground | P4.6 |
| Reach is a constant 4.5 | `client_main.cpp:87` `kReach` | The `block_interaction_range` attribute (4.5 survival, 5.0 creative) and `entity_interaction_range` 3.0 | P1.10 |
| Sprint FOV hardcoded 1.15x | `client_main.cpp:914` | Driven by the movement-speed attribute and scaled by the `fovEffectScale` option | P1.10, P9.x |
| View bobbing always on | `client_main.cpp:892-904` | An option, and it is suppressed in third person and while flying | P9.x |
| Face always 1 (up) for dig and place | `client_main.cpp:976`, `send_place(..., 1, 0.5f, 1.0f, 0.5f)` | The face the ray actually entered, and the real cursor position on it | P4.5 |
| `client_information` is hardcoded | `session.cpp:278-286`: `en_us`, view distance 8, chat enabled, all skin parts, right hand, no filtering, listings on, particles all | The player's own options, and re-sent when they change | P9.3 |
| Zero known packs reported | `session.cpp:312` | The client's own built-in pack versions | P10.2 |
| Resource packs declined outright | `session.cpp:372`, always sends "declined" | Prompt, download, verify, apply | P10.3 |
| Chat sent unsigned | `session.cpp:838`, salt 0, no signature | Signed with the session key when the server has secure chat on. An unsigned client is kicked by `enforce-secure-chat=true` servers, which is most of them. | P8.9 |
| Code of conduct auto-accepted | `session.cpp:376` | A screen the player reads and accepts | P2.22 |
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

---

## 5. The seven items that stop it feeling like pasted-in pictures

*"Het voelt allemaal als ingeplakte plaatjes."* That is the actual complaint and
it deserves a direct answer rather than a 200-item list to read first. The
feeling has a cause, and in two cases the cause is literal: there really are
pasted-in pictures in the build.

**1. The inventory icons are pre-rendered PNGs. (P4.11, P7.3) - done**
`tools/bake_items.py` and `tools/item_render.py` now resolve each item to its
own model and render that model, the way vanilla renders one into a cell of
its item atlas: FaceBakery's vertex order and default uvs, ItemTransform's
`gui` placement, the two directional lights of Lighting's ITEMS_3D entry and
light.glsl's mix of them, against a depth buffer. A stair, a torch, a fence, a
cake, an anvil, a hopper and a cauldron all come out as the model they are.
1426 of 1505 items have an icon, up from 1006.

What is left is the 78 items vanilla draws with a block entity's model - a
chest, a bed, a banner, a shulker box, a skull, the shield, the conduit, the
decorated pot and the copper golem statue. Their geometry is in the entity
model table `tools/export/export_models.py` already writes, and each one needs
its own `*SpecialRenderer.submit` placement ported; nothing else is missing.

The one deviation left is resolution. Vanilla re-renders its atlas at sixteen
times the gui scale whenever that changes; the bake has to pick one, and picks
32, which is gui scale two exactly. A larger scale enlarges that rather than
being rendered at its own size. Rendering the icons into an atlas on the gpu,
once per item state, is what closes it.

**2. Nothing in the world is alive. (P3.17 through P3.27)**
Other players and every mob are invisible. A multiplayer server with twenty
people on it looks like an empty world with chat happening. There is no
intermediate version of this that helps - a capsule or a box would look worse
than nothing - so the fix is the real humanoid model, the real skin fetched from
the player's profile, the second skin layer, and the nametag. That is one
coherent chunk of work (about two weeks) and it changes the client more than
anything else in this document.

**3. You cannot see your own hands. (P4.1 through P4.4) - done**
ItemInHandRenderer is translated whole into `src/client/hand.cpp`: where the
arm and the item rest, the swing, the equip dip, eating and drinking, the bow
and crossbow draw, blocking, the brush, and the hurt reel and walk bob under
all of it. Every pose is checked against the game's own private methods by
`tools/oracle/HandOracle.java`, matrix by matrix, to the bit.

The hand is drawn where the game draws it: last in the world's pass, with the
depth cleared in front of it and a seventy degree projection of its own.
`tools/bake_held.py` writes every item's model as the hand holds it, flat
sprites extruded into relief the way ItemModelGenerator does, and the whole
frame - both hands, the model, the sleeve - is one draw.

What the window cannot feed it yet is the item use state, so eating and the
bow draw are exercised offscreen only, and a map in the hand is not drawn.

**4. It is completely silent. (Phase 5)**
1838 sound events, 1740 of them with a subtitle, and not one plays. Footsteps
alone carry an enormous amount of the sense that you are standing in a place
rather than looking at one. Sound is also fully independent of every other item
here, so it can run in parallel with anything.

**5. Nothing you break makes a mark. (P4.6 through P4.10)**
Mining is instantaneous after a flat 0.25 seconds, with no crack overlay
growing across the block face, no breaking particles, no sound, and no
block-destruction progress from other players' mining. Vanilla's ten-frame
`destroy_stage_0..9` overlay on the block you are hitting is the feedback loop
of the entire game.

**6. The interface is an overlay, not screens. (Phase 2)**
There is no title screen, so the client has no front door; there is no pause
menu, so Escape just leaves; there is no death screen, so dying is a silent
teleport. The HUD is drawn in a fixed 1600x900 design space scaled to fit,
which gives non-integer sprite scales - an 8-pixel-tall hotbar drawn at 1.37
pixels per pixel is a blurred hotbar, and *blurred vanilla sprites are exactly
what "pasted-in" looks like*. Vanilla's integer GUI scale (P2.2) is a two-hour
fix that sharpens every pixel of the existing HUD.

**7. The world does not move. (P6.10, P6.11, P6.14)**
No day/night cycle: the sky is whatever colour the standing biome asks for, at a
fixed time. No weather. And 62 textures in the jar declare an `animation` in
their metadata - water, lava, fire, portal, sea lantern, prismarine, magma,
command blocks, the campfire - of which the baker takes **the first frame and
discards the rest**. Still water, still lava and still fire are a strong
uncanny signal even when you cannot say why.

**If you only did three.** Items 2, 3 and 6 in that order. Entity rendering and
the first-person hand give the world bodies; the integer GUI scale and the screen
spine give it a front door. After those three the client stops reading as a tech
demo, and every remaining item is parity rather than impression.

---

## Phase 0 - make the foundation true

**Goal.** Fix what is wrong in what already exists, before anything is built on
top of it. Every item here is small. Two are outright bugs.

**Demonstrable at the end.** `plasma-client play` runs for an hour in a busy
chunk of a real server without the GPU buffer filling, with light correct after
mining, and reports a dimension's real height instead of assuming the overworld.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P0.1 | Dead `light_update` branch | `src/session.cpp` has the branch twice; the second is unreachable, and the live one calls `on_light`, which nothing sets. Delete the duplicate, keep one callback name. | broken | - | 20 m | **!** | both |
| P0.2 | `forget_level_chunk` | Unload the chunk: drop it from `world`, from `seen`, free its sections in the renderer. Without it nothing is ever unloaded. | none | - | 4 h | **!** | both |
| P0.3 | Free GPU section room | `WorldRenderer::replace` leaks the old section's room by design. With P0.2 that becomes a real allocator: a free list over the vertex and index buffers, or periodic compaction. | none | P0.2 | 1 d | | fast |
| P0.4 | `set_chunk_cache_center` / `_radius` | The server tells you which chunks it considers loaded. Use it to decide what to drop rather than guessing. | ignored | P0.2 | 2 h | ‖ | both |
| P0.5 | Dimension height is hardcoded | `y + 64` appears in five places and `min_section_y = -4` is a default argument. Both are overworld constants. The nether is 0..256 and the end 0..256, so a dimension change mis-decodes every chunk. | broken off-overworld | P1.2 | 4 h | **!** | both |
| P0.6 | `respawn` packet | Named in the id table, never handled. On a dimension change the whole world has to be dropped, the dimension type re-read and the renderer reset. | none | P0.5 | 4 h | | both |
| P0.7 | `add_entity` full read | Read pitch, yaw, head yaw, the data int and the three velocity shorts instead of stopping after position. | partial | - | 1 h | ‖ | both |
| P0.8 | `move_entity_pos_rot` full read | Read the two angle bytes and the on-ground flag. | partial | - | 30 m | ‖ | both |
| P0.9 | `move_entity_rot` | Rotation-only movement, currently ignored. A mob turning on the spot sends only this. | none | P0.8 | 30 m | ‖ | both |
| P0.10 | `bundle_delimiter` | Packets between two delimiters must be applied as one unit. The server bundles an entity's spawn with its metadata and equipment, and applying them separately gives one frame of a naked default-skinned player. | ignored | - | 3 h | **!** | both |
| P0.11 | Translucent depth sort | Sections draw their translucent indices after their opaque ones, but the sections themselves are not ordered back to front and the quads inside a section are not sorted. Water behind water is wrong. | partial | - | 1 d | | fast |
| P0.12 | Mipmap strategies | `strict_cutout` (28 sprites), `dark_cutout` (13 leaves), `mean` (5 glass and redstone) and `alpha_cutoff_bias` on kelp are declared in `.png.mcmeta` and ignored by the baker. Leaves and flowers bleed at distance. | ignored | - | 1 d | ‖ | fast |
| P0.13 | Nine-slice in the GUI baker | Read the 38 `gui.scaling` blocks, store border and inner mode per sprite, draw nine-slice instead of stretch. Correct the docstring that says this data is not in the jar. | wrong | - | 1 d | **!** | both |
| P0.14 | Fluid height interpolation | Only the top block of a column is lowered, which fixed the layer lines, but real fluid corner heights interpolate across the four diagonal neighbours. | partial | - | 1 d | ‖ | fast |
| P0.15 | A fixed chunk corpus on disk | `docs/ARCHITECTURE.md` already names this: two renders of the same scene differed because the server had generated more world. Dump N chunks once, replay from disk. Every later item's before/after picture depends on it. | none | - | 1 d | **!** | both |

**Phase total: about 8 days.**

**Mechanism worth writing down - why P0.5 is bigger than it looks.** The number
of sections in a chunk packet is not in the packet, and `docs/ARCHITECTURE.md`
already works out that reading until the blob is empty avoids needing the
registry. That is true and it still works. But *where* those sections start -
the y of the lowest one - is `min_y` from the dimension type, and that is used
to turn a world y into an array index. Overworld is `min_y: -64, height: 384`,
so `y + 64` is right there and only there. The fix is not a constant swap: it is
plumbing `min_y` from the registry (P1.2) through the chunk, the mesher, the
physics `block_at` closure and the renderer's section origins. Do it in Phase 0
rather than after Phase 3, because entity positions are also absolute and will
inherit the same mistake.

---

## Phase 1 - the data layer

**Goal.** Everything the server tells you, decoded. No pixels in this phase at
all. This is the phase that unlocks the most per hour spent, because three
codecs here - registries, data components and entity metadata - are the
prerequisite for roughly half of the remaining document.

**Demonstrable at the end.** A new subcommand, `plasma-client inspect <host>`,
that joins and prints: the dimension and its height, every registry the server
sent with its entry count, every entity in range with its type name, pose, custom
name, equipment and effects, and every item in the inventory with its real name,
durability, enchantments and lore. All of it checkable by eye against the vanilla
client standing in the same place. No renderer involved, so a mistake is visible
rather than plausible-looking.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P1.1 | Generic registry reader | `registry_data` arrives once per registry: a name, a count, then per entry an id and an optional NBT body. Store all 95 generically - name to index, index to NBT - instead of special-casing biome. | biome only | - | 1 d | **!** | both |
| P1.2 | `dimension_type` | min_y, height, has_skylight, has_ceiling, ambient_light, bed_works, respawn_anchor_works, piglin_safe, monster_spawn_light_level, coordinate_scale, effects (which sky to draw), infiniburn, cloud_height. Drives P0.5, P0.6, P6.12, P6.13. | none | P1.1 | 4 h | **!** | both |
| P1.3 | `damage_type` | 40-odd entries with a message id, exhaustion, scaling and effects. Without it a death message is an untranslated key. | none | P1.1 | 2 h | ‖ | both |
| P1.4 | `chat_type` | The decoration template per chat type. `read_player_chat` currently formats `<name> body` by hand; vanilla applies the registry's own template. | hand-rolled | P1.1 | 3 h | | both |
| P1.5 | **Data component codec** | 104 component types, each with its own wire body. This is the hole that makes `container_set_content` give up. Needs a reader per type: `custom_name`, `lore`, `damage`, `max_damage`, `enchantments`, `stored_enchantments`, `custom_model_data`, `dyed_color`, `trim`, `potion_contents`, `food`, `tool`, `attribute_modifiers`, `can_place_on`, `can_break`, `profile`, `bundle_contents`, `container`, `written_book_content`, `writable_book_content`, `map_id`, `map_decorations`, `firework_explosion`, `fireworks`, `banner_patterns`, `base_color`, `pot_decorations`, `instrument`, `jukebox_playable`, `block_state`, `bees`, `lock`, `note_block_sound`, `ominous_bottle_amplifier`, `suspicious_stew_effects`, `charged_projectiles`, `recipes`, `unbreakable`, `hide_tooltip`, `enchantment_glint_override`, `repair_cost`, `max_stack_size`, `rarity`, `item_name`, `item_model`, `equippable`, `glider`, `tooltip_style`, `death_protection`, `blocks_attacks`, `weapon`, `consumable`, `use_remainder`, `use_cooldown`, `damage_resistant`, `provides_banner_patterns`, `provides_trim_material`, `break_sound`, `villager_variant`, `wolf_variant`, `wolf_sound_variant`, `wolf_collar`, `fox_variant`, `salmon_size`, `parrot_variant`, `tropical_fish_pattern`, `tropical_fish_base_color`, `tropical_fish_pattern_color`, `mooshroom_variant`, `rabbit_variant`, `pig_variant`, `cow_variant`, `chicken_variant`, `frog_variant`, `horse_variant`, `painting_variant`, `llama_variant`, `axolotl_variant`, `cat_variant`, `cat_collar`, `sheep_color`, `shulker_color`, `potion_duration_scale`, `entity_data`, `bucket_entity_data`, `block_entity_data`, `creative_slot_lock`, `intangible_projectile`, `debug_stick_state`, `writable_book_content`, `tooltip_display`, `break_sound`. The registry gives the exact list and ids; the wire layout of each body comes from the protocol wiki and is checked against real bytes. | none | P1.1 | **1 w** | **!** | both |
| P1.6 | Item registry and names | 1505 items, id to name, plus `items.json`'s default components so a stack that omits a component still tooltips correctly. `pretty_item()` currently title-cases the registry name; the real name is `item.minecraft.<name>` in the language file. | partial | P1.5 | 3 h | | both |
| P1.7 | Full inventory read | With P1.5 the 46 slots, the cursor stack and `set_player_inventory` all decode. Also `set_cursor_item` and `container_set_data`. | partial | P1.5 | 4 h | | both |
| P1.8 | **Entity metadata codec** | `set_entity_data` is an index, a serializer type and a value, terminated by index 255. About 30 serializer types: byte, int, long, float, string, component, optional component, item stack, boolean, rotations, block pos, optional block pos, direction, optional uuid, block state, optional block state, NBT, particle, particle list, villager data, optional int, pose, cat variant, wolf variant, frog variant, optional global pos, painting variant, sniffer state, armadillo state, vector3, quaternion. Every entity appearance below depends on this. | none | P1.1 | **3 d** | **!** | both |
| P1.9 | **Collision shapes** | A hand-authored table of per-state collision boxes. Not in the reports. About 40 distinct shape families cover all 1166 blocks: full cube, slab (2 halves), stair (4 facings x 2 halves x 5 shapes), fence (post + 4 arms), fence gate, wall (post + 4 arms x 3 heights), pane, door, trapdoor, carpet, snow layer (8), cake (7 bites), bed, chest, ender chest, cauldron, hopper, anvil, piston, piston head, enchanting table, farmland, grass path, soul sand, end portal frame, lectern, stonecutter, bell, brewing stand, campfire, candle, sea pickle, turtle egg, pointed dripstone, amethyst bud, chain, lightning rod, scaffolding, conduit, flower pot, honey block, slime block, ladder, vine, button, lever, pressure plate, rail, redstone wire, torch, no-collision (plants, air, fluid, banner, sign, water). | full cubes only | - | **3 d** | **!** | both |
| P1.10 | Attributes | `update_attributes`, 35 attributes with base values and modifiers (add, multiply base, multiply total). The ones a client must have: `movement_speed`, `flying_speed`, `attack_speed`, `attack_damage`, `block_interaction_range`, `entity_interaction_range`, `scale`, `step_height`, `jump_strength`, `gravity`, `safe_fall_distance`, `fall_damage_multiplier`, `knockback_resistance`, `water_movement_efficiency`, `sneaking_speed`, `movement_efficiency`, `mining_efficiency`, `submerged_mining_speed`, `block_break_speed`, `burning_time`, `explosion_knockback_resistance`, `max_health`, `armor`, `armor_toughness`. Replaces the hardcoded 4.5 reach and the 1.15x sprint FOV. | none | P1.1 | 1 d | **!** | pvp |
| P1.11 | `player_abilities` | invulnerable, flying, allow_flying, instabuild, fly speed, walk speed. Replaces the `f` key and the invented flight constants. | none | - | 3 h | | both |
| P1.12 | `update_mob_effect` / `remove_mob_effect` | Effect id, amplifier, duration, flags (ambient, show particles, show icon, blend). 40 effects. Needed for the HUD, for speed and jump boost in physics, and for mining fatigue in the dig timer. | none | P1.1 | 4 h | | pvp |
| P1.13 | `update_tags` | Block, item, fluid, entity and game-event tags. A tag decides whether a block is in `minecraft:mineable/pickaxe`, which the dig timer needs, and whether a fluid is water. | ignored | P1.1 | 4 h | | both |
| P1.14 | Block hardness and tool table | A hand-authored data file: destroy time and required tool tier per block. Public data, not in any report. About 1166 rows, most of them a family default. | none | P1.13 | 1 d | | both |
| P1.15 | `change_difficulty` / `lock_difficulty` | Difficulty and whether it is locked. Drives the hardcore heart sprites (there are 24 of them) and the options screen. | ignored | - | 1 h | ‖ | both |
| P1.16 | `game_event` in full | 16 sub-events, of which only 3 is handled. 0 no respawn block, 1 begin raining, 2 end raining, 3 game mode, 4 win game, 5 demo event, 6 arrow hit player, 7 rain level, 8 thunder level, 9 puffer sting, 10 elder guardian effect, 11 immediate respawn, 12 limited crafting, 13 wait for level chunks, 14 ender dragon sound. | partial | - | 3 h | | both |
| P1.17 | `set_time` | World age and time of day, with the "frozen" sign bit. Everything in P6.10 hangs off this and it is 20 minutes of work. | none | - | 30 m | **!** | both |
| P1.18 | `player_info_update` in full | All 8 actions: add player with its properties, chat session, game mode, listed, latency, display name, list priority, hat. Currently only latency is kept. The `textures` property is the skin URL. | partial | - | 4 h | **!** | both |
| P1.19 | `player_info_remove` | Named in the id table, never handled. | none | P1.18 | 30 m | | both |
| P1.20 | Entity type table | 157 entity types with their width, height, eye height, category and whether they are summonable. **Dimensions are not in any report** - see Appendix D. Hand-authored, or exported. Needed for hitboxes, nametag height, shadow size and interaction raycasts. | none | P1.1 | 1 d | **!** | both |
| P1.21 | The tick clock | Physics already runs on a 20 Hz accumulator. Promote it to a real client tick that everything else hangs off: entity interpolation, animation counters, particle ageing, sound ageing, the glint scroll, the destroy-stage counter, texture animation frames. One counter, one place. | partial | - | 4 h | **!** | both |
| P1.22 | `custom_payload` both ways | Plugin channels. `minecraft:brand` must be answered (vanilla replies "vanilla"; this client replies with its own brand, which is correct and is what every modded client does). Needed in configuration and play. | ignored | - | 2 h | ‖ | both |
| P1.23 | `store_cookie` / `cookie_request` / `transfer` | Server transfer with cookies. A proxy network uses this to move you between backends. Login-phase cookies are already answered with "none"; the play and configuration phases are not handled, and `transfer` must reconnect. | partial | - | 4 h | | both |
| P1.24 | `commands` tree | The command graph: 57 argument parser types, literals, redirects, suggestion providers. Decoded here, used by P8.11. | ignored | P1.1 | 2 d | | both |
| P1.25 | `update_recipes` / `recipe_book_*` | Recipe display data and the recipe book's own add/remove/settings packets. Decoded here, used by P7.12. | ignored | P1.1, P1.5 | 1 d | | fast |
| P1.26 | `server_data` / `server_links` / `custom_report_details` | MOTD and icon, the server's link list for the pause menu, and the crash-report detail block. | ignored | - | 3 h | ‖ | both |
| P1.27 | `ticking_state` / `ticking_step` | Tick rate and freeze. A frozen server must not make the client extrapolate entities forward. | ignored | P1.21 | 2 h | ‖ | both |
| P1.28 | `cooldown` | Per item-group cooldown, drawn as the white sweep over a hotbar slot. | ignored | P1.6 | 1 h | ‖ | both |
| P1.29 | `chunks_biomes` | Biome-only updates for already-loaded chunks. Remesh without re-sending blocks. | ignored | P0.1 | 2 h | ‖ | both |

**Phase total: about 5 weeks.** Three items (P1.5, P1.8, P1.9) are two and a half
of those five weeks and nothing in Phases 3, 4, 6 or 7 can start without them.

**Mechanism worth writing down - the component codec is where a client lives or
dies.** Since 1.20.5 an item stack is not an id, a count and an NBT tag. It is
an id, a count, a count of added components, a count of removed components, then
each added component as a varint type id followed by a body whose shape is
decided entirely by that type. There is no length prefix on a component body, so
**you cannot skip a component you do not understand**. Reading one wrong does
not corrupt that item; it desynchronises the rest of the packet, which is
exactly what `read_slot`'s `readable` flag is honestly reporting today. This has
three consequences worth planning around:

1. All 104 types have to be implemented before any of them is reliable. There is
   no useful partial. Budget the week.
2. The reports give the exact type list and the exact protocol ids, so there is
   no guessing about *which* types exist - only about each body's layout, which
   comes from the wiki and is then checked against real bytes from a real
   server. That is the same method that found Mojang's empty-key NBT wrapper
   (`{"": 5}` rather than `5`) during the chat work, and it is reliable.
3. Write the reader as a table of 104 function pointers indexed by protocol id,
   generated from `registries.json` the way `packet_ids_gen.cpp` is generated
   from `packets.json`. Ids move between versions exactly as packet ids do.

**Mechanism worth writing down - entity metadata terminates, it does not count.**
`set_entity_data` has no field count. It is a stream of (index byte, serializer
varint, value) triples ended by an index of `0xFF`. The serializer id indexes
into a fixed list that is *not* in `registries.json`, so the list comes from the
wiki. Same rule as components: an unknown serializer type cannot be skipped, and
the packet after it is lost. Unlike components there are only about 30, so this
is three days rather than a week.

**Mechanism worth writing down - why collision shapes have to be hand-built.**
`blocks.json` gives every state's properties and nothing about its shape.
The block *models* give visual geometry, and for roughly 80% of blocks the
visual and collision shapes coincide - but not for the ones players notice. A
fence is 1.5 blocks tall to walk into and 1 block tall to look at. A wall is
similar. Soul sand and farmland sit slightly lower than they look. A honey block
has a smaller box than its model. Deriving collision from the model therefore
produces a client where *jumping over a fence works* - and nothing else is
wrong, which is worse, because the bug is invisible until someone tries to
escape over one. Hand-authoring 40 shape families is three days and it is
checkable: stand in a doorway, walk a staircase, jump a fence, and compare
against the vanilla client at the same coordinates.

---

## Phase 2 - the screen spine

**Goal.** The client gets a front door and the ability to stop. Everything here
is independent of Phase 3; the two phases can run in parallel if there are two
people. The screen *framework* is what makes Phases 7, 9, 10 and 11 tractable,
which is why it is this early despite being UI.

**Demonstrable at the end.** Launch `plasma-client` with no arguments. A title
screen with the panorama turning behind it. Click Multiplayer, see your server
list with live pings and MOTDs, add a server, join it, press Escape, see the
pause menu, die, see the death screen, press Respawn, disconnect back to the
title.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P2.1 | Asset object store reader | Resolve a virtual path through `assets/indexes/<id>.json` to `assets/objects/xx/<sha1>`, sniffing content because objects have no extension. Sounds, the panorama, the locale list and unifont are all store-only. | none | - | 4 h | **!** | both |
| P2.2 | **Integer GUI scale** | Vanilla picks the largest integer `s` where `width/s >= 320` and `height/s >= 240`, clamped by the `guiScale` option (0 = auto), and draws the entire interface in that virtual space. Replaces the 1600x900 design space. Every existing HUD sprite gets sharp as a side effect. | wrong | - | 3 h | **!** | both |
| P2.3 | Screen stack and framework | A stack of screens with push, pop, and a `Screen` that gets `init(width,height)`, `render`, `mouseClicked`, `keyPressed`, `charTyped`, `tick`. Escape pops. A screen owns widgets; widgets own focus. This is the spine of 30 screens. | none | P2.2, P0.13 | 3 d | **!** | both |
| P2.4 | Widget: button | Nine-slice `widget/button`, `_highlighted`, `_disabled`, centred label, hover and disabled states, the click sound hook (wired in Phase 5). | none | P2.3 | 4 h | | both |
| P2.5 | Widget: slider | `widget/slider`, `_highlighted`, `slider_handle`, `_highlighted`, drag, keyboard step, the "%s: %s" label formats from `options.generic_value`, `options.percent_value`, `options.pixel_value`, `options.multiplier`. | none | P2.4 | 4 h | | both |
| P2.6 | Widget: cycle button | The option button that cycles values and shows `options.on.composed` / `options.off.composed`. | none | P2.4 | 2 h | ‖ | both |
| P2.7 | Widget: text field | `widget/text_field`, `_highlighted`, caret, selection, scroll, clipboard, the 1-2 character suggestion ghost text, a validity filter. | none | P2.3 | 1 d | | both |
| P2.8 | Widget: checkbox | `widget/checkbox`, `_selected`, `_highlighted`, `_selected_highlighted`. | none | P2.4 | 1 h | ‖ | both |
| P2.9 | Widget: scrolling list | The pattern behind the server list, world list, options rows, pack lists, statistics and social interactions: a scrollbar from `widget/scroller` and `scroller_background`, per-row hover and selection, header and footer separators (`header_separator.png`, `footer_separator.png`, and the `inworld_` variants for when a world is behind the screen), and the `menu_list_background` tiling. | none | P2.3 | 1 d | **!** | both |
| P2.10 | Widget: tabs | `widget/tab`, `_highlighted`, `_selected`, `_selected_highlighted`, `tab_header_background.png`. Per-side nine-slice border `{2,2,2,0}`. Used by options, packs, advancements, statistics. | none | P2.4 | 3 h | ‖ | both |
| P2.11 | Tooltip rendering | `sprites/tooltip/background` nine-slice, border 9, placed to stay on screen, multi-line, the `stretch_inner` mode its metadata asks for. Used by every screen and every item. | none | P2.3 | 4 h | **!** | both |
| P2.12 | Menu background | `menu_background.png` tiled at 32px, `menu_list_background.png`, and the in-world variants with the blur from `options.accessibility.menu_background_blurriness`. | none | P2.3 | 4 h | | both |
| P2.13 | Title screen | The logo (`title/minecraft.png`, `title/edition.png`), the rotating six-face panorama with `panorama_overlay`, the splash text from `texts/splashes.txt` (453 lines) in its yellow bobbing form, the buttons (Singleplayer disabled and labelled as such, Multiplayer, Realms removed, Options, Quit), the accessibility and language corner buttons, the copyright line, and `options.hideSplashTexts`. | none | P2.3, P2.1 | 2 d | | both |
| P2.14 | Server list | Read and write `servers.dat` in vanilla's own NBT format so the list is shared with the vanilla client. Rows showing name, address, MOTD, ping, player count, version and the icon, drawn with the 18 `sprites/server_list/` sprites. Reorder with the move arrows, Join, Edit, Delete, Refresh, Direct Connection, Add Server. | none | P2.9 | 2 d | **!** | both |
| P2.15 | Server status ping | The Status protocol: a handshake with next-state 1, `status_request`, read the JSON (version name and protocol, players online and max, sample, description, favicon as base64 PNG, `enforcesSecureChat`), then `ping_request`/`pong_response` for the round trip. Four packets Plasma has never spoken; `src/tools/mc_ping.cpp` is the start of it. | partial | - | 1 d | | both |
| P2.16 | Add/Edit server screen | Name, address, resource-pack policy (Enabled/Prompt/Disabled), Done, Cancel. Writes `servers.dat`. | none | P2.7, P2.14 | 4 h | | both |
| P2.17 | Direct connect screen | Address field with the last-used address remembered, Join, Cancel. | none | P2.7 | 2 h | ‖ | both |
| P2.18 | Connecting / loading screens | `connect.connecting`, `.authorizing`, `.encrypting`, `.negotiating`, `.joining`, `.reconfiguring`, `.transferring`, `multiplayer.downloadingTerrain` ("Loading terrain..."), the progress overlay, and Cancel. The 11 `connect.*` keys are the exact state list. | none | P2.3 | 1 d | | both |
| P2.19 | Disconnected screen | The reason as a rendered text component with its formatting, Back to Server List, Reconnect. The 41 `multiplayer.disconnect.*` keys are what the server may say; they are translation keys, so P2.19 needs the language file lookup Plasma already has. | none | P2.3 | 4 h | | both |
| P2.20 | Pause menu | `menu.paused`, Back to Game, Advancements, Statistics, Social Interactions, Options, Open to LAN (disabled on a server), Server Links, Quick Actions, Disconnect. Escape toggles. **The game must keep running behind it** - vanilla does not pause a multiplayer world. | none | P2.3 | 4 h | | both |
| P2.21 | Death screen | `deathScreen.title` / `.title.hardcore`, the death message built from the `damage_type` registry, `deathScreen.score.value`, Respawn, Spectate World (when the server allows), Title Screen with the `deathScreen.quit.confirm` confirmation. **Removes the auto-respawn divergence.** | none | P2.3, P1.3 | 4 h | **!** | both |
| P2.22 | Code of conduct screen | `multiplayer.codeOfConduct.title`, the text, the "do not notify again" checkbox, Accept, Decline. Replaces the auto-accept. | auto-accept | P2.8 | 3 h | | both |
| P2.23 | Confirm-command screen | `multiplayer.confirm_command.*`: five keys covering parse errors, missing permissions and signature requirements, with Run Command and Copy to Chat Screen. | none | P2.3 | 2 h | ‖ | both |
| P2.24 | Generic confirm and notice screens | The two-button confirm and the notice, used by the delete-server warning, the multiplayer warning, the symlink warning and the out-of-memory screen. | none | P2.3 | 2 h | ‖ | both |
| P2.25 | Narration hooks | 29 `narration.*` and 23 `narrator.*` keys. The framework records what each widget is for; the speech backend is P10.8. Putting the hooks in while the widgets are being written costs hours; retrofitting them later costs days. | none | P2.3 | 4 h | **!** | fast |

**Phase total: about 3 weeks.**

**Mechanism worth writing down - the GUI scale is not a zoom.** Vanilla does not
render the interface at native resolution and scale it, and it does not render at
a fixed design size and fit it. It computes an integer `s` and then every
coordinate in every screen is in units of `s` physical pixels, with all sprite
blits landing on exact multiples. That is why vanilla's 8-pixel hotbar is always
pixel-crisp at every window size, and why Plasma's is not. The consequence for
the code is bigger than the fix: every drawing call below this line takes GUI
units, not pixels, and the one conversion happens at the top. Doing this at P2.2
and not later is the difference between three hours and rewriting every screen.

**Mechanism worth writing down - `servers.dat` is shared ground.** It is an NBT
file in `~/.minecraft/servers.dat` with a list of compounds carrying `name`,
`ip`, `icon` (base64 PNG) and `acceptTextures`. Reading and writing vanilla's own
format rather than inventing one means a player's existing server list appears
in Plasma the first time they open it. For a client that has to be adopted, that
is worth the hour it costs over a private format.

---

## Phase 3 - the world is alive

**Goal.** Other players and every mob are visible, animated, skinned, named,
holding what they are holding and wearing what they are wearing. This is the
owner's complaint answered. It is also the largest phase in the document.

**Demonstrable at the end.** Stand on a busy server and see the people on it,
with their real skins, their capes, their names, swinging their swords, wearing
their armour, sneaking and swimming, and the mobs around them walking.

**Ordering note.** P3.1 to P3.16 are the engine. P3.17 to P3.27 are the player,
and they are the demonstrable midpoint - stop there, show it, then carry on.
P3.28 to P3.45 are animation and state. P3.46 onwards is the long list of
specific entities, which is genuinely parallel work: any of those rows can be
done in any order by anyone once the engine exists.

### 3a. The entity engine

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P3.1 | Entity store | Replace the `map<int, array<double,3>>` with a real entity: network id, uuid, type, position, previous position, velocity, yaw, pitch, head yaw, previous rotations, on-ground, the metadata bag, equipment, effects, passengers, vehicle, leash target. | positions only | P1.8 | 1 d | **!** | both |
| P3.2 | `teleport_entity` | Absolute position and rotation with the relative-flags bitfield. | none | P3.1 | 2 h | | both |
| P3.3 | `rotate_head` | Head yaw as a single angle byte, separate from body yaw. | none | P3.1 | 1 h | | both |
| P3.4 | `set_entity_motion` | Velocity in 1/8000 blocks per tick. Needed for knockback to look right before the next position arrives. | none | P3.1 | 1 h | | pvp |
| P3.5 | Position interpolation | Vanilla interpolates an entity's drawn position toward its last reported position over a number of "lerp steps" (3 for most entities since 1.21.2, 1 for the local player's vehicle). Without it every entity teleports twenty times a second. | none | P3.1, P1.21 | 1 d | **!** | both |
| P3.6 | Rotation interpolation | Yaw and pitch interpolate separately, through the shortest arc, with head yaw clamped to within a limit of body yaw and the body turning to follow at a maximum rate. Getting this wrong is what makes a mob's head spin. | none | P3.5 | 4 h | | both |
| P3.7 | `entity_event` | A single status byte per entity: hurt, death, taming success and failure, wolf shake, sheep eat, iron golem attack, villager mating hearts, firework explode, armour stand hit, totem of undying, 60-odd codes. Drives most one-shot visual feedback. | none | P3.1 | 4 h | | both |
| P3.8 | `damage_event` and `hurt_animation` | The damage type, source and the direction the hit came from. Drives the hurt flash, the camera tilt and the knockback direction. | none | P3.1, P1.3 | 3 h | | pvp |
| P3.9 | `set_passengers` and `set_entity_link` | Who is riding whom, and what is leashed to what. | none | P3.1 | 3 h | | both |
| P3.10 | Entity model format | Plasma's own binary format for a skeletal model: a tree of parts, each with a pivot, an initial rotation, a visibility flag and a list of boxes, each box having an origin, a size, an inflation and a UV origin. The runtime maps it and poses it. | none | - | 2 d | **!** | both |
| P3.11 | **Where the geometry comes from** | A decision, not code. Entity geometry **does not exist in the jar in any form** - `assets/minecraft/models/` contains only `block/` and `item/`; `models/entity/` has zero entries. All ~90 mob models, all 20-odd block-entity models and every animation are compiled Java. Appendix D sets out the two clean-room routes and recommends one. Decide before P3.12. | - | - | 1 d | **!** | both |
| P3.12 | The geometry pipeline | Whichever route P3.11 picks, turn its output into P3.10's format at install time, the same way `bake_block_table.py` works. | none | P3.11 | 3 d? | **!** | both |
| P3.13 | Entity texture atlas | 631 textures under `textures/entity/`, in sizes from 16x16 to 256x256. Not one array like the block atlas - entity textures are per-model and many are non-square - so an atlas with real UV rectangles, or a texture array with the largest size and per-entry scale. Baked by a new `bake_entities.py`. | none | - | 1 d | | both |
| P3.14 | Entity showcase tool | `plasma-client entityshot <type>...` rendering a given entity with no server, from the front, side, top and an angle. `docs/ARCHITECTURE.md` already records that the top-face bug was found by `showcase` and not by looking at landscapes; the same applies tenfold to a model with 20 parts. Build the tool before the models, not after. | none | P3.12 | 1 d | **!** | both |
| P3.15 | Entity render pipeline | A second Vulkan pipeline: per-part model matrix as a push constant or an SSBO index, real per-vertex normals, the two-directional-light shading vanilla uses, the overlay colour (hurt red and the white creeper flash) as a uniform, and the alpha mode per layer. | none | P3.10 | 2 d | **!** | both |
| P3.16 | Entity batching and culling | One draw per model per entity is fine at 20 entities and not at 200. Instance per part, cull against the frustum by the entity's own bounding box from P1.20, and honour `options.entityDistanceScaling`. | none | P3.15 | 1 d | | fast |

### 3b. The player

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P3.17 | The humanoid model | Six boxes - head 8x8x8, body 8x12x4, two arms 4x12x4, two legs 4x12x4 - with the arms pivoting at the shoulder and the legs at the hip. The single most important model in the game. | none | P3.10, P3.12 | 1 d | **!** | both |
| P3.18 | The hat layer | A seventh box, the head inflated by 0.5, drawn from the skin's hat region in a second pass with alpha. | none | P3.17 | 2 h | | both |
| P3.19 | The second skin layer | Jacket, two sleeves, two trouser legs: five more boxes, each the corresponding part inflated by 0.25, from the lower half of a 64x64 skin. A skin without them is a 64x32 legacy skin. | none | P3.17 | 4 h | | both |
| P3.20 | Slim and classic arms | The profile's `model` field is `"slim"` or absent. Slim arms are 3 wide instead of 4 and sit 0.5 lower, with different UVs. Two model variants, not a scale. | none | P3.17 | 3 h | | both |
| P3.21 | Skin fetch | The `textures` property on a player in `player_info_update` is base64 JSON holding a `SKIN` url and optionally a `CAPE` url on `textures.minecraft.net`. Fetch over HTTPS, decode the PNG, upload. **Horizon's own known-issues file admits their https skins silently fall back to the default** - so test this against a real server with a real skin before calling it done. | none | P1.18 | 1 d | **!** | both |
| P3.22 | Skin cache | On disk by url hash, with the texture kept across sessions. 200 players on a lobby is 200 HTTPS requests otherwise. | none | P3.21 | 4 h | | fast |
| P3.23 | Legacy skin conversion | A 64x32 skin has no left arm, no left leg and no second layer. Vanilla mirrors the right limbs into the left and synthesises the missing regions. Also strips the alpha from the opaque regions, because old skins have garbage there. | none | P3.21 | 3 h | | both |
| P3.24 | Default skins | A player with no skin gets one of six defaults chosen by a hash of their uuid, in both arm widths. Those textures are in the jar at `textures/entity/player/wide/` and `slim/`. | none | P3.17 | 2 h | | both |
| P3.25 | Cape | A single 10x16x1 box behind the body, from the cape texture, swinging with movement and tilting as the player moves and looks. | none | P3.21 | 4 h | | both |
| P3.26 | Skin part toggles | A player's own `client_information` byte says which of hat, cape, jacket, left and right sleeve, left and right trouser leg to draw. It travels to other clients through the player list's hat action and through metadata. 7 `options.modelPart.*` keys. | partial (hardcoded 0x7F) | P3.19 | 2 h | | both |
| P3.27 | Nametag | The custom name or the profile name, drawn as a billboard above the entity at its height plus 0.5, in GUI-scale text, on a dark quad at `options.accessibility.text_background_opacity`, **depth-tested when the entity is within range and drawn through walls beyond it** - that asymmetry is deliberate in vanilla and is why nametags are visible across a field but not through the wall next to you. Hidden when the entity is sneaking, or further than 32 blocks, or when its metadata says not to show it. | none | P3.1, P2.2 | 1 d | **!** | both |

### 3c. Animation and state

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P3.28 | Limb swing | Two accumulators per entity: how far it has walked, and how hard. The legs and arms swing as a cosine of the first, scaled by the second, which eases in and out when the entity starts and stops. Every walking mob uses this. | none | P3.17, P1.21 | 4 h | **!** | both |
| P3.29 | Head pitch and yaw | The head part takes the entity's pitch and the difference between head yaw and body yaw. | none | P3.6, P3.17 | 1 h | | both |
| P3.30 | Arm swing | The attack animation: a 6-tick counter, triggered by `animate` with the swing-arm action, moving the arm through a sine arc with the body twisting slightly. Applies to both hands. | none | P3.17 | 4 h | | pvp |
| P3.31 | `animate` packet | The animation packet: swing main hand, swing off hand, leave bed, critical effect, magic critical effect. | none | P3.30 | 1 h | | pvp |
| P3.32 | Idle motion | The slight arm and head drift vanilla applies when nothing is happening, which is most of why a standing player does not look frozen. | none | P3.28 | 2 h | | both |
| P3.33 | Sneaking pose | The body rotates forward about 0.5 radians, the head rises to compensate, the arms move with it, and the hitbox drops from 1.8 to 1.5. Comes from the metadata pose field, not from a flag. | none | P1.8, P3.17 | 3 h | | pvp |
| P3.34 | Swimming and crawling pose | The whole model rotates to horizontal about the body's centre, interpolated in over a few ticks, with the limbs paddling. Same pose field. | none | P3.33 | 4 h | | both |
| P3.35 | Elytra pose | Arms back, legs together, body pitched, plus the elytra model itself, whose texture is the player's cape if they have one. | none | P3.33, P3.25 | 4 h | | pvp |
| P3.36 | Sleeping pose | Horizontal, oriented by the bed's facing, offset to lie on it. | none | P3.33 | 2 h | | fast |
| P3.37 | Riding pose | Legs spread and raised, by an amount the vehicle decides, and the whole entity positioned at the vehicle's passenger offset. | none | P3.9, P3.33 | 4 h | | both |
| P3.38 | Death animation | The entity rotates 90 degrees about its forward axis over 20 ticks and then disappears. | none | P3.7 | 2 h | | both |
| P3.39 | Hurt flash | A red overlay colour over the whole model for 10 ticks, from `entity_event` or `damage_event`. The overlay is a multiply-and-add in the shader, not a tint. | none | P3.7, P3.15 | 2 h | | pvp |
| P3.40 | `set_equipment` | Six slots: main hand, off hand, helmet, chest, legs, feet, with the top bit of the slot byte marking "more follow". Needs the component codec to read the stacks. | none | P1.5, P3.1 | 3 h | **!** | both |
| P3.41 | Armour layers | Two layer models per armour material - the outer (helmet, chest, boots) and the inner (leggings) - as inflated copies of the humanoid parts. `assets/minecraft/equipment/` is 44 JSON files and is **the one place Mojang replaced hardcoding with parseable data**, so the layer-to-texture mapping is readable rather than guessed. | none | P3.40, P3.17 | 1 d | | pvp |
| P3.42 | Dyed leather tint | Leather armour multiplies its layer texture by the `dyed_color` component, with the overlay layer left untinted. | none | P3.41, P1.5 | 2 h | | both |
| P3.43 | Armour trims | The `trims` atlas declares `paletted_permutations`: 36 trim textures crossed with 16 palettes, generating 576 sprites, by remapping a greyscale ramp through a palette image. Generated at bake time. 18 `trim_pattern.*` and 11 `trim_material.*` language keys. | none | P3.41 | 1 d | | both |
| P3.44 | Held item on an entity | The item in each hand, rendered at the hand's transform - the model turned and offset differently for a block, a flat item, a rod, a bow, a shield and a spyglass. This is the `display_context` property of the item model selector. | none | P3.40, P4.11 | 1 d | | pvp |
| P3.45 | Enchantment glint | A second pass over the same geometry with the scrolling `misc/enchanted_glint_item.png` (or `_entity`) sampled in screen or model space, blended additively, speed and strength from `options.glintSpeed` and `options.glintStrength`, and suppressed by the `enchantment_glint_override` component. | none | P1.5, P3.15 | 1 d | | both |
| P3.46 | Entity shadow | The soft ellipse under every entity: `misc/shadow.png` projected down onto the blocks below, sized by the entity's width, faded by height and by light. An `options.entityShadows` toggle. Cheap, and its absence is one of the reasons entities look pasted on even once they are drawn. | none | P3.15 | 1 d | | both |
| P3.47 | Glowing outline | The `entity_outline` post chain: the entity drawn to a second target, blurred, and composited as a team-coloured silhouette. Driven by the glowing metadata flag and by spectator highlighting. | none | P3.15 | 1 d | | both |
| P3.48 | Invisibility | An invisible entity draws nothing except its armour and held items, and is drawn translucently to spectators. | none | P1.8 | 2 h | | pvp |

### 3d. Every entity that has to be drawn

157 types. Grouped by the work rather than by the mob, because one model family
covers many entities and the per-entity cost after the family exists is small.
Every row here is parallel-safe once 3a to 3c are done.

| # | Item | Covers | Count | Size | Who |
|---|---|---|---|---|---|
| P3.49 | Humanoid mobs on the player model | zombie, husk, drowned, parched, zombie_villager, skeleton, stray, bogged, wither_skeleton, piglin, piglin_brute, zombified_piglin, giant, mannequin | 14 | 2 d | both |
| P3.50 | Illagers | evoker, illusioner, pillager, vindicator, witch (own model), evoker_fangs | 6 | 1 d | both |
| P3.51 | Villagers and traders | villager, zombie_villager overlay, wandering_trader. **14 `.mcmeta` files carry `villager: {hat: full\|partial}`** - a three-layer composite of body, type and profession where the profession's hat flag decides whether the type's hat is hidden. 15 professions x 7 types. | 3 + 105 textures | 2 d | both |
| P3.52 | Quadrupeds | cow, mooshroom, pig, sheep, wolf, ocelot, cat, horse, donkey, mule, skeleton_horse, zombie_horse, llama, trader_llama, camel, camel_husk, polar_bear, panda, fox, goat, armadillo, sniffer, strider, hoglin, zoglin, rabbit, chicken, bee, turtle, axolotl, frog, tadpole, allay, vex, creaking, copper_golem | 36 | 1 w | both |
| P3.53 | Variant layers | sheep wool and its 16 colours and shearing, wolf variants from the registry and the collar, cat variants and collar, horse markings and armour, llama decor, mooshroom type, panda gene, fox type, rabbit type, tropical_fish 22 patterns x 15 colours, parrot 5 colours, frog, axolotl, pig, cow, chicken variants from the registry | - | 3 d | both |
| P3.54 | Fish and aquatics | cod, salmon, tropical_fish, pufferfish and its three puff states, squid, glow_squid, dolphin, nautilus, zombie_nautilus, guardian, elder_guardian and the guardian beam | 11 | 2 d | both |
| P3.55 | Flyers | bat, parrot, phantom, blaze, ghast, happy_ghast, bee wings, allay wings | 8 | 2 d | both |
| P3.56 | Slimes and cubes | slime, magma_cube, and the nested inner cube with its size from metadata | 2 | 4 h | both |
| P3.57 | Creeper | The model plus the swell: a **non-uniform scale** applied after the pose, which is the one case where the normals cannot be taken from the pose matrix and have to be recomputed. | 1 | 4 h | both |
| P3.58 | Enderman and endermite | Plus the eyes overlay layer and the carried block rendered in its hands. | 2 | 4 h | both |
| P3.59 | Spiders, silverfish, cave spider | Plus the eyes overlay. | 3 | 4 h | both |
| P3.60 | Golems | iron_golem, snow_golem and its pumpkin-less variant. | 2 | 4 h | both |
| P3.61 | Shulker | The base and the lid, attached to a face, peeking by an amount from metadata, in 17 colours. | 1 | 4 h | both |
| P3.62 | Keyframe animation runtime | 1.19 onwards, some mobs are animated by a **declarative keyframe system** rather than by code: a list of bone channels with timestamps, a value per keyframe and an interpolation mode. Needed by warden, breeze, camel, armadillo, sniffer, creaking, copper_golem, happy_ghast, bogged and the new 1.21.11 mobs. The animation data is in the same place the geometry is - see P3.11 - and is not in the jar as data. | - | 2 d | both |
| P3.63 | Warden | Multi-stage model with keyframe animations for emerge, dig, roar, sonic boom, and the heart glow layer. | 1 | 1 d | both |
| P3.64 | Breeze | Keyframe animated, with its wind-charge projectile and the gust particles. | 1 | 4 h | both |
| P3.65 | Ravager and bosses | ravager, wither with three heads and its armour phase, ender_dragon as **eight linked body parts with their own hitboxes**, and the dragon's death animation. | 3 | 3 d | both |
| P3.66 | Boats and rafts | 11 wood types x boat and chest boat, plus bamboo raft and chest raft. One model, 22 textures, plus the paddle animation from `paddle_boat` and the water-level bob. | 24 | 1 d | both |
| P3.67 | Minecarts | minecart, chest, furnace, hopper, tnt, spawner, command_block. The block inside is rendered as a block model. Plus `move_minecart_along_track`, which is the new per-tick position stream. | 7 | 1 d | both |
| P3.68 | Item entity | The dropped item: the model billboarded or 3D depending on the item, **bobbing on a sine and spinning**, and rendered 2 to 4 times offset when the stack is large. The spin and bob are seeded from the entity id so a pile does not pulse in unison. | 1 | 4 h | both |
| P3.69 | Experience orb | A 6-frame animated sprite, one of 11 size tiers by value, bobbing, with the magnet-to-player motion coming from the server. | 1 | 3 h | both |
| P3.70 | Projectiles | arrow, spectral_arrow, trident, snowball, egg, ender_pearl, eye_of_ender, experience_bottle, splash_potion, lingering_potion, fireball, small_fireball, dragon_fireball, wither_skull, shulker_bullet, llama_spit, wind_charge, breeze_wind_charge, firework_rocket, fishing_bobber with its line | 20 | 2 d | pvp |
| P3.71 | Arrows stuck in a body | The arrow-count metadata field, drawn as that many arrows at pseudo-random positions over the model, seeded by entity id. | - | 3 h | pvp |
| P3.72 | Static world entities | painting (102 variants, size from the registry), item_frame, glow_item_frame, armor_stand, end_crystal with its beam, leash_knot, lightning_bolt, falling_block, tnt with its near-detonation white flash, area_effect_cloud, ominous_item_spawner | 11 | 2 d | both |
| P3.73 | Armour stand | The model plus its four metadata flags (small, has arms, no base plate, marker) and the per-part Euler poses from metadata, which is what makes posed stands work. | 1 | 4 h | both |
| P3.74 | Display entities | block_display, item_display, text_display: no model of their own, driven entirely by metadata - a transform (translation, left rotation quaternion, scale, right rotation quaternion), a billboard mode, a view range, a shadow radius and strength, a glow override, and then either a block state, an item stack with a display context, or a text component with a background colour and alignment. Servers use these for everything now, so the absence is very visible on a modern server. | 3 | 2 d | both |
| P3.75 | Invisible markers | interaction with its width, height and response flag; marker. Nothing drawn, but the interaction entity must be hit-testable because servers use it for clickable regions. | 2 | 2 h | both |
| P3.76 | `take_item_entity` | The pickup animation: the item entity flies to the collector over a few ticks and vanishes. | - | 2 h | both |
| P3.77 | Mannequin and the 1.21.11 additions | mannequin, parched, camel_husk, nautilus, zombie_nautilus, copper_golem. New in this version and easy to miss from a list written from memory; the registry put them there. | 6 | 1 d | both |

**Phase total: about 11 weeks**, of which 3a-3c is 5 and 3d is 6. 3d is the
parallel part: with three people it is two weeks.

**Mechanism worth writing down - head yaw is a separate field for a reason.**
An entity has a body yaw and a head yaw, and they arrive in different packets:
body yaw in `teleport_entity` and the `move_entity_*_rot` family, head yaw in
`rotate_head`. The head follows the look direction immediately; the body turns
to catch up at a limited rate, and only while the entity is moving. Treating
them as one value gives a mob whose whole body snaps around when it glances at
you, which reads as broken even to someone who cannot say what is wrong. The
clamp - head yaw is held within a limit of body yaw, and exceeding it drags the
body - is what produces the characteristic look of a Minecraft mob tracking you.

**Mechanism worth writing down - the nametag's depth behaviour is deliberate.**
Vanilla draws a nametag with the depth test *enabled* when the entity is close
and *disabled* when it is far, with the far version also drawing a translucent
background. The effect is that names do not poke through the wall in front of
you, but a teammate across a field is still findable. It is one `if` and it is
the difference between a nametag that looks right and one that looks like a
cheat client's.

**Mechanism worth writing down - the creeper is where pose normals break down.**
Every other entity's normals can be taken straight from the part's pose matrix,
because every other transform is a rotation and a translation. The creeper's
swell is a non-uniform scale, which does not preserve normals - you need the
inverse transpose, or the lighting on a swelling creeper goes wrong in a way
that is very hard to attribute later. Horizon's own parity harness documents
hitting exactly this and having to treat the posed normals as untrusted for that
renderer. Worth knowing before you spend an afternoon on it.

---

## Phase 4 - hands and hitting things

**Goal.** The client has a body you can see and the world reacts when you touch
it. Second-biggest answer to the pasted-in complaint, and much cheaper than
Phase 3.

**Demonstrable at the end.** You can see your own hand holding your own sword,
it swings when you click, the block you are mining cracks across its face over
the right length of time for that block and that tool, and it bursts into
particles when it breaks.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P4.1 | First-person view space | A second render pass with its own projection - a narrower FOV than the world's, drawn after everything, depth-cleared so the hand never clips into a wall. | none | - | 4 h | **!** | both |
| P4.2 | The arm model | The player's own arm, from the player's own skin, at the fixed first-person transform. Includes the empty-handed version, which is what you see most of the time. | none | P4.1, P3.17 | 4 h | **!** | both |
| P4.3 | Held item in first person | The item at the hand transform for the main and off hand, with the off-hand version mirrored. Positions differ per item class - a block, a flat sprite, a tool, a bow being drawn, a shield being raised, a spyglass, a map held in two hands. | none | P4.2, P4.11 | 1 d | **!** | both |
| P4.4 | Hand animations | Four curves: the equip animation when the item changes (down and back up over 4 ticks), the swing arc (6 ticks), the use animation (eating and drinking shake, bow pull, shield raise, crossbow load), and the walking bob, which is a different curve from the camera's. | none | P4.3, P1.21 | 1 d | **!** | both |
| P4.5 | Real face and cursor position | The raycast knows which face it entered and where on that face; `send_dig` and `send_place` currently hardcode face 1 and cursor (0.5, 1.0, 0.5). Placement against a wall is wrong today, and so is placing a slab's half or a stair's facing. | wrong | - | 3 h | **!** | both |
| P4.6 | Real dig timer | Destroy time = hardness, scaled by the tool's speed on that material, by Efficiency, by Haste and Conduit Power, by Mining Fatigue, by whether you are in water without Aqua Affinity, and by whether you are off the ground. Progress accumulates per tick and resets if you look away. The flat 0.25 s goes. | wrong | P1.14, P1.10, P1.12 | 1 d | **!** | both |
| P4.7 | The crack overlay | Ten textures `block/destroy_stage_0..9`, drawn over the block being mined as a decal on each of its faces in a separate pass, with the stage from the dig progress. This is the game's core feedback loop and it is currently absent. | none | P4.6 | 1 d | **!** | both |
| P4.8 | `block_destruction` | Other players' mining progress: an entity id, a position and a stage. Same overlay, driven by the server. | ignored | P4.7 | 2 h | | both |
| P4.9 | Attack cooldown and the indicator | The attack-speed attribute gives a cooldown; vanilla draws it as either a crosshair ring (`crosshair_attack_indicator_*`, three sprites) or a hotbar bar (`hotbar_attack_indicator_*`, two sprites), per `options.attackIndicator`. Central to how 1.9+ PvP reads. | none | P1.10 | 4 h | | pvp |
| P4.10 | `interact` packet | Attacking and interacting with an *entity*, which Plasma cannot do at all: the entity raycast against the P1.20 bounding boxes within the `entity_interaction_range` attribute, then interact, interact-at or attack, with the sneaking flag. **Without this the client cannot hit anything that moves.** | none | P1.20, P1.10 | 1 d | **!** | pvp |
| P4.11 | **Item model selector** | `assets/minecraft/items/` is 1505 JSON files and is a layer Plasma does not have at all. Each file picks a model by type: `model`, `composite`, `condition`, `select`, `range_dispatch`, `dye`, `special`, driven by 16 runtime properties - `display_context`, `trim_material`, `block_state`, `using_item`, `bundle/has_selected_item`, `compass`, `local_time`, `crossbow/pull`, `fishing_rod/cast`, `custom_model_data`, `damage`, `count`, `keybind_down`, `selected`, `view_entity`, `charge_type`. This replaces the pre-rendered cube icons. | none | P1.5, P1.6 | **1 w** | **!** | both |
| P4.12 | Live item rendering | Render an item model, not a picture of one: the `gui` display context's orthographic projection with its fixed rotation, two directional lights, into the slot or the hand. The 419 baked cube icons go away. **This is pasted-in picture number one.** | none | P4.11 | 2 d | **!** | both |
| P4.13 | Code-only item models | `items/*.json` names 11 items whose model is `minecraft:special`, meaning vanilla hand-builds it: chest, bed, banner, shulker_box, copper_golem_statue, head, player_head, trident, shield, conduit, decorated_pot. **This is Mojang's own list of models a client has to hand-build just for item rendering**, and it overlaps exactly with the block-entity models in Phase 6, so build them once and use them twice. | none | P4.12, P3.12 | 2 d | | both |
| P4.14 | Particle engine | A particle pool with position, velocity, age, lifetime, size, colour and a sprite, gravity, drag, block collision, and the five render types (billboard, terrain-sheet, lit, unlit, and the "sheet" types that face the camera versus the ones that do not). | none | P1.21 | 2 d | **!** | both |
| P4.15 | Particle sprite atlas | `assets/minecraft/particles/` is 104 JSON files, each just a list of texture frames. 255 textures under `textures/particle/`. Baked. | none | P4.14 | 4 h | | both |
| P4.16 | Block-breaking particles | The burst when a block breaks: 4x4x4 particles per block, each taking a 4x4 crop of a random spot on the block's own texture. The single most-seen particle in the game. | none | P4.14, P4.15 | 4 h | **!** | both |
| P4.17 | Block-hitting particles | The small spray while you are mining, from the face you are hitting. | none | P4.16 | 2 h | | both |
| P4.18 | Landing and running particles | The puff when you land hard, and the ones that trail from your feet when you sprint - again cropped from the block you are standing on. | none | P4.16 | 3 h | | both |
| P4.19 | `level_particles` | The server asking for particles: a particle type with its own type-specific extra data, a position, an offset, a max speed, a count, a "long distance" flag and the per-type payload (block state for `block`, item stack for `item`, colour for `dust`, two colours for `dust_color_transition`, a position source for `vibration`, a target for `trail`). 115 particle types. | ignored | P4.14, P1.5 | 2 d | **!** | both |
| P4.20 | `level_event` | 70-odd one-shot world events, each a sound and usually a particle: door open and close, chest open, dispenser fire, firework, fizz, record play, ghast warn, zombie attack door, anvil break, wither spawn, portal travel, end gateway, dragon breath, brewing stand brew, bubble column whirlpool, electric spark, composter fill, lava extinguish, redstone torch burnout, pointed dripstone drip, trial spawner detect, vault activate, copper golem. | ignored | P4.14 | 1 d | | both |
| P4.21 | `explode` | The explosion: the particle emitter, the sound, the knockback applied to the player, and the small-versus-large form. | ignored | P4.19 | 4 h | | pvp |
| P4.22 | `block_event` | The block-entity action packet: chest opening and closing (the lid angle), note block pitch, piston extend and retract, mob spawner reset, end gateway cooldown, bell ring, decorated pot wobble. Drives the animations in Phase 6. | ignored | - | 3 h | | both |
| P4.23 | Hurt camera tilt | The camera rolls toward the damage direction over 10 ticks, scaled by `options.damageTiltStrength`. One of the oldest and most recognisable bits of feedback in the game. | none | P3.8 | 2 h | | pvp |
| P4.24 | Screen overlays | The red vignette on low health, the pumpkin blur, the powder-snow freeze, the frozen heart sprites, the Nausea warp, the Darkness pulse scaled by `options.darknessEffectScale`, the fire overlay when you burn, the water overlay and its fog, the suffocation vignette, the spyglass overlay, and the portal distortion. | none | P1.12 | 2 d | | both |
| P4.25 | `player_command` | Start and stop sprinting, start and stop sneaking, leave bed, start and stop horse jump, open horse inventory, start and stop elytra flight. The server needs to be told; right now it is not, which is why a sprinting Plasma player does not look like one to anyone else. | none | - | 3 h | **!** | pvp |
| P4.26 | `player_input` | The raw input bitfield (forward, back, left, right, jump, sneak, sprint), sent every tick since 1.21.2. The server uses it for vehicles and for anti-cheat; a client that does not send it is conspicuous. | none | - | 2 h | **!** | pvp |
| P4.27 | `move_player_rot` and `move_player_status_only` | Vanilla sends the cheapest of four movement packets depending on what actually changed. Plasma always sends position-and-rotation, which is more bytes than vanilla on most ticks. | partial | - | 2 h | ‖ | pvp |
| P4.28 | `swing` for the off hand | Currently always hand 0. | partial | - | 30 m | ‖ | pvp |
| P4.29 | `use_item` | Right-click with nothing targeted: eating, drinking, drawing a bow, throwing, raising a shield, with the use-duration tracking and the release. | none | P1.5 | 4 h | **!** | pvp |
| P4.30 | `pick_item_from_block` / `pick_item_from_entity` | Middle click. | none | P4.10 | 2 h | ‖ | both |
| P4.31 | Full player physics | What the current tick does not do: swimming and the swim sprint, water and lava drag, climbing ladders and vines, scaffolding, powder snow, honey block slowdown and wall-sliding, slime bounce, soul speed, ice friction, cobweb, sweet berry slowdown, bubble columns, the step-up over a 0.6 block, fall damage, the sprint-jump boost, the elytra flight model and firework boost, riding, auto-jump, and the sneak-at-an-edge check. | AABB walk and jump only | P1.9, P1.10, P1.13 | **2 w** | **!** | pvp |

**Phase total: about 7 weeks.**

**Mechanism worth writing down - the item model selector is a tree, not a
lookup.** Before 1.21.4 an item had one model. Now `items/<item>.json` is a
decision tree evaluated every frame against the stack and its context. A
crossbow picks a model from `crossbow/pull` (a float) crossed with
`charge_type`; a compass rotates from `compass`; a bundle changes from
`bundle/has_selected_item`; a clock reads `local_time`; trimmed armour selects on
`trim_material`. So the slot cannot hold a cached picture even in principle -
the model depends on state that changes while you look at it. That is the deep
reason the baked-icon approach cannot be fixed by baking more icons, and it is
why P4.11 is a week rather than a day.

**Mechanism worth writing down - the dig timer is the most-felt number in the
game.** Destroy progress per tick is `speed / hardness / (30 if the tool is
correct else 100)`, and progress reaching 1 is the break. `speed` starts at 1,
is multiplied by the tool's material speed when the tool is correct for the
block, gets `level² + 1` from Efficiency, `×(0.2·level + 1)` from Haste, is
divided by 5 per Mining Fatigue level, divided by 5 again if you are in water
without Aqua Affinity, and divided by 5 again if you are not on the ground.
Those multipliers are public and the hardness table is public, so this is a data
problem rather than a reverse-engineering problem - but every one of those
factors is noticeable to a player who knows the game, which is why P1.14 is worth
a full day of careful table entry rather than a plausible approximation.

---

## Phase 5 - sound

**Goal.** The client makes noise. Fully independent of every other phase - it can
run in parallel with Phase 3 or Phase 4 at any time, and it is the cheapest
remaining item per unit of "this feels like a game".

**Demonstrable at the end.** Footsteps on the block you are actually standing on,
your own tools, the mobs around you, the music, and closed captions.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P5.1 | `sounds.json` reader | 1838 events, each with a list of weighted entries, each entry naming an ogg or referencing another event, with volume, pitch, attenuation distance, stream and preload flags. **The file is not in the jar** - it is at the virtual path `minecraft/sounds.json` in the object store. | none | P2.1 | 4 h | **!** | both |
| P5.2 | Ogg Vorbis decoding | 4,405 ogg files. `stb_vorbis.c` is a single public-domain header and is the right answer; it decodes to interleaved shorts and that is all that is needed. | none | - | 4 h | | both |
| P5.3 | Audio backend | An output device, a mix thread, a ring buffer. Linux means ALSA or PipeWire directly, or PortAudio or miniaudio for one file. Honest recommendation: miniaudio, one header, works on all three platforms, no OpenAL dependency. | none | - | 1 d | **!** | both |
| P5.4 | The 3D mixer | Per-voice gain, pitch, position, looping, and the listener's position and orientation. Vanilla attenuates linearly to a default 16 blocks, scaled by the event's attenuation distance, and a volume above 1 extends the audible range rather than making it louder nearby. Panning is simple stereo from the listener's right vector. | none | P5.3 | 2 d | **!** | both |
| P5.5 | Sound categories | 11 categories (`soundCategory.*`): master, music, record, weather, block, hostile, neutral, player, ambient, voice, ui. Each has its own volume in the options and they multiply. | none | P5.4 | 2 h | | both |
| P5.6 | Voice pool and priority | Vanilla caps concurrent sounds per category and drops the quietest when full. Without a cap, a TNT chain or a lot of mobs overruns the mixer. | none | P5.4 | 4 h | | fast |
| P5.7 | `sound` packet | A sound event by registry id or by name, with a category, a position in 1/8 blocks, volume, pitch and a seed. The seed picks which weighted entry plays, and it has to be the server's seed or two clients hear different variants. | ignored | P5.4 | 3 h | **!** | both |
| P5.8 | `sound_entity` | The same, attached to an entity so it follows it. | ignored | P5.7 | 2 h | | both |
| P5.9 | `stop_sound` | Stop by source category, by name, or everything. | ignored | P5.7 | 1 h | | both |
| P5.10 | Footsteps and block sounds | Not from a packet: the client plays these itself from the block's sound group. **The sound group is not in any report** - see Appendix D. A hand table of about 60 sound groups mapped over the 1166 blocks: stone, wood, gravel, grass, metal, glass, wool, sand, snow, ladder, anvil, slime, honey, netherrack, nylium, bamboo, azalea, moss, deepslate, tuff, copper, amethyst, candle, cave vines, powder snow, sculk, cherry wood, pale moss, resin, and the rest. Covers stepping, breaking, placing, hitting and falling onto every block. | none | P5.7, P1.13 | 1 d | **!** | both |
| P5.11 | UI sounds | The 10 `ui.*` events: button click, the loom and stonecutter takes, the cartography table take, the hud bubble pop, the toast in and out. Wired into P2.4's button. | none | P5.7 | 2 h | | both |
| P5.12 | Music | 31 `music.*` events. Vanilla's rule: a delay between tracks that varies by situation (menu, creative, game, each biome's own music, the nether's, the end's, the credits', and the "boss" override near the dragon), with `options.music_frequency` and the "now playing" toast. | none | P5.7 | 1 d | | fast |
| P5.13 | Ambient sound | 22 `ambient.*` events: the cave sounds, the nether loops, the underwater loops and the basalt-delta wind, placed by vanilla's own rule - a random dark air block near the player with no sky access. | none | P5.7 | 1 d | | both |
| P5.14 | Weather sound | The 3 `weather.*` events: rain above you, rain near you, thunder. | none | P5.7, P6.11 | 3 h | | both |
| P5.15 | Closed captions | 1,740 of the 1838 events carry a `subtitle` key. `options.showSubtitles` draws the most recent few bottom-right, with a direction arrow and fading, in a stack that merges repeats into a count. | none | P5.7, P2.2 | 1 d | | fast |
| P5.16 | Jukebox and note blocks | 21 `music_disc.*` events and the 8 `instrument.*` note-block instruments, which are pitched by note rather than played at pitch 1. | none | P5.7 | 4 h | | both |
| P5.17 | Attenuation and directional audio | `options.directionalAudio` turns on HRTF, which miniaudio and OpenAL both provide. Without it, vanilla's plain stereo panning. | none | P5.4 | 4 h | ‖ | fast |
| P5.18 | Audio device selection | `options.audioDevice`, enumerating devices and handling one disappearing. | none | P5.3 | 3 h | ‖ | fast |

**Phase total: about 2.5 weeks.**

**Mechanism worth writing down - the seed is load-bearing.** A `sound` packet
carries a seed, and the client uses it to pick which of the weighted entries in
that event plays. Ignore it and generate your own random number, and two players
standing next to each other hear different variants of the same anvil hit - which
nobody will ever report as a bug but which quietly makes the world feel less
solid. It is one field.

---

## Phase 6 - block entities and the rest of the world

**Goal.** The 2,881 block states that render as nothing, the systems that make
the world move, and the two other dimensions.

`docs/ARCHITECTURE.md` already identified these 2,881 and correctly said they are
not a bug: their models hold nothing but a particle texture because vanilla draws
them with a renderer of its own. This is that renderer.

**Demonstrable at the end.** A base with chests that open, signs you can read,
beds, banners, item frames and a nether portal, under a sky that moves through a
day and rains on you.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P6.1 | `block_entity_data` and chunk block entities | The chunk packet already carries a block-entity list and it is read past. 49 block-entity types with their own NBT. | ignored | P1.5 | 4 h | **!** | both |
| P6.2 | Block-entity render path | A third pipeline: models from P3.10 placed at a block position, with the block's own light, drawn after the terrain and before the translucent pass. | none | P3.15, P6.1 | 1 d | **!** | both |
| P6.3 | Chests | Single, double (left and right halves), trapped, ender, copper in its four oxidation stages, plus the Christmas variant. The lid angle animates from `block_event`, and a double chest's two halves animate together. | none | P6.2, P4.22 | 1 d | | both |
| P6.4 | Signs and hanging signs | 11 wood types x standing, wall, hanging and wall-hanging. Four lines of text per side, two sides, each line a text component with its own colour, optionally glowing (which draws it bright and outlined), the text scaled to fit, and `is_waxed`. 12 `gui/hanging_signs/` textures for the editing screen. | none | P6.2, P1.5 | 1 d | | both |
| P6.5 | Beds | 16 colours, head and foot halves, the four legs, and the player lying on it. | none | P6.2 | 4 h | | both |
| P6.6 | Banners and shields | The base colour plus up to 6 patterns, each pattern a texture from the `banner_patterns` atlas tinted by a dye colour, composited in order. The shield uses the same data on a different model. Also the 24 `decorated_pot_pattern` registry entries on the same principle. | none | P6.2, P1.5 | 1 d | | both |
| P6.7 | Shulker boxes and heads | 17 shulker box colours on the mob's own model; 6 head types plus `player_head` with its skin fetched from the `profile` component. | none | P6.2, P3.21 | 1 d | | both |
| P6.8 | The rest of the block entities | conduit (its two animated states), bell, enchanting table's book with its page animation and its look-at-the-player behaviour, mob spawner's spinning miniature mob, piston and its moving blocks, lectern's book, campfire's items, chiseled bookshelf, shelf, decorated pot, crafter, trial spawner and its ominous state, vault, brushable block, copper golem statue, end portal and end gateway's starfield, creaking heart, sculk sensor and shrieker, jigsaw, structure block, command block, daylight detector, comparator, beehive, barrel, jukebox, hopper, dispenser, dropper, furnace family, brewing stand, beacon and its beam. | none | P6.2 | **1 w** | | both |
| P6.9 | Beacon and end-crystal beams | The vertical beacon beam with its segment colours, and the end crystal's beam to the dragon. Both are a textured billboard quad pair with a scrolling UV. | none | P6.8 | 4 h | | both |
| P6.10 | Day and night | `set_time` gives the time of day. From it: the sun and moon positions, the **8 moon phases** - which are now 8 separate PNGs under `textures/environment/celestial/moon/` rather than a sheet, declared by the new `celestials.json` atlas - the star field with its own fade, the sky colour gradient through dawn and dusk, the light level, and the fog colour. | none | P1.17 | 2 d | **!** | both |
| P6.11 | Weather | Rain and snow as the camera-relative column of textured quads vanilla uses, chosen per biome by temperature, with the rain not falling where there is a roof, plus the `rainLevel` and `thunderLevel` lerps from `game_event` 7 and 8, the lightning flash with `options.hideLightningFlashes`, and the darkened sky. | none | P1.16 | 2 d | | both |
| P6.12 | The nether | Its dimension type: no skylight, a ceiling, ambient light 0.1, a fixed fog colour with no sun or moon or stars, and the correct 0..256 height. | none | P1.2, P0.5 | 4 h | **!** | both |
| P6.13 | The end | Its own sky texture (`end_sky.png` as a tiled cube), no sun or moon, the void, and the dragon fight's boss bar. | none | P1.2 | 4 h | | both |
| P6.14 | Animated textures | **62 textures declare an `animation`** in `.png.mcmeta` and the baker takes frame 0 of each. 54 blocks, 5 GUI, 2 entity, 1 particle; 15 use `interpolate: true` and 11 have explicit frame lists with per-frame times. The fix is a second atlas dimension plus a per-frame index the shader reads, updated on the tick clock. Still water, still lava and still fire are a strong uncanny signal. | wrong | P1.21 | 1 d | **!** | both |
| P6.15 | Water and swimming visuals | The underwater fog colour and density from the biome, the water overlay texture, the bubble stream, the drowning air bar (`hud/air`, `air_bursting`, `air_empty`), and the swim camera. | none | P4.31 | 1 d | | both |
| P6.16 | Lava and fire visuals | The lava fog, the fire overlay on the camera when burning, and the fire quads drawn on a burning entity. | none | P4.24 | 4 h | | both |
| P6.17 | Translucency and the graphics presets | `options.graphics` has Fast, Fancy and Fabulous; `improvedTransparency` and `cutoutLeaves` are separate now. Fabulous means the translucent pass goes to its own target and is composited, which is what makes water over stained glass right. | none | P0.11 | 2 d | | fast |
| P6.18 | Cloud detail | `options.renderClouds` Off/Fast/Fancy and `renderCloudsDistance`. Fancy clouds are boxes with sides; the current deck is the flat version. Cloud height comes from the dimension type. | partial | P1.2 | 4 h | ‖ | fast |
| P6.19 | Void fog and the world bottom | Below the dimension's min_y the sky darkens and the fog closes in. | none | P1.2 | 2 h | ‖ | both |
| P6.20 | World border | `initialize_border`, `set_border_center`, `set_border_size`, `set_border_lerp_size`, `set_border_warning_delay`, `set_border_warning_distance`: the animated texture wall, the red vignette as you approach it, and the collision. | ignored | - | 1 d | | both |
| P6.21 | Biome blending | `options.biomeBlendRadius` has 8 settings from 1x1 to 15x15. Vanilla averages grass, foliage and water colour over that square so a biome edge is a gradient rather than a line. Plasma applies one biome's colour per block, so every edge is a hard seam. | none | - | 1 d | | fast |
| P6.22 | Chunk fade-in | `options.chunkFade` - a newly meshed section fades in rather than popping. One of the things that makes a fast client *look* fast. | none | - | 4 h | ‖ | fast |
| P6.23 | `add_transient_block` | New in 774 and easy to miss: a short-lived block the server shows without changing the world. | ignored | - | 2 h | ‖ | both |

**Phase total: about 6 weeks.**

**Mechanism worth writing down - 2,881 invisible states is the right number.**
`docs/ARCHITECTURE.md` walked these through rather than waving them away, and
that work pays off here: the list is already known to be block entities,
unreachable wall states and immature crops, with no unresolved remainder. So
Phase 6 has a definite end, and P6.8's "the rest of the block entities" is a
finite list of 49 types rather than an open-ended hunt. That is a direct benefit
of the earlier refusal to let an invisible block slide.

**Mechanism worth writing down - moon phases moved.** They used to be one 4x2
sheet; in 1.21.11 they are eight separate files under
`textures/environment/celestial/moon/`, declared by a new `celestials.json`
atlas. Anything written from memory about the moon will be wrong. This is the
kind of thing walking the jar catches and recollection does not.

---

## Phase 7 - containers

**Goal.** Every one of the 25 screens in the `minecraft:menu` registry, plus the
inventory, the creative inventory and the recipe book. Needs Phase 2's framework
and Phase 1's component codec; needs Phase 3 for the player model in the
inventory, which is why it is here and not earlier.

**Demonstrable at the end.** Open a chest, move items with every click type,
craft something, enchant something, trade with a villager, and see yourself
turning to watch the mouse in the inventory's corner.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P7.1 | `open_screen` and the container stack | A window id, a menu type from the 25-entry registry and a title. Plus `container_close` both ways, and the rule that closing drops the cursor stack. | ignored | P2.3 | 4 h | **!** | both |
| P7.2 | The slot grid | A container screen is a background sheet and a list of slots at fixed positions. The 30 `sprites/container/slot/` sprites cover the typed empty-slot icons (helmet, chest, legs, boots, shield, saddle, horse armour, llama armour, smithing template, banner, dye, amethyst, quartz, ingot, redstone, lapis, hoe, axe, sword, pickaxe, shovel). `sprites/container/inventory/` and the 23 legacy `container/*.png` sheets are the backgrounds. | display-only | P7.1 | 1 d | **!** | both |
| P7.3 | Slot item rendering | The item model at the `gui` display context, the stack count, the durability bar, the cooldown sweep, and the hover highlight. **Replaces the pasted-in icons.** | baked pictures | P4.12 | 4 h | **!** | both |
| P7.4 | Item tooltips | The name in its rarity colour, then every component's tooltip lines in vanilla's order: enchantments, attribute modifiers grouped by slot with their green and red deltas, potion effects, trim, dye, stored enchantments, map id, book author and generation, banner patterns, bundle contents, firework, food, durability under advanced tooltips, the registry id, the lore, and the "when on body" and "when in main hand" headers. **Horizon's own docs admit their tooltips still lack the attribute lines** - it is more work than it looks. | none | P1.5, P2.11 | 2 d | **!** | both |
| P7.5 | `container_click` | The click protocol, and it is the fussiest packet in the game. A window id, a state id that has to be echoed exactly, the slot, the button, a **mode** (0 pickup, 1 quick move, 2 swap, 3 clone, 4 throw, 5 quick craft, 6 pickup all), the list of slots the client thinks changed with their new contents, and the cursor's new contents. The client predicts the result and the server corrects it with the state id. Getting the predicted-changes list wrong gives the famous ghost item. | none | P1.5, P7.2 | 2 d | **!** | both |
| P7.6 | Every click gesture | Left, right, shift-left, shift-right, the number keys, F to swap off-hand, Q and Ctrl-Q to drop, double-click to gather, and the three-phase left and right drag-spread, which is click mode 5 with its own begin, add-slot and end sub-buttons. | none | P7.5 | 1 d | **!** | both |
| P7.7 | The inventory screen | The 2x2 crafting grid, the four armour slots, the off-hand slot, the 27 main slots, the 9 hotbar slots, the recipe-book button, and **the player model in the corner that follows the mouse**. That model is the inventory's `display_context` on a live entity render, and its absence is the most-noticed single missing thing in an inventory screen. | display-only | P3.17, P7.2 | 1 d | **!** | both |
| P7.8 | Creative inventory | 14 tabs from the `creative_mode_tab` registry, the tab sprites (30 under `sprites/container/creative_inventory/`), the scroller, the search tab with its text field and the search over item names and tags, the destroy slot, the saved hotbars, the `set_creative_mode_slot` packet, and `options.operatorItemsTab`. 16 `itemGroup.*` keys. | none | P7.5, P2.10 | 2 d | | both |
| P7.9 | Crafting table | The 3x3 grid, the result slot, and the `place_recipe` packet for recipe-book clicks. | none | P7.5 | 4 h | | both |
| P7.10 | Furnace family | Furnace, blast furnace, smoker: the fuel slot, the input, the output, the burn-time flame and the progress arrow, both driven by `container_set_data`, and the recipe-book tab. 2 sprites each. | none | P7.5 | 4 h | | both |
| P7.11 | Chest-like containers | The six `generic_9xN` sizes, `generic_3x3`, hopper, shulker box, barrel, ender chest, dispenser, dropper, and the horse inventory with its typed slots. | none | P7.2 | 4 h | | both |
| P7.12 | Recipe book | 30 sprites under `sprites/recipe_book/`, 13 categories from the registry, the toggle button, the search field, the filter button, the page arrows, the ghost recipe overlay from `place_ghost_recipe`, and the three `recipe_book_*` serverbound packets. 10 `gui.recipebook.*` keys. | none | P7.9, P1.25 | 2 d | | fast |
| P7.13 | Anvil | Two inputs, an output, the name field, the enchantment cost with its "Too Expensive!" state, and `rename_item`. 3 sprites. | none | P7.5, P2.7 | 4 h | | both |
| P7.14 | Enchanting table | Three enchantment slots each with a level requirement, a lapis count, the **Standard Galactic Alphabet** clue text - which is a real font provider in the jar, `assets/minecraft/font/alt.json`, so it is read rather than faked - the hover tooltip revealing one enchantment, and `container_button_click`. 9 sprites. | none | P7.5 | 1 d | | both |
| P7.15 | Brewing stand | Three bottle slots, the ingredient, the blaze powder, the bubbles and the arrow. 3 sprites. | none | P7.5 | 3 h | | both |
| P7.16 | Villager trading | The offer list from `merchant_offers` (each offer being two inputs, an output, whether it is out of stock, uses, max uses, xp, a price multiplier and a special price), the five profession levels with their xp bar, the scroller, `select_trade`, and 9 sprites plus 8 `merchant.*` keys. | none | P7.5 | 1 d | | both |
| P7.17 | Beacon | The three-tier pyramid display, the primary and secondary effect choice, the payment slot, and `set_beacon`. 6 sprites. | none | P7.5 | 4 h | | both |
| P7.18 | Grindstone, smithing, stonecutter, loom, cartography table, crafter | Six more screens, each a small slot layout plus one list or one preview. The loom's 6 sprites include its pattern list; the stonecutter's 5 include its recipe buttons; the smithing table has the template, base and addition slots with their "can't be upgraded this way" tooltip. | none | P7.5 | 1 d | | both |
| P7.19 | Lectern and the book screens | The lectern's page view, the book reading screen, the book editing screen with `edit_book` and the sign-and-close flow, and the 17 `book.*` keys. | none | P7.5, P2.7 | 1 d | | both |
| P7.20 | Sign editing | `open_sign_editor` and `sign_update`: the four lines, the front and back, and the glowing toggle. | none | P6.4, P2.7 | 4 h | | both |
| P7.21 | Bundles | The 12 `sprites/container/bundle/` sprites: the fill bar, the selected-item frame, the tooltip grid of contents, and `bundle_item_selected`. | none | P7.5, P1.5 | 4 h | | both |
| P7.22 | `container_slot_state_changed` | The crafter's slot enable and disable. | none | P7.18 | 1 h | ‖ | both |
| P7.23 | Operator screens | Command block (19 `advMode.*` keys), structure block (41 `structure_block.*`), jigsaw block (14 `jigsaw_block.*`), and their four serverbound packets. Low priority but they are parity and a server will send `open_screen` for them. | none | P7.1 | 1 d | | both |
| P7.24 | Horse and mount screens | `mount_screen_open`, the horse inventory with saddle and armour, and the llama's decor slot. | none | P7.11 | 3 h | | both |
| P7.25 | Dialogs | **A whole screen system most people do not know exists.** `show_dialog` / `clear_dialog` in both the configuration and play phases, `custom_click_action` serverbound, 3 `sprites/dialog/` textures, and the `menu.custom_options` and `menu.custom_screen_info.*` keys. Since 1.21.6 a server can define its own screens - notices, confirmations, multi-action lists, server-link lists, dialog lists - with text inputs, booleans and single-select widgets, declared as data. Servers are starting to use it instead of chat menus. This was found by walking the packet list, not from memory. | ignored | P2.3 | 2 d | **!** | both |

**Phase total: about 5.5 weeks.**

**Mechanism worth writing down - the state id is the whole anti-desync
protocol.** `container_set_content` and `container_set_slot` each carry a state
id. A `container_click` must echo the last one the client saw. If the server's
state id has moved on, it rejects the click's prediction and resends the whole
container. This is why the client is *expected* to predict the outcome of a click
locally and send what it thinks changed - the protocol is built around optimistic
local application with a cheap correction. A client that does not predict feels
laggy; a client that predicts and reports its changes wrongly produces items
that appear and vanish. Plasma reads the state id today and comments that it is
"only needed to echo back on a click" - which is exactly right, and P7.5 is where
that comment gets cashed in.

---

## Phase 8 - the whole HUD

**Goal.** Everything vanilla draws over the world while you play, which is a
great deal more than a hotbar and some hearts. The existing HUD covers maybe a
fifth of it.

**Demonstrable at the end.** Join a minigame server and have its scoreboard,
boss bar, titles, action bar and tab list all appear correctly, with chat you can
scroll, click links in, and complete commands in.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P8.1 | HUD layout in GUI units | Move the existing HUD off the 1600x900 design space onto P2.2's integer scale. Everything below assumes it. | wrong | P2.2 | 4 h | **!** | both |
| P8.2 | Heart variants | **47 sprites under `sprites/hud/heart/`** and the HUD uses 3. The full matrix is normal, poisoned, withered, frozen, absorbing and vehicle, crossed with full and half, crossed with hardcore and not, crossed with blinking and not. Blinking is the low-health pulse; the container has its own blinking form. Also the health-row wobble when damaged and the second row for absorption. | 3 of 47 | P1.12, P1.15 | 1 d | | both |
| P8.3 | Food variants | The hunger-effect forms (`food_empty_hunger`, `food_full_hunger`, `food_half_hunger`) and the saturation jitter that makes the drumsticks shake. | 3 of 6 | P1.12 | 3 h | | both |
| P8.4 | Air bar | `hud/air`, `air_bursting`, `air_empty` while underwater, with the bubbles popping. | none | P6.15 | 2 h | | both |
| P8.5 | Jump bar | `hud/jump_bar_background`, `_progress`, `_cooldown` - the horse jump charge, which replaces the experience bar while riding. | none | P3.37 | 2 h | | both |
| P8.6 | Off-hand slot | `hud/hotbar_offhand_left` and `_right`, placed according to `options.mainHand`. | none | P8.1 | 2 h | | pvp |
| P8.7 | Effect icons | The 40 `mob_effect` textures, drawn top-right over `hud/effect_background` or `_background_ambient`, with the duration text and the blinking in the last 10 seconds, and the inventory-screen variant that lists them with names and durations. | none | P1.12 | 4 h | | pvp |
| P8.8 | Chat, properly | Vanilla's chat is: a scrollable 100-line history with the scrollbar, the input line with its own history navigable by arrow keys, the 10-second fade for each line, `options.chat.*` (visibility, opacity, line spacing, scale, width, height focused and unfocused, delay, colours, links, link prompt, drafts), clickable and hoverable text components (`run_command`, `suggest_command`, `open_url` with its confirm prompt, `copy_to_clipboard`, and hover text), word wrapping, the `chat_screen.*` keys, and `delete_chat` for removed messages. The current version is a 64-line list drawn as plain text. | partial | P2.7, P1.4 | **1 w** | **!** | both |
| P8.9 | Chat signing | The session key from the profile, `chat_session_update`, the signature chain with its preceding-message hashes, the acknowledged-messages bitset, the index, and the checksum. Plasma sends `salt 0, unsigned`, which **a server with `enforce-secure-chat=true` disconnects**. That is most public servers. Also `chat_ack` and `chat_command_signed`. | unsigned | - | 1 w | **!** | both |
| P8.10 | Command suggestions | `command_suggestion` out, `command_suggestions` in: the popup list under the input, Tab cycling, the highlighted range, per-suggestion tooltips, `options.autoSuggestCommands`, and `custom_chat_completions` for server-provided names. | none | P8.8, P1.24 | 1 d | | both |
| P8.11 | Local command parsing | With the command tree from P1.24, the client colours arguments as you type and refuses to send a malformed command. That colouring is most of why vanilla's command input feels helpful. | none | P1.24, P8.10 | 2 d | | fast |
| P8.12 | Tab player list | `tab_list` gives a header and footer. The list itself comes from `player_info_update`: sorted by list priority then name, in up to 4 columns, with the player's head rendered from their skin, the latency as a 5-state bar (3 `sprites/player_list/` sprites), the display name, and the `belowName` scoreboard column with `multiplayer.player.list.hp`. | none | P1.18, P3.21 | 1 d | | pvp |
| P8.13 | Scoreboard sidebar | `set_objective`, `set_display_objective`, `set_score`, `reset_score`, `set_player_team`: the sidebar with its title, up to 15 lines sorted by score, the right-aligned score in its number format (blank, styled or fixed), team prefixes and suffixes, and the three display slots (list, sidebar, below name) plus the 16 per-colour sidebar slots. **Minigame servers live on this.** | none | - | 2 d | **!** | pvp |
| P8.14 | Teams | The team data in `set_player_team`: the display name, prefix, suffix, colour, friendly fire, see-invisible-teammates, nametag visibility rule, collision rule. Drives nametag colours and the glowing outline colour. | none | P8.13 | 4 h | | pvp |
| P8.15 | Boss bars | `boss_event` add, remove, update health, update title, update style, update flags. **22 `sprites/boss_bar/` sprites**: 6 colours x background and progress, plus 5 notched overlays and their progress forms. Multiple bars stack. Darkens the sky, creates fog and plays the dragon music when flagged. | none | - | 1 d | | both |
| P8.16 | Titles | `set_title_text`, `set_subtitle_text`, `set_titles_animation` (fade in, stay, fade out), `clear_titles`. 13 `title.*` keys. | none | P8.1 | 4 h | **!** | both |
| P8.17 | Action bar | `set_action_bar_text` - the line above the hotbar. Used constantly by servers. | none | P8.1 | 1 h | **!** | both |
| P8.18 | Toasts | 15 `sprites/toast/` sprites with per-side nine-slice borders `{17,30,4,4}`: the advancement toast (3 forms), the recipe toast, the system toast, the tutorial toast, the "now playing" music toast. Each slides in, waits and slides out. | none | P2.3 | 1 d | | fast |
| P8.19 | Locator bar | **New in 1.21.9 and nobody would list it from memory.** `hud/locator_bar_background` (animated), `locator_bar_arrow_up` and `_down` (both animated), and 5 `sprites/hud/locator_bar_dot/` sprites. Driven by the `waypoint` packet, which the current client ignores entirely: a compass strip showing where other players are. | ignored | P8.1 | 1 d | | both |
| P8.20 | Crosshair modes | `options.attack.crosshair` versus `.hotbar` for the attack indicator, the F5 third-person variant, the spectator variant, and the high-contrast block outline option. | partial | P4.9 | 3 h | | pvp |
| P8.21 | Held-item name popup | The item name fading in above the hotbar when you switch slots, in the item's rarity colour, which Plasma draws but not with vanilla's timing or colour. | partial | P1.5 | 2 h | | both |
| P8.22 | Autosave and sleep overlays | `options.autosaveIndicator`, the 3 `sleep.*` keys and `multiplayer.stopSleeping`. | none | P8.1 | 2 h | ‖ | both |
| P8.23 | `player_combat_*` | Combat enter, end and kill. The kill packet carries the death message and is what opens the death screen. | ignored | P2.21 | 2 h | **!** | pvp |
| P8.24 | Gamemode switcher | F4: the 2 `sprites/gamemode_switcher/` sprites and the 6 `gameMode.*` keys, plus `change_game_mode`. | none | P8.1 | 3 h | | both |
| P8.25 | Spectator menu and HUD | 5 `sprites/spectator/` sprites, 8 `spectatorMenu.*` keys, `set_camera` to possess an entity, `teleport_to_entity`, the spectator hotbar, and `options.toggleSpectatorShaderEffects`. | none | P8.24 | 1 d | | both |
| P8.26 | Tutorial hints | 16 `tutorial.*` keys as toasts: move, look, find a tree, punch a tree, craft planks, open your inventory, bundle insert, social interactions. Parity, low value, cheap. | none | P8.18 | 3 h | ‖ | fast |
| P8.27 | `player_look_at` and `player_rotation` | The server forcing the camera to face a point or a rotation. Used by cutscene-style servers. | ignored | - | 2 h | ‖ | both |

**Phase total: about 7 weeks**, of which chat and chat signing are two.

**Mechanism worth writing down - chat signing is not optional any more.** Since
1.19 a chat message carries a 256-byte signature over the message, a salt, a
timestamp and the hashes of the messages the client had seen, chained so the
server can prove nobody edited the history. `enforce-secure-chat=true` is the
default on a vanilla server, and a client that sends `signature: absent` is
disconnected with `multiplayer.disconnect.unsigned_chat`. Plasma's current
comment - "needs enforce-secure-chat=false" - is honest and is also a statement
that the client cannot talk on most servers. The work is: get the player's
private key and its Mojang-signed public key from
`api.minecraftservices.com/player/certificates`, send the public key and
signature in `chat_session_update`, then sign each message over the canonical
byte sequence and maintain the acknowledged-message bitset. It is a week and it
is not negotiable for a shipping client.

**Mechanism worth writing down - the scoreboard is how servers talk to you.**
A minigame server's entire interface is `set_objective` plus `set_score` plus
team prefixes, and a client without it shows a bare world where everyone else
sees a scoreboard, a timer and their stats. For the PvP audience specifically
this is not a nicety - it is the screen they look at. It is two days.

---

## Phase 9 - options, keys and debug

**Goal.** The player can configure the client and inspect it, the way vanilla
lets them. 283 `options.*` keys, 164 `key.*` keys, 61 `debug.*` keys.

**Demonstrable at the end.** Every vanilla option present and persisted in
`options.txt`; every vanilla key rebindable; F3 showing what vanilla's F3 shows.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P9.1 | `options.txt` | Read and write vanilla's own `options.txt` so settings carry across, the same argument as `servers.dat`. About 90 keys in a `key:value` text format. | none | - | 4 h | **!** | both |
| P9.2 | Keybind registry | A named, rebindable binding per `key.*`: **59 gameplay and UI binds, 9 hotbar binds and 21 debug binds**, in the 8 categories the `key.categories.*` keys name (movement, gameplay, inventory, creative, multiplayer, UI, misc, spectator). Each with a default, a current value, and the ability to be unbound or to clash - with the clash shown, per `controls.keybinds.duplicateKeybinds`. Currently every key is hardcoded in `window.cpp`. | hardcoded | - | 1 d | **!** | both |
| P9.3 | `client_information` from options | Send the player's real locale, view distance, chat mode, chat colours, skin parts, main hand, text filtering, server listings and particle level - and re-send when they change. | hardcoded | P9.1 | 2 h | **!** | both |
| P9.4 | Options screen | The root: Music & Sound, Video, Controls, Language, Chat, Resource Packs, Accessibility, Skin Customization, Online, Telemetry (replaced), Credits & Attribution, plus FOV and Brightness sliders, Difficulty, and the in-world versus title-screen variants. | none | P2.9, P2.5 | 1 d | **!** | both |
| P9.5 | Video settings | Graphics preset, render distance, simulation distance, max framerate, VSync, clouds, cloud distance, chunk fade, particles, mipmap levels, texture filtering (none, anisotropic, RGSS), anisotropy level, smooth lighting, biome blend, entity shadows, entity distance, FOV effects, distortion effects, glint speed, glint strength, improved transparency, see-through leaves, chunk builder priority, fullscreen and its resolution list, GUI scale, attack indicator, autosave indicator, inactivity FPS limit, and the unsupported-GPU warning. | none | P9.4 | 2 d | | fast |
| P9.6 | Controls screen | Sensitivity, invert mouse X and Y, scroll sensitivity, discrete scrolling, raw input, touchscreen mode, auto-jump, sneak and sprint as hold-or-toggle (`options.key.hold` / `.toggle`), sprint window, rotate with minecarts, allow cursor changes, and the Key Binds sub-screen with its per-row rebind button and Reset All. | none | P9.2, P9.4 | 1 d | | pvp |
| P9.7 | Sound settings | The 11 category sliders, audio device selection, directional audio, show subtitles, music frequency, music toast. | none | P5.5, P9.4 | 4 h | | both |
| P9.8 | Chat and skin settings | The 19 `options.chat.*` keys and the 7 `options.modelPart.*` toggles plus main hand. | none | P9.4 | 4 h | | both |
| P9.9 | **F3 debug screen** | Vanilla's left column: version, FPS with its min/max and the chunk-update counts, the T/E/B/SD/FD/CE counters, the particle count, the chunk counts, the dimension, and on the right the Java and memory lines, the CPU and GPU names, the display mode, the XYZ and block coordinates, the chunk-relative coordinates, the facing with its axes, the client and server light, the CH and SH heightmaps, the biome, local difficulty, the sounds counter, the day and time. Plus the F3 chord set: F3+G chunk borders, F3+B hitboxes, F3+A reload chunks, F3+T reload packs, F3+H advanced tooltips, F3+C copy location and the crash chord, F3+N spectator cycle, F3+F4 gamemode switcher, F3+I copy data, F3+L profiling, F3+P focus pause, F3+1/2/3 the FPS, network and profiling charts, F3+Q the help list. 61 `debug.*` keys and 9 `debug.options.*`. | none | P1.*, P8.1 | **1 w** | **!** | both |
| P9.10 | F3 charts | The frame-time graph, the network graph and the profiler pie, which are what a performance-focused client should be *best* at. Plasma already measures real GPU and CPU times per frame, so this is drawing work rather than instrumentation work - and it is the one place where being a faster client can show its evidence inside the vanilla interface. | none | P9.9 | 1 d | | fast |
| P9.11 | F5 perspectives | First person, third person behind, third person front. Third person needs the player model (P3.17), a camera that collides with blocks so it does not go through a wall, and the hand suppressed. | none | P3.17 | 1 d | **!** | both |
| P9.12 | F1 hide GUI | Hides the whole HUD and the hand. | none | P8.1 | 1 h | ‖ | both |
| P9.13 | F2 screenshots | Write a PNG into `screenshots/`, with the chat confirmation that has a click-to-open link. `src/image_png.cpp` already writes PNGs. 2 `screenshot.*` keys. | none | - | 3 h | ‖ | both |
| P9.14 | F11 fullscreen | And the fullscreen resolution list. | none | P9.5 | 2 h | ‖ | both |
| P9.15 | Cinematic camera | `key.smoothCamera` - the smoothed mouse that the F-key toggles. | none | P9.2 | 2 h | ‖ | both |
| P9.16 | Pick block | `key.pickItem` with the creative and survival behaviours, and the NBT-copying Ctrl variant. | none | P4.30 | 2 h | ‖ | both |
| P9.17 | Hotbar save and load | `key.saveToolbarActivator` and `loadToolbarActivator` - the creative saved hotbars, stored locally. | none | P7.8 | 3 h | ‖ | both |
| P9.18 | Quick actions | `key.quickActions` and the `menu.quick_actions` screen, new in 1.21.9. | none | P2.3 | 3 h | ‖ | both |
| P9.19 | `debug_subscription_request` and the debug packets | The five `debug/*` clientbound packets and `debug_sample`. Developer-facing, op-gated; handle without falling over. | ignored | - | 3 h | ‖ | both |
| P9.20 | Brightness and reduced debug info | `options.gamma` and `options.reducedDebugInfo`. The Brightness slider is where fullbright honestly lives. | none | P9.4 | 2 h | ‖ | both |

**Phase total: about 4 weeks.**

**Mechanism worth writing down - 21 of the 164 key bindings are debug chords.**
A list of "the vanilla keys" written from memory will have about fifteen entries.
The language file has 59 real binds plus 9 hotbar plus 21 under `key.debug.*`,
and those 21 are not a separate system - they are bindings in a category that the
Key Binds screen lists like any other, held behind the `key.debug.modifier` key.
Which means the keybind registry (P9.2) has to exist before F3 (P9.9) can be
built properly, and that is why they are in this order.

---

## Phase 10 - packs, fonts, languages

**Goal.** The client can be themed and read in any language, which is a parity
requirement and also the thing most likely to make it adoptable.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P10.1 | Pack format and loader | A resource pack is a zip or a directory with `pack.mcmeta` declaring a `pack_format` (75 for 1.21.11) and an optional supported range, plus `assets/<namespace>/...`. A stack of packs where a later pack's file wins, with the vanilla jar at the bottom. | none | - | 2 d | **!** | fast |
| P10.2 | Built-in packs and known packs | The vanilla and High Contrast packs (store-only), `resourcePack.vanilla`, `.high_contrast`, `.programmer_art`, and reporting the real known-pack list in configuration instead of zero. | wrong | P10.1 | 4 h | | both |
| P10.3 | Server resource packs | `resource_pack_push` and `_pop` in both phases: the prompt screen (`multiplayer.texturePrompt.*`, `.requiredTexturePrompt.*`), the HTTPS download with its progress bar, the SHA-1 verification, applying it, and the per-server policy from P2.16. Plasma declines everything today. | declines | P10.1, P2.3 | 2 d | **!** | both |
| P10.4 | Pack selection screen | 23 `pack.*` keys, `gui.packSelection`, the available and selected lists with drag-between, the pack icon, the incompatible warning (7 keys), the source labels (5 keys) and the drop-to-add. Shared with the data-pack screen. | none | P10.1, P2.9 | 1 d | | fast |
| P10.5 | Rebaking on a pack change | The atlas, the block table's textures, the GUI atlas, the item models and the entity textures all come from a baked file today. A pack change has to rebuild them - which means the bakers move from install-time scripts into the client, or the client learns to run them. **This is the architectural decision in this phase** and it should be made before P10.1 starts. | none | - | 1 w | **!** | fast |
| P10.6 | Font definitions | `assets/minecraft/font/*.json` with their provider types: `bitmap` (done), `space` (done), `unihex` for unifont, `reference` to another font, `ttf`, and the `filter` wrapper. Named fonts: `default`, `alt` (the Standard Galactic Alphabet the enchanting table needs), `illageralt`, `uniform`. Text components can name a font. | bitmap only | P2.1 | 1 d | | both |
| P10.7 | Unifont | `unifont.zip` from the object store is a hex font: 16x16 glyph bitmaps as hex strings, 100k-odd codepoints, plus `unifont_jp` for the Japanese glyph variants option and `unifont_pua`. Without it there is no CJK text at all, so a Chinese player sees empty boxes. `options.forceUnicodeFont` and `options.japaneseGlyphVariants`. | none | P10.6 | 1 d | | fast |
| P10.8 | Languages | `pack.mcmeta` in the object store is the **143-locale table**, with each locale's name, region and bidirectional flag. 5 are right-to-left (`ar_sa`, `he_il`, `fa_ir`, `yi_de`, `zlm_arab`), which means the text renderer needs bidi reordering, not just a different file. Plus the Language screen and `options.languageWarning`. | en_us only | P2.1, P10.7 | 2 d | | fast |
| P10.9 | Text component rendering, properly | Plasma strips formatting to plain text (`json::strip_formatting`). Vanilla renders colour, bold, italic, underline, strikethrough and obfuscated (the scrambling `§k`), nested styles, hex colours, the named font, shadow colour, insertion text, click events and hover events - in both the JSON and NBT forms. Every chat line, every tooltip, every screen title and every disconnect reason goes through this. | stripped | P2.2 | 2 d | **!** | both |
| P10.10 | Core and post shaders | 75 GLSL files under `shaders/core` (38 programs) and 6 post chains over 10 fragment shaders, with 8 shared includes. **Mojang's own specification of every render type, its vertex format and its uniform blocks, with the maths written out** - the single most reusable document in the jar for a renderer. Parity means the 6 post chains exist: blur (6 passes), spider (10 passes over 4 targets), entity_outline (4), creeper, invert, transparency. A pack replacing them is a further step and Horizon openly does not do it. | none | P3.47 | 2 w | | fast |
| P10.11 | Accessibility screen | The 18 `options.accessibility.*` keys: high contrast, high contrast block outline, text background and its opacity, menu background blurriness, panorama speed, damage tilt, distortion effects, FOV effects, darkness pulsing, hide lightning flashes, hide splash texts, sprint window, notification time, plus the narrator mode and the onboarding screen. | none | P9.4 | 1 d | | fast |
| P10.12 | Narrator | 23 `narrator.*` and 29 `narration.*` keys, four modes (off, all, chat, system). Needs a platform speech backend - speech-dispatcher on Linux. P2.25 put the hooks in; this is the voice. Honest note: Horizon's own narrator doc admits their narration "comes close to vanilla's widget trees but not always all the way", and that ordering is the hard part. | none | P2.25 | 1 w | | fast |
| P10.13 | `update_enabled_features` | Feature flags the server has on, which gate items and blocks. | ignored | P1.1 | 2 h | ‖ | both |
| P10.14 | `reset_chat` | Clears the chat and the signature chain on a configuration re-entry. | ignored | P8.9 | 1 h | ‖ | both |

**Phase total: about 7 weeks**, of which the core and post shaders are two and
the narrator one. Both of those are the most deferrable items in the whole plan.

---

## Phase 11 - the long tail

**Goal.** Close the enumerations. Nothing here is big; the point of the phase is
that after it there is no sprite, no language prefix and no packet without an
owner.

| # | Item | What it is | Now | Needs | Size | Flag | Who |
|---|---|---|---|---|---|---|---|
| P11.1 | Advancements screen | **36 `sprites/advancements/` sprites** plus 5 background textures: the tab tree, the pannable graph, the frame types (task, goal, challenge) crossed with obtained and unobtained, the connector lines, the item icon, the title and description, and the progress text. `update_advancements` and `select_advancements_tab` in, `seen_advancements` out. 258 `advancements.*` keys. | ignored | P2.10, P7.4 | 1 w | | fast |
| P11.2 | Advancement toasts | The 3 toast forms and `advancements.toast.*`. | none | P8.18, P11.1 | 3 h | | fast |
| P11.3 | Statistics screen | 9 `sprites/statistics/` sprites, three tabs (general, items, mobs), the sortable columns with their sort-arrow sprites, the 9 stat types and 77 custom stats formatted as time, distance or count. `award_stats` in, `client_command` 1 out to request them. 80 `stat.*` and 10 `stat_type.*` keys. | ignored | P2.9, P2.10 | 2 d | | fast |
| P11.4 | Social interactions | 9 `sprites/social_interactions/` sprites, 30 `gui.socialInteractions.*` keys: the player list with heads, the search field, the three tabs, and the local mute and hide-messages toggles. The reporting half is excluded (section 3). | none | P2.9, P3.21 | 1 d | | fast |
| P11.5 | Maps | `map_item_data`: the 128x128 colour grid with its 4-colour-shade palette, scaling, locked flag, and the 35 `map_decoration_type` icons from the `map_decorations` atlas. Rendered as an item in hand (two-handed), in an item frame, and on the item model. 16 `filled_map.*` keys and 37 `textures/map/` files. | ignored | P4.12 | 2 d | | both |
| P11.6 | Paintings | 102 `painting.minecraft.*` keys and 52 textures; the variant registry gives each one's width and height in blocks. | none | P3.72, P1.1 | 4 h | | both |
| P11.7 | Jukebox songs | 21 `jukebox_song.*` registry entries with their description, length and comparator output, and the action-bar "Now Playing" line. | none | P5.16, P1.1 | 2 h | ‖ | both |
| P11.8 | `tag_query` and `block_entity_tag_query` | F3+I's data copy, op-gated. | ignored | P9.9 | 2 h | ‖ | both |
| P11.9 | `open_book` | The server opening a written book. | ignored | P7.19 | 1 h | ‖ | both |
| P11.10 | `set_default_spawn_position` | The compass target and the respawn point. | ignored | - | 1 h | ‖ | both |
| P11.11 | `projectile_power` | The crossbow and bow power modifier used for the trajectory preview. | ignored | P3.70 | 1 h | ‖ | pvp |
| P11.12 | `set_simulation_distance` | What the server simulates, which bounds how far entities update. | ignored | - | 1 h | ‖ | both |
| P11.13 | `pong_response` and `ping_request` | The client-initiated round trip, which is how a client measures its own latency rather than reading the server's published number. Relevant to the PvP profile. | ignored | - | 2 h | ‖ | pvp |
| P11.14 | `disguised_chat` | Named in the id table, never handled: a chat message the server is sending as someone else. | none | P1.4 | 1 h | ‖ | both |
| P11.15 | `custom_report_details` | The key-value block a server contributes to a crash report. | ignored | P1.26 | 1 h | ‖ | both |
| P11.16 | `move_vehicle` both ways | Riding a boat or a horse over the network. | ignored | P3.67, P4.31 | 4 h | | both |
| P11.17 | `paddle_boat` | The boat paddle state. | ignored | P3.66 | 1 h | ‖ | both |
| P11.18 | `jigsaw_generate` | The jigsaw block's generate button. | ignored | P7.23 | 1 h | ‖ | both |
| P11.19 | Credits and attribution | `credits.json` is 4,864 lines, `texts/end.txt` is the 152-line end poem, `postcredits.txt` follows it. 4 `credits_and_attribution.*` keys. Vanilla's rolling credits after the dragon. Horizon has not built this either. | none | P2.3 | 1 d | | fast |
| P11.20 | The `test_*` family | `test_instance_block_status` in, `test_instance_block_action` out, the test block screens, 45 `test.*` keys. Parked at the very end: not worth building, but the client must not fall over when an op on a dev server sends one. | ignored | - | 2 h | ‖ | both |
| P11.21 | Recover-world and optimise-world screens | 15 `recover_world.*` and 19 `optimizeWorld.*` keys. Singleplayer-only, so excluded by section 3 - listed here so the prefixes in Appendix C have an owner. | n/a | - | - | ‖ | - |

**Phase total: about 3.5 weeks.**

---

## Phase 12 - the Plasma layer

Only after everything above. By then the vanilla profile is a vanilla client,
and everything here draws only in the Plasma profile.

| # | Item | What it is | Size | Who |
|---|---|---|---|---|
| P12.1 | Profile switch enforced | Every non-vanilla drawing path behind `Registry::shows()`, with a build-time assertion that the vanilla profile touches none of them. Section 4 is the checklist. | 2 d | both |
| P12.2 | The feature menu | The existing `menu.cpp` styling, restored as a Plasma-profile screen on top of the vanilla screen stack rather than instead of it. | 1 w | pvp |
| P12.3 | HUD modules | FPS, CPS, ping, keystrokes, coordinates, armour, potions, reach display - as movable, resizable HUD modules with a layout editor, which is what the Lunar and Badlion audience actually expects. | 2 w | pvp |
| P12.4 | Toggle sprint and sneak | With the server-forbids mechanism already in `features.hpp`. | 2 d | pvp |
| P12.5 | Zoom | The only genuinely non-vanilla optic, and the one every PvP client ships. | 1 d | pvp |
| P12.6 | Server feature control | The `plasma:features` plugin channel, so a server can switch features off rather than ban the client. Already designed in `features.hpp`; this is the implementation and the server-side plugin. | 1 w | pvp |
| P12.7 | Performance work with evidence | The reason the "faster vanilla" profile exists. Plasma already measures real GPU and CPU time per frame and publishes both halves on purpose; P9.10's F3 charts are where that becomes a selling point inside the vanilla interface. | ongoing | fast |

---

## Totals, and an honest word about them

| Phase | Work |
|---|---|
| 0 - foundation | 8 d |
| 1 - data layer | 5 w |
| 2 - screen spine | 3 w |
| 3 - entities | 11 w |
| 4 - hands and interaction | 7 w |
| 5 - sound | 2.5 w |
| 6 - block entities and world | 6 w |
| 7 - containers | 5.5 w |
| 8 - HUD | 7 w |
| 9 - options and debug | 4 w |
| 10 - packs and languages | 7 w |
| 11 - long tail | 3.5 w |
| **vanilla parity total** | **about 63 weeks** |
| 12 - the Plasma layer | about 6 w |

**Read that number carefully.** 63 person-weeks is a year and a bit of one
person's full attention, and it is the estimate for *reaching* parity, not for
*verifying* it. Two honest caveats:

**It is an underestimate, and here is the evidence.** Horizon - the Rust client
solving the same problem, studied for this document - is 1,082,104 lines of Rust
across 2,069 files, with another 154,628 lines of test and oracle
infrastructure. Its own port ledger, which matches every vanilla method against
the Rust function that ports it, currently reports **833 player-visible gaps
still open** - and the two largest buckets are **197 in screens and HUD** and
**129 in entity and block rendering**, which are exactly Phases 2, 7, 8 and 3
here. That is a project at a million lines, with 379 differential test harnesses
all passing, still counting 326 open gaps in the two areas this plan spends 30 of
its 63 weeks on. The ratio worth taking from that is not the line count - it
includes a dedicated server and world generation, both out of scope here - it is
that **GUI and entity rendering are where parity work goes to die**, and that my
estimates for those phases are the ones most likely to be low.

**The sizes are a guess calibrated against one data point.** Where I am
confident, it is because the work is enumerable: 25 container screens, 49 block
entity types, 157 entity types, 104 component types, 164 keybinds. Where I am
guessing, the number carries a `?` or the item says so. P3.12 (the geometry
pipeline) is the largest guess in the document, because it depends on a decision
that has not been made yet (P3.11). P10.10 (core and post shaders) is the second,
because two weeks assumes the GLSL translates and does not assume a pack can
replace it.

**What the ordering buys you.** The three codecs in Phase 1 - components,
metadata, registries - unblock about 60% of the remaining items. The screen
framework in Phase 2 unblocks about 40 screens. The entity engine in Phase 3a-c
unblocks 29 rows of 3d plus the inventory player model plus third person. If
anything in this plan is worth protecting against being deferred, it is those
three, and that is what the `!` flags mark.

**What to do first, concretely.** Phase 0, items P0.1 and P0.2, this afternoon:
they are a dead branch and a missing packet handler, they take half a day
together, and they are both silently corrupting the client you are testing
everything else in.

---

## Appendix A — every packet

**Why this table exists.** `reports/packets.json` for protocol 774 is the ground
truth for what a client can and cannot do, and it is a finite list. Every one of
its 251 entries appears below, marked **done**, *partial* or ignored, and every
ignored or partial row names the plan item that covers it. A packet with no
owner would be a hole; there are none.

Generated from
`out774/reports/packets.json`, cross-checked against the field names in
`include/mcpp/proto/packet_ids.hpp` and the branches actually present in
`src/session.cpp`. Note the distinction the table makes and the id table does
not: **being named in `packet_ids.hpp` is not the same as being handled.** Twelve
clientbound play packets are in the generated id table and have no branch in
`handle_play` — `container_close`, `custom_payload`, `disguised_chat`,
`forget_level_chunk`, `move_entity_rot`, `player_info_remove`, `respawn`,
`set_chunk_cache_center`, `set_chunk_cache_radius`, `set_cursor_item`,
`set_player_inventory`, `chunk_batch_start`. `respawn` is one of them, and the
README claims it works.

Across all phases and both directions: **251 packets**. **42 fully handled**, *28 partially handled*, 177 ignored with an owner, 4 deliberately out of scope. Every packet has an owner; there are no unassigned rows.

### handshake / serverbound — 1 packets: 1 done, 0 partial, 0 ignored, 0 out of scope

| id | packet | state | owner | note |
|---:|---|---|---|---|
| 0 | `intention` | **done** | - | Sent with next-state 2. P2.15 adds next-state 1 for the server list ping. |

### status / clientbound — 2 packets: 0 done, 0 partial, 2 ignored, 0 out of scope

| id | packet | state | owner | note |
|---:|---|---|---|---|
| 0 | `status_response` | ignored | P2.15 | Server list MOTD, version, players, favicon. |
| 1 | `pong_response` | ignored | P2.15 | The ping round trip. |

### status / serverbound — 2 packets: 0 done, 0 partial, 2 ignored, 0 out of scope

| id | packet | state | owner | note |
|---:|---|---|---|---|
| 0 | `status_request` | ignored | P2.15 |  |
| 1 | `ping_request` | ignored | P2.15 |  |

### login / clientbound — 6 packets: 5 done, 1 partial, 0 ignored, 0 out of scope

| id | packet | state | owner | note |
|---:|---|---|---|---|
| 0 | `login_disconnect` | **done** | - | Reason rendered via json::strip_formatting; P10.9 renders its formatting. |
| 1 | `hello` | **done** | - | Encryption request; RSA wrap and the Mojang join call are done. |
| 2 | `login_finished` | **done** | - | uuid and name read. |
| 3 | `login_compression` | **done** | - |  |
| 4 | `custom_query` | *partial* | P1.22 | Answered with "no payload" always. A modded server expects a real answer. |
| 5 | `cookie_request` | **done** | - | Answered with "none". |

### login / serverbound — 5 packets: 4 done, 1 partial, 0 ignored, 0 out of scope

| id | packet | state | owner | note |
|---:|---|---|---|---|
| 0 | `hello` | **done** | - |  |
| 1 | `key` | **done** | - |  |
| 2 | `custom_query_answer` | *partial* | P1.22 |  |
| 3 | `login_acknowledged` | **done** | - |  |
| 4 | `cookie_response` | **done** | - |  |

### configuration / clientbound — 20 packets: 4 done, 4 partial, 12 ignored, 0 out of scope

| id | packet | state | owner | note |
|---:|---|---|---|---|
| 0 | `cookie_request` | ignored | P1.23 | Not handled in configuration, only in login. |
| 1 | `custom_payload` | ignored | P1.22 | minecraft:brand must be answered. |
| 2 | `disconnect` | **done** | - |  |
| 3 | `finish_configuration` | **done** | - |  |
| 4 | `keep_alive` | **done** | - |  |
| 5 | `ping` | **done** | - |  |
| 6 | `reset_chat` | ignored | P10.14 |  |
| 7 | `registry_data` | *partial* | P1.1 | Only minecraft:worldgen/biome of the 95 registries is parsed. |
| 8 | `resource_pack_pop` | ignored | P10.3 |  |
| 9 | `resource_pack_push` | *partial* | P10.3 | Always answered "declined". |
| 10 | `store_cookie` | ignored | P1.23 |  |
| 11 | `transfer` | ignored | P1.23 | Must reconnect to the named host. |
| 12 | `update_enabled_features` | ignored | P10.13 |  |
| 13 | `update_tags` | ignored | P1.13 |  |
| 14 | `select_known_packs` | *partial* | P10.2 | Answers zero packs so the server resends everything. Correct for now, not vanilla. |
| 15 | `custom_report_details` | ignored | P1.26 |  |
| 16 | `server_links` | ignored | P1.26 |  |
| 17 | `clear_dialog` | ignored | P7.25 |  |
| 18 | `show_dialog` | ignored | P7.25 | The server-defined screen system. |
| 19 | `code_of_conduct` | *partial* | P2.22 | Auto-accepted; vanilla shows a screen. |

### configuration / serverbound — 10 packets: 4 done, 3 partial, 3 ignored, 0 out of scope

| id | packet | state | owner | note |
|---:|---|---|---|---|
| 0 | `client_information` | *partial* | P9.3 | Hardcoded values, not the player options. |
| 1 | `cookie_response` | ignored | P1.23 |  |
| 2 | `custom_payload` | ignored | P1.22 |  |
| 3 | `finish_configuration` | **done** | - |  |
| 4 | `keep_alive` | **done** | - |  |
| 5 | `pong` | **done** | - |  |
| 6 | `resource_pack` | *partial* | P10.3 | Always "declined". |
| 7 | `select_known_packs` | *partial* | P10.2 | Always zero. |
| 8 | `custom_click_action` | ignored | P7.25 |  |
| 9 | `accept_code_of_conduct` | **done** | - |  |

### play / clientbound — 139 packets: 14 done, 13 partial, 110 ignored, 2 out of scope

| id | packet | state | owner | note |
|---:|---|---|---|---|
| 0 | `bundle_delimiter` | ignored | P0.10 | Packets between two delimiters must be applied atomically. |
| 1 | `add_entity` | *partial* | P0.7 | Reads id, uuid, type, position; stops before pitch, yaw, head yaw, data and velocity. |
| 2 | `animate` | ignored | P3.31 | Swing, leave bed, crit, magic crit. |
| 3 | `award_stats` | ignored | P11.3 |  |
| 4 | `block_changed_ack` | *partial* | P4.5 | Sequence read and discarded; nothing is rolled back on a rejected dig or place. |
| 5 | `block_destruction` | ignored | P4.8 | Other players mining progress. |
| 6 | `block_entity_data` | ignored | P6.1 |  |
| 7 | `block_event` | ignored | P4.22 | Chest lids, note blocks, pistons, bells, spawners. |
| 8 | `block_update` | **done** | - |  |
| 9 | `boss_event` | ignored | P8.15 |  |
| 10 | `change_difficulty` | ignored | P1.15 |  |
| 11 | `chunk_batch_finished` | **done** | - | Answers with a fixed 32 chunks per tick rather than a measured rate. |
| 12 | `chunk_batch_start` | *partial* | P0.3 | Read only as a marker; vanilla uses the batch to pace chunk loading. |
| 13 | `chunks_biomes` | ignored | P1.29 |  |
| 14 | `clear_titles` | ignored | P8.16 |  |
| 15 | `command_suggestions` | ignored | P8.10 |  |
| 16 | `commands` | ignored | P1.24 | The command tree. |
| 17 | `container_close` | ignored | P7.1 | Named in the id table, never handled. |
| 18 | `container_set_content` | *partial* | P1.5 | Gives up at the first stack with data components. Container 0 only. |
| 19 | `container_set_data` | ignored | P7.10 | Furnace burn and progress, enchanting levels, beacon, brewing. |
| 20 | `container_set_slot` | *partial* | P1.5 | Same component limit. Container 0 only. |
| 21 | `cookie_request` | ignored | P1.23 |  |
| 22 | `cooldown` | ignored | P1.28 |  |
| 23 | `custom_chat_completions` | ignored | P8.10 |  |
| 24 | `custom_payload` | ignored | P1.22 | Named in the id table, never handled. |
| 25 | `damage_event` | ignored | P3.8 |  |
| 26 | `debug/block_value` | ignored | P9.19 | Op-gated developer packet. |
| 27 | `debug/chunk_value` | ignored | P9.19 | Op-gated developer packet. |
| 28 | `debug/entity_value` | ignored | P9.19 | Op-gated developer packet. |
| 29 | `debug/event` | ignored | P9.19 | Op-gated developer packet. |
| 30 | `debug_sample` | ignored | P9.19 | Tick-time samples for F3 charts. |
| 31 | `delete_chat` | ignored | P8.8 |  |
| 32 | `disconnect` | **done** | - |  |
| 33 | `disguised_chat` | ignored | P11.14 | Named in the id table, never handled. |
| 34 | `entity_event` | ignored | P3.7 | The one-byte status codes: hurt, death, taming, hearts, totem. |
| 35 | `entity_position_sync` | **done** | - |  |
| 36 | `explode` | ignored | P4.21 |  |
| 37 | `forget_level_chunk` | ignored | P0.2 | Named in the id table, never handled. Nothing is ever unloaded. |
| 38 | `game_event` | *partial* | P1.16 | Only sub-event 3 (game mode) of 16. |
| 39 | `game_test_highlight_pos` | out of scope | P11.20 | Mojang game-test harness; handle without falling over. |
| 40 | `mount_screen_open` | ignored | P7.24 |  |
| 41 | `hurt_animation` | ignored | P3.8 |  |
| 42 | `initialize_border` | ignored | P6.20 |  |
| 43 | `keep_alive` | **done** | - |  |
| 44 | `level_chunk_with_light` | *partial* | P6.1 | Blocks, light and biomes read; the block-entity list is read past. |
| 45 | `level_event` | ignored | P4.20 | About 70 one-shot world events. |
| 46 | `level_particles` | ignored | P4.19 | 115 particle types with per-type payloads. |
| 47 | `light_update` | *partial* | P0.1 | BROKEN: duplicated branch; the live one calls a callback nothing sets. |
| 48 | `login` | *partial* | P1.2 | Only the entity id is read. Dimension, game mode, hardcore, max players, view distance, reduced debug, enable respawn screen, limited crafting, sea level and the dimension name are all discarded. |
| 49 | `map_item_data` | ignored | P11.5 |  |
| 50 | `merchant_offers` | ignored | P7.16 |  |
| 51 | `move_entity_pos` | **done** | - |  |
| 52 | `move_entity_pos_rot` | *partial* | P0.8 | Reads the three deltas and stops; rotation and on-ground discarded. |
| 53 | `move_minecart_along_track` | ignored | P3.67 |  |
| 54 | `move_entity_rot` | ignored | P0.9 | Named in the id table, never handled. |
| 55 | `move_vehicle` | ignored | P11.16 |  |
| 56 | `open_book` | ignored | P11.9 |  |
| 57 | `open_screen` | ignored | P7.1 | The 25 container types. |
| 58 | `open_sign_editor` | ignored | P7.20 |  |
| 59 | `ping` | **done** | - |  |
| 60 | `pong_response` | ignored | P11.13 |  |
| 61 | `place_ghost_recipe` | ignored | P7.12 |  |
| 62 | `player_abilities` | ignored | P1.11 | Flight permission, fly speed, walk speed, instabuild. |
| 63 | `player_chat` | *partial* | P1.4 | Read and formatted by hand as "<name> body"; vanilla applies the chat_type registry template. Signature chain not verified. |
| 64 | `player_combat_end` | ignored | P8.23 |  |
| 65 | `player_combat_enter` | ignored | P8.23 |  |
| 66 | `player_combat_kill` | ignored | P8.23 | Carries the death message; opens the death screen. |
| 67 | `player_info_remove` | ignored | P1.19 | Named in the id table, never handled. |
| 68 | `player_info_update` | *partial* | P1.18 | Parsed only to extract the local player latency. Skin property, display name, game mode and listed flag all discarded. |
| 69 | `player_look_at` | ignored | P8.27 |  |
| 70 | `player_position` | **done** | - | Teleport confirmed and player_loaded sent. |
| 71 | `player_rotation` | ignored | P8.27 |  |
| 72 | `recipe_book_add` | ignored | P1.25 |  |
| 73 | `recipe_book_remove` | ignored | P1.25 |  |
| 74 | `recipe_book_settings` | ignored | P1.25 |  |
| 75 | `remove_entities` | **done** | - |  |
| 76 | `remove_mob_effect` | ignored | P1.12 |  |
| 77 | `reset_score` | ignored | P8.13 |  |
| 78 | `resource_pack_pop` | ignored | P10.3 |  |
| 79 | `resource_pack_push` | ignored | P10.3 | Only the configuration-phase version is answered; this one is ignored. |
| 80 | `respawn` | ignored | P0.6 | Named in the id table, never handled. The README claims it is handled; it is not. |
| 81 | `rotate_head` | ignored | P3.3 |  |
| 82 | `section_blocks_update` | **done** | - |  |
| 83 | `select_advancements_tab` | ignored | P11.1 |  |
| 84 | `server_data` | ignored | P1.26 |  |
| 85 | `set_action_bar_text` | ignored | P8.17 |  |
| 86 | `set_border_center` | ignored | P6.20 |  |
| 87 | `set_border_lerp_size` | ignored | P6.20 |  |
| 88 | `set_border_size` | ignored | P6.20 |  |
| 89 | `set_border_warning_delay` | ignored | P6.20 |  |
| 90 | `set_border_warning_distance` | ignored | P6.20 |  |
| 91 | `set_camera` | ignored | P8.25 |  |
| 92 | `set_chunk_cache_center` | ignored | P0.4 | Named in the id table, never handled. |
| 93 | `set_chunk_cache_radius` | ignored | P0.4 | Named in the id table, never handled. |
| 94 | `set_cursor_item` | ignored | P1.7 | Named in the id table, never handled. |
| 95 | `set_default_spawn_position` | ignored | P11.10 |  |
| 96 | `set_display_objective` | ignored | P8.13 |  |
| 97 | `set_entity_data` | ignored | P1.8 | The metadata codec. Every entity appearance depends on it. |
| 98 | `set_entity_link` | ignored | P3.9 |  |
| 99 | `set_entity_motion` | ignored | P3.4 |  |
| 100 | `set_equipment` | ignored | P3.40 | Armour and held items on other entities. |
| 101 | `set_experience` | **done** | - |  |
| 102 | `set_health` | *partial* | P2.21 | Read, but on zero it auto-respawns instead of showing the death screen. |
| 103 | `set_held_slot` | **done** | - |  |
| 104 | `set_objective` | ignored | P8.13 |  |
| 105 | `set_passengers` | ignored | P3.9 |  |
| 106 | `set_player_inventory` | ignored | P1.7 | Named in the id table, never handled. |
| 107 | `set_player_team` | ignored | P8.14 |  |
| 108 | `set_score` | ignored | P8.13 |  |
| 109 | `set_simulation_distance` | ignored | P11.12 |  |
| 110 | `set_subtitle_text` | ignored | P8.16 |  |
| 111 | `set_time` | ignored | P1.17 | 30 minutes of work; the whole day/night cycle hangs off it. |
| 112 | `set_title_text` | ignored | P8.16 |  |
| 113 | `set_titles_animation` | ignored | P8.16 |  |
| 114 | `sound_entity` | ignored | P5.8 |  |
| 115 | `sound` | ignored | P5.7 |  |
| 116 | `start_configuration` | **done** | - |  |
| 117 | `stop_sound` | ignored | P5.9 |  |
| 118 | `store_cookie` | ignored | P1.23 |  |
| 119 | `system_chat` | **done** | - | Text is stripped to plain; P10.9 renders its formatting. |
| 120 | `tab_list` | ignored | P8.12 | The tab list header and footer. |
| 121 | `tag_query` | ignored | P11.8 |  |
| 122 | `take_item_entity` | ignored | P3.76 |  |
| 123 | `teleport_entity` | ignored | P3.2 |  |
| 124 | `test_instance_block_status` | out of scope | P11.20 | Dev-server only; handle without falling over. |
| 125 | `ticking_state` | ignored | P1.27 |  |
| 126 | `ticking_step` | ignored | P1.27 |  |
| 127 | `transfer` | ignored | P1.23 |  |
| 128 | `update_advancements` | ignored | P11.1 |  |
| 129 | `update_attributes` | ignored | P1.10 | Reach, speed, attack speed, knockback resistance. |
| 130 | `update_mob_effect` | ignored | P1.12 |  |
| 131 | `update_recipes` | ignored | P1.25 |  |
| 132 | `update_tags` | ignored | P1.13 |  |
| 133 | `projectile_power` | ignored | P11.11 |  |
| 134 | `custom_report_details` | ignored | P1.26 |  |
| 135 | `server_links` | ignored | P1.26 |  |
| 136 | `waypoint` | ignored | P8.19 | The locator bar, new in 1.21.9. |
| 137 | `clear_dialog` | ignored | P7.25 |  |
| 138 | `show_dialog` | ignored | P7.25 |  |

### play / serverbound — 66 packets: 10 done, 6 partial, 48 ignored, 2 out of scope

| id | packet | state | owner | note |
|---:|---|---|---|---|
| 0 | `accept_teleportation` | **done** | - |  |
| 1 | `block_entity_tag_query` | ignored | P11.8 |  |
| 2 | `bundle_item_selected` | ignored | P7.21 |  |
| 3 | `change_difficulty` | ignored | P1.15 |  |
| 4 | `change_game_mode` | ignored | P8.24 |  |
| 5 | `chat_ack` | ignored | P8.9 | Named in the id table, never sent. |
| 6 | `chat_command` | **done** | - | Sent unsigned. |
| 7 | `chat_command_signed` | ignored | P8.9 |  |
| 8 | `chat` | *partial* | P8.9 | Sent with salt 0 and no signature. A server with enforce-secure-chat=true disconnects this client. |
| 9 | `chat_session_update` | ignored | P8.9 | The session public key; required before any signed chat. |
| 10 | `chunk_batch_received` | **done** | - | Reports a fixed 32 chunks per tick. |
| 11 | `client_command` | *partial* | P2.21 | Only "perform respawn" is sent, and automatically. Mode 1 (request stats) is never sent. |
| 12 | `client_tick_end` | **done** | - |  |
| 13 | `client_information` | *partial* | P9.3 | Hardcoded. |
| 14 | `command_suggestion` | ignored | P8.10 |  |
| 15 | `configuration_acknowledged` | **done** | - |  |
| 16 | `container_button_click` | ignored | P7.14 |  |
| 17 | `container_click` | ignored | P7.5 | Named in the id table, never sent. The inventory is read-only. |
| 18 | `container_close` | ignored | P7.1 | Named in the id table, never sent. |
| 19 | `container_slot_state_changed` | ignored | P7.22 |  |
| 20 | `cookie_response` | ignored | P1.23 |  |
| 21 | `custom_payload` | ignored | P1.22 | Named in the id table, never sent. |
| 22 | `debug_subscription_request` | ignored | P9.19 |  |
| 23 | `edit_book` | ignored | P7.19 |  |
| 24 | `entity_tag_query` | ignored | P11.8 |  |
| 25 | `interact` | ignored | P4.10 | The client cannot attack or interact with any entity at all. |
| 26 | `jigsaw_generate` | ignored | P11.18 |  |
| 27 | `keep_alive` | **done** | - |  |
| 28 | `lock_difficulty` | ignored | P1.15 |  |
| 29 | `move_player_pos` | ignored | P4.27 | Named in the id table, never sent; pos_rot is always used instead. |
| 30 | `move_player_pos_rot` | **done** | - | Sent every 50 ms with on_ground hardcoded false. |
| 31 | `move_player_rot` | ignored | P4.27 |  |
| 32 | `move_player_status_only` | ignored | P4.27 |  |
| 33 | `move_vehicle` | ignored | P11.16 |  |
| 34 | `paddle_boat` | ignored | P11.17 |  |
| 35 | `pick_item_from_block` | ignored | P9.16 |  |
| 36 | `pick_item_from_entity` | ignored | P9.16 |  |
| 37 | `ping_request` | ignored | P11.13 |  |
| 38 | `place_recipe` | ignored | P7.9 |  |
| 39 | `player_abilities` | ignored | P1.11 | The client never tells the server it started flying. |
| 40 | `player_action` | *partial* | P4.5 | Start and finish dig only, face hardcoded to 1. Drop item, drop stack, shoot arrow, swap offhand and the finish-using-item actions are never sent. |
| 41 | `player_command` | ignored | P4.25 | Sprint, sneak, leave bed, horse jump, elytra. The server is never told this client is sprinting. |
| 42 | `player_input` | ignored | P4.26 | The per-tick input bitfield vanilla has sent since 1.21.2. |
| 43 | `player_loaded` | **done** | - |  |
| 44 | `pong` | **done** | - |  |
| 45 | `recipe_book_change_settings` | ignored | P7.12 |  |
| 46 | `recipe_book_seen_recipe` | ignored | P7.12 |  |
| 47 | `rename_item` | ignored | P7.13 |  |
| 48 | `resource_pack` | ignored | P10.3 | Only the configuration-phase version is sent. |
| 49 | `seen_advancements` | ignored | P11.1 |  |
| 50 | `select_trade` | ignored | P7.16 |  |
| 51 | `set_beacon` | ignored | P7.17 |  |
| 52 | `set_carried_item` | **done** | - |  |
| 53 | `set_command_block` | ignored | P7.23 |  |
| 54 | `set_command_minecart` | ignored | P7.23 |  |
| 55 | `set_creative_mode_slot` | ignored | P7.8 |  |
| 56 | `set_jigsaw_block` | ignored | P7.23 |  |
| 57 | `set_structure_block` | ignored | P7.23 |  |
| 58 | `set_test_block` | out of scope | P11.20 | Dev-server only. |
| 59 | `sign_update` | ignored | P7.20 |  |
| 60 | `swing` | *partial* | P4.28 | Always hand 0. |
| 61 | `teleport_to_entity` | ignored | P8.25 |  |
| 62 | `test_instance_block_action` | out of scope | P11.20 | Dev-server only. |
| 63 | `use_item_on` | *partial* | P4.5 | Face hardcoded to 1 and cursor to (0.5, 1.0, 0.5), so placing against a wall is wrong. |
| 64 | `use_item` | ignored | P4.29 | Eating, drinking, bows, shields, throwing: none of it works. |
| 65 | `custom_click_action` | ignored | P7.25 |  |

---

## Appendix B — every GUI sprite

**Why this table exists.** A sprite nothing claims is a feature nobody has
noticed. `assets/minecraft/textures/gui/` is Mojang's own enumeration of every
screen and HUD element in the game, and it is the one place where a feature you
have never heard of shows up as a file.

It worked. Walking it turned up `sprites/hud/locator_bar_*` and
`sprites/hud/locator_bar_dot/` — the locator bar, new in 1.21.9, driven by a
`waypoint` packet neither of us would have listed (now P8.19) — and
`sprites/dialog/`, the server-defined dialog screen system (now P7.25).

**The encouraging half.** `tools/bake_gui.py` already bakes every one of these
into a 16.8 MB atlas with the names kept. The sprites are on the GPU. 15 of them
are drawn.

**565 files in 53 directories** under `assets/minecraft/textures/gui/` (523 `.png` + 42 `.png.mcmeta`). 27 belong to deliberately excluded features (Realms, singleplayer world list, superflat presets). Every directory has an owner.

| directory | files | owner | what claims it |
|---|---:|---|---|
| `(root)/` | 12 | P2.12, P2.9, P7.12 | Menu backgrounds, list backgrounds, header/footer separators and their in-world variants, the book sheet, the recipe-book sheet, the tab header |
| `advancements/` | 1 | P11.1 | The advancement window sheet |
| `advancements/backgrounds/` | 5 | P11.1 | The 5 per-tab tiled backgrounds |
| `container/` | 23 | P7.2 | The 23 legacy full-sheet container backgrounds, blitted by uv |
| `container/creative_inventory/` | 3 | P7.8 | The 3 legacy creative tab sheets |
| `hanging_signs/` | 12 | P7.20 | The 12 hanging-sign editing backgrounds |
| `presets/` | 1 | EXCLUDED | Superflat presets — singleplayer, section 3 |
| `realms/` | 9 | EXCLUDED | Realms — section 3 |
| `sprites/advancements/` | 36 | P11.1 | Advancements screen: frame types x obtained, connectors, tabs |
| `sprites/boss_bar/` | 22 | P8.15 | Boss bars: 6 colours x background/progress, 5 notch overlays |
| `sprites/container/` | 5 | P7.2 | Shared container chrome: the slot highlight and the blocked slot |
| `sprites/container/anvil/` | 3 | P7.13 | Anvil screen |
| `sprites/container/beacon/` | 6 | P7.17 | Beacon screen |
| `sprites/container/blast_furnace/` | 2 | P7.10 | Blast furnace progress and flame |
| `sprites/container/brewing_stand/` | 3 | P7.15 | Brewing stand bubbles, arrow and fuel |
| `sprites/container/bundle/` | 12 | P7.21 | Bundle tooltip grid and fill bar |
| `sprites/container/cartography_table/` | 5 | P7.18 | Cartography table |
| `sprites/container/crafter/` | 3 | P7.18 | Crafter slot enable/disable |
| `sprites/container/creative_inventory/` | 30 | P7.8 | Creative inventory tabs and scroller |
| `sprites/container/enchanting_table/` | 9 | P7.14 | Enchanting table: 3 slots x states, level pips |
| `sprites/container/furnace/` | 2 | P7.10 | Furnace progress and flame |
| `sprites/container/grindstone/` | 1 | P7.18 | Grindstone |
| `sprites/container/horse/` | 1 | P7.24 | Horse inventory |
| `sprites/container/inventory/` | 4 | P7.7 | Inventory screen: effect panels and the recipe-book button |
| `sprites/container/loom/` | 6 | P7.18 | Loom pattern list |
| `sprites/container/slot/` | 30 | P7.2 | The 30 typed empty-slot icons (helmet, shield, saddle, hoe, ingot, dye...) |
| `sprites/container/smithing/` | 1 | P7.18 | Smithing table error slot |
| `sprites/container/smoker/` | 2 | P7.10 | Smoker progress and flame |
| `sprites/container/stonecutter/` | 5 | P7.18 | Stonecutter recipe buttons and scroller |
| `sprites/container/villager/` | 9 | P7.16 | Villager trading: offer arrows, out-of-stock, xp bar, scroller |
| `sprites/dialog/` | 3 | P7.25 | The server-defined dialog system |
| `sprites/gamemode_switcher/` | 2 | P8.24 | F4 game-mode switcher |
| `sprites/hud/` | 35 | P8.1-P8.21 | The HUD. 11 of 35 are used today; see the per-sprite list below |
| `sprites/hud/heart/` | 47 | P8.2 | Health. 3 of 47 are used today |
| `sprites/hud/locator_bar_dot/` | 5 | P8.19 | Locator bar player dots |
| `sprites/icon/` | 25 | P2.14, P8.18 | Server-list and toast icons: ping bars, lock, chat state, music notes, trial |
| `sprites/notification/` | 6 | P8.18 | Toast notification backgrounds |
| `sprites/pending_invite/` | 4 | EXCLUDED | Realms invites — section 3 |
| `sprites/player_list/` | 3 | P8.12 | Tab list latency bars |
| `sprites/popup/` | 2 | P2.24 | The generic popup frame |
| `sprites/realm_status/` | 5 | EXCLUDED | Realms — section 3 |
| `sprites/recipe_book/` | 30 | P7.12 | Recipe book: tabs, pages, filter, ghost overlay |
| `sprites/server_list/` | 18 | P2.14 | Server list: move arrows, join, ping, incompatible, unreachable |
| `sprites/social_interactions/` | 9 | P11.4 | Social interactions: mute/unmute, hide, search, tabs |
| `sprites/spectator/` | 5 | P8.25 | Spectator HUD |
| `sprites/statistics/` | 9 | P11.3 | Statistics screen sort arrows and headers |
| `sprites/toast/` | 15 | P8.18, P11.2 | Toasts: advancement x3, recipe, system, tutorial — per-side nine-slice {17,30,4,4} |
| `sprites/tooltip/` | 4 | P2.11 | Tooltip frame, nine-slice border 9, stretch_inner |
| `sprites/transferable_list/` | 8 | P1.23 | Server-transfer list |
| `sprites/widget/` | 47 | P2.4-P2.10 | Every widget: button, slider, checkbox, tab, text field, scroller, page arrows, locked button |
| `sprites/world_list/` | 8 | EXCLUDED | Singleplayer world list — section 3 |
| `title/` | 5 | P2.13 | Title screen: the logo, the edition line, the Mojang Studios splash |
| `title/background/` | 7 | P2.13 | The 6 panorama faces and the overlay. NOTE: 1x1 stubs in the jar; the real faces are object-store only (P2.1) |

### `sprites/hud/` per sprite — 11 used of 35

| sprite | used today | owner |
|---|---|---|
| `air.png` | no | P8.4 |
| `air_bursting.png` | no | P8.4 |
| `air_empty.png` | no | P8.4 |
| `armor_empty.png` | **yes** | P8.1 (in use) |
| `armor_full.png` | **yes** | P8.1 (in use) |
| `armor_half.png` | **yes** | P8.1 (in use) |
| `crosshair.png` | **yes** | P8.1 (in use) |
| `crosshair_attack_indicator_background.png` | no | P4.9 |
| `crosshair_attack_indicator_full.png` | no | P4.9 |
| `crosshair_attack_indicator_progress.png` | no | P4.9 |
| `effect_background.png` | no | P8.7 |
| `effect_background_ambient.png` | no | P8.7 |
| `experience_bar_background.png` | **yes** | P8.1 (in use) |
| `experience_bar_progress.png` | **yes** | P8.1 (in use) |
| `food_empty.png` | **yes** | P8.1 (in use) |
| `food_empty_hunger.png` | no | P8.3 |
| `food_full.png` | **yes** | P8.1 (in use) |
| `food_full_hunger.png` | no | P8.3 |
| `food_half.png` | **yes** | P8.1 (in use) |
| `food_half_hunger.png` | no | P8.3 |
| `hotbar.png` | **yes** | P8.1 (in use) |
| `hotbar_attack_indicator_background.png` | no | P4.9 |
| `hotbar_attack_indicator_progress.png` | no | P4.9 |
| `hotbar_offhand_left.png` | no | P8.6 |
| `hotbar_offhand_right.png` | no | P8.6 |
| `hotbar_selection.png` | **yes** | P8.1 (in use) |
| `jump_bar_background.png` | no | P8.5 |
| `jump_bar_cooldown.png` | no | P8.5 |
| `jump_bar_progress.png` | no | P8.5 |
| `locator_bar_arrow_down.png` | no | P8.19 |
| `locator_bar_arrow_down.png.mcmeta` | no | P8.19 |
| `locator_bar_arrow_up.png` | no | P8.19 |
| `locator_bar_arrow_up.png.mcmeta` | no | P8.19 |
| `locator_bar_background.png` | no | P8.19 |
| `locator_bar_background.png.mcmeta` | no | P8.19 |

### `sprites/hud/heart/` — 3 used of 47

All 47 are owned by **P8.2**. The matrix is: *normal, poisoned, withered, frozen, absorbing, vehicle* x *full, half* x *hardcore, not* x *blinking, not*, plus four container forms. Used today: `container`, `full`, `half`. Not used: the other 44, which means no poison tint, no wither tint, no freeze, no absorption row, no hardcore hearts and no low-health blink.

`absorbing_full`, `absorbing_full_blinking`, `absorbing_half`, `absorbing_half_blinking`, `absorbing_hardcore_full`, `absorbing_hardcore_full_blinking`, `absorbing_hardcore_half`, `absorbing_hardcore_half_blinking`, `container`, `container_blinking`, `container_hardcore`, `container_hardcore_blinking`, `frozen_full`, `frozen_full_blinking`, `frozen_half`, `frozen_half_blinking`, `frozen_hardcore_full`, `frozen_hardcore_full_blinking`, `frozen_hardcore_half`, `frozen_hardcore_half_blinking`, `full`, `full_blinking`, `half`, `half_blinking`, `hardcore_full`, `hardcore_full_blinking`, `hardcore_half`, `hardcore_half_blinking`, `poisoned_full`, `poisoned_full_blinking`, `poisoned_half`, `poisoned_half_blinking`, `poisoned_hardcore_full`, `poisoned_hardcore_full_blinking`, `poisoned_hardcore_half`, `poisoned_hardcore_half_blinking`, `vehicle_container`, `vehicle_full`, `vehicle_half`, `withered_full`, `withered_full_blinking`, `withered_half`, `withered_half_blinking`, `withered_hardcore_full`, `withered_hardcore_full_blinking`, `withered_hardcore_half`, `withered_hardcore_half_blinking`

### `sprites/widget/` — 0 used of 47

All 47 are owned by **P2.4** to **P2.10**, and **none are drawn today** because there are no widgets. The 10 with `.png.mcmeta` carry the nine-slice data P0.13 adds: `button` (200x20, border 3), `button_disabled`, `button_highlighted`, `slider`, `slider_highlighted`, `slider_handle`, `slider_handle_highlighted`, `scroller`, `scroller_background`, `tab` family (per-side border {2,2,2,0}), `text_field`, `text_field_highlighted`.

`button`, `button.mcmeta`, `button_disabled`, `button_disabled.mcmeta`, `button_highlighted`, `button_highlighted.mcmeta`, `checkbox`, `checkbox_highlighted`, `checkbox_selected`, `checkbox_selected_highlighted`, `cross_button`, `cross_button_highlighted`, `locked_button`, `locked_button_disabled`, `locked_button_highlighted`, `page_backward`, `page_backward_highlighted`, `page_forward`, `page_forward_highlighted`, `scroller`, `scroller.mcmeta`, `scroller_background`, `scroller_background.mcmeta`, `slider`, `slider.mcmeta`, `slider_handle`, `slider_handle.mcmeta`, `slider_handle_highlighted`, `slider_handle_highlighted.mcmeta`, `slider_highlighted`, `slider_highlighted.mcmeta`, `slot_frame`, `tab`, `tab.mcmeta`, `tab_highlighted`, `tab_highlighted.mcmeta`, `tab_selected`, `tab_selected.mcmeta`, `tab_selected_highlighted`, `tab_selected_highlighted.mcmeta`, `text_field`, `text_field.mcmeta`, `text_field_highlighted`, `text_field_highlighted.mcmeta`, `unlocked_button`, `unlocked_button_disabled`, `unlocked_button_highlighted`

---

## Appendix C — every language key prefix

**Why this table exists.** `assets/minecraft/lang/en_us.json` names every button,
option, message, screen title and sound subtitle in the game. 7,767 keys. A
prefix with no owner in the plan is a system nobody has thought about. It is the
closest thing to a complete feature index that Mojang publishes, and it is a
better completeness check than any list written from memory.

It worked here too. The prefixes that would not have been on a list from
recollection: `known_server_link.` (10 standard server link types for the pause
menu), `quickplay.` (the `--quickPlayMultiplayer` command-line entry),
`locator_bar` inside `options.`, `chat_screen.`, `translation.` (the fallback
rendered when a key is missing), `chunk.` (chunk status names for F3),
`component.` (data component parse errors) and `event.` (seasonal strings).

Note what the counts say about where the work is: `options.` alone is 283 keys,
`key.` is 164, `container.` is 42 and `gui.` is 208. Those four prefixes are
roughly the whole of Phases 2, 7 and 9, and they can be used as a literal
checklist for those phases.

**116 top-level prefixes over 7767 keys.** 789 keys belong to deliberately excluded features. Every prefix has an owner.

| prefix | keys | owner | what it is |
|---|---:|---|---|
| `block.` | 1854 | P1.6, P7.4 | Block names, for tooltips, the creative inventory and the F3 screen. |
| `subtitles.` | 968 | P5.15 | Closed captions. 1,740 of the 1838 sound events have one. |
| `item.` | 781 | P1.6, P7.4 | Item names, for tooltips and every slot. |
| `commands.` | 580 | P8.11 | Command feedback and errors, rendered as chat text components. |
| `mco.` | 353 | EXCLUDED | Realms. Section 3. 353 keys — the single largest excluded block. |
| `options.` | 283 | P9.4-P9.8, P10.11 | Every option. 283 keys = the complete options-screen specification. |
| `advancements.` | 258 | P11.1 | Advancement names and descriptions. |
| `entity.` | 214 | P1.20, P3.27 | Entity names, for nametags, death messages and the statistics screen. |
| `gui.` | 208 | P2.*, P11.4 | Shared UI strings. 66 abuseReport + 26 chatReport + 27 banned + 7 chatSelection are EXCLUDED (section 3); the rest is the framework, the recipe book and social interactions. |
| `key.` | 164 | P9.2 | The complete keybind list: 59 gameplay/UI + 9 hotbar + 21 debug, in 8 categories, plus 85 key.keyboard.* and the mouse names. |
| `selectWorld.` | 123 | EXCLUDED | Singleplayer world list and creation. Section 3. |
| `argument.` | 113 | P8.11 | Command argument parse errors. |
| `gamerule.` | 108 | EXCLUDED | Game-rule editing is a singleplayer/LAN screen. Section 3. The client still reads game rules from game_event (P1.16). |
| `painting.` | 104 | P11.6 | 102 painting variant names. |
| `death.` | 103 | P1.3, P2.21 | Death messages, built from the damage_type registry. 91 attack forms + 12 fall forms. |
| `multiplayer.` | 93 | P2.14-P2.22 | The server list, the 41 disconnect reasons, the pack prompts, the player join/leave lines. |
| `stat.` | 80 | P11.3 | The statistics screen: 77 custom stats plus the 3 headers. |
| `attribute.` | 78 | P1.10, P7.4 | Attribute names, for tooltips and the F3 screen. |
| `biome.` | 65 | P9.9 | Biome names, for the F3 screen. |
| `music.` | 65 | P5.12 | The 31 music events. |
| `debug.` | 61 | P9.9 | The F3 screen and its 21 chords. |
| `enchantment.` | 54 | P1.5, P7.4 | Enchantment names and the 10 level numerals. |
| `telemetry.` | 46 | EXCLUDED | Section 3. Replaced by a statement that this client sends none. |
| `test.` | 45 | P11.20 | Mojang game-test harness. Parked; the client must not fall over. |
| `chat.` | 43 | P8.8 | Chat: the 11 chat types, the link prompt, the copy action, the queue, the disabled states. |
| `container.` | 42 | P7.1-P7.21 | All 25 container titles plus their in-screen strings. |
| `effect.` | 42 | P1.12, P8.7 | The 40 mob effect names. |
| `structure_block.` | 41 | P7.23 | The structure block screen. Op-gated. |
| `menu.` | 35 | P2.13, P2.20 | The title screen and the pause menu, plus the dialog system notices (P7.25). |
| `narration.` | 29 | P2.25, P10.12 | Widget narration strings. |
| `realms.` | 26 | EXCLUDED | Realms. Section 3. |
| `dataPack.` | 23 | P10.4 | The data-pack half of the pack selection screen. |
| `narrator.` | 23 | P10.12 | Narrator modes and announcements. |
| `pack.` | 23 | P10.4 | Resource and data pack selection: sources, incompatibility, drop handling. |
| `arguments.` | 21 | P8.11 | Command argument errors. |
| `jukebox_song.` | 21 | P11.7 | The 21 jukebox song descriptions. |
| `snbt.` | 21 | P8.11 | SNBT parse errors in command arguments. |
| `advMode.` | 19 | P7.23 | The command block screen. Op-gated. |
| `createWorld.` | 19 | EXCLUDED | Singleplayer world creation. Section 3. |
| `demo.` | 19 | EXCLUDED | Demo mode. Section 3. |
| `optimizeWorld.` | 19 | EXCLUDED | Singleplayer world optimisation. Section 3. |
| `trim_pattern.` | 18 | P3.43 | The 18 armour trim patterns. |
| `book.` | 17 | P7.19 | The book reading and editing screens. |
| `color.` | 16 | P6.6 | The 16 dye colours, for banners, beds, wool, shulkers and collars. |
| `filled_map.` | 16 | P11.5 | Map names and scales. |
| `itemGroup.` | 16 | P7.8 | The 14 creative inventory tabs plus search and hotbar. |
| `tutorial.` | 16 | P8.26 | The 8 tutorial hints as toasts. |
| `disconnect.` | 15 | P2.19 | Generic disconnect reasons. |
| `recover_world.` | 15 | EXCLUDED | Singleplayer world recovery. Section 3. |
| `jigsaw_block.` | 14 | P7.23 | The jigsaw block screen. Op-gated. |
| `parsing.` | 14 | P8.11 | Command parse errors. |
| `title.` | 13 | P2.13 | Title screen strings and the 32-bit warning. |
| `test_block.` | 12 | P11.20 | Section 3 / parked. |
| `test_instance.` | 12 | P11.20 | Section 3 / parked. |
| `connect.` | 11 | P2.18 | The 11 connection states, which are exactly the loading screen. |
| `generator.` | 11 | EXCLUDED | World generator types. Singleplayer, section 3. |
| `selectServer.` | 11 | P2.14 | The server list buttons. |
| `soundCategory.` | 11 | P5.5 | The 11 sound categories. |
| `trim_material.` | 11 | P3.43 | The 11 trim materials. |
| `flat_world_preset.` | 10 | EXCLUDED | Superflat presets. Singleplayer, section 3. |
| `known_server_link.` | 10 | P1.26 | The 10 standard server link types for the pause menu. |
| `stat_type.` | 10 | P11.3 | The 9 statistic types. |
| `test_instance_block.` | 10 | P11.20 | Section 3 / parked. |
| `lanServer.` | 9 | EXCLUDED | Open to LAN. Needs an integrated server, section 3. |
| `potion.` | 9 | P1.5, P7.4 | Potion names. |
| `resourcePack.` | 9 | P10.2 | The built-in pack names. |
| `team.` | 9 | P8.14 | Team display strings. |
| `addServer.` | 8 | P2.16 | The add/edit server screen. |
| `command.` | 8 | P8.11 | Command execution messages. |
| `deathScreen.` | 8 | P2.21 | The death screen. 8 keys = the whole screen. |
| `instrument.` | 8 | P5.16 | The 8 note-block instruments. |
| `manageServer.` | 8 | EXCLUDED | Realms. Section 3. |
| `merchant.` | 8 | P7.16 | Villager trading levels. |
| `spectatorMenu.` | 8 | P8.25 | The spectator menu. |
| `symlink_warning.` | 7 | EXCLUDED | Singleplayer world symlink warning. Section 3. |
| `telemetry_info.` | 7 | EXCLUDED | Section 3. |
| `translation.` | 7 | P10.9 | The fallback shown when a translation key is missing or malformed. |
| `controls.` | 6 | P9.6 | The controls screen and the Key Binds sub-screen. |
| `event.` | 6 | P5.12 | Seasonal event strings. |
| `gameMode.` | 6 | P8.24 | The 6 game mode names. |
| `chunk.` | 5 | P9.9 | Chunk status names for the F3 chunk display. |
| `credits_and_attribution.` | 4 | P11.19 | The credits screen. |
| `datapackFailure.` | 4 | EXCLUDED | Singleplayer datapack failure. Section 3. |
| `download.` | 4 | P10.3 | Resource pack download progress. |
| `quickplay.` | 4 | P2.17 | The quick-play command-line entry (--quickPlayMultiplayer). |
| `accessibility.` | 3 | P10.11 | The accessibility onboarding screen. |
| `chat_screen.` | 3 | P8.8 | Chat screen narration and usage. |
| `compliance.` | 3 | EXCLUDED | Regional compliance notices tied to a Mojang account service. Section 3. |
| `inventory.` | 3 | P7.7 | The inventory screen: the title, the binslot and the hotbar hint. |
| `language.` | 3 | P10.8 | The language screen. |
| `mirror.` | 3 | P7.23 | Structure block mirror modes. Op-gated. |
| `multiplayerWarning.` | 3 | P2.24 | The "online play is not moderated by Mojang" notice. |
| `recipe.` | 3 | P11.2 | The recipe unlocked toast. |
| `resourcepack.` | 3 | P10.3 | Resource pack application progress. |
| `sleep.` | 3 | P8.22 | The sleeping overlay and the skip-night vote. |
| `clear.` | 2 | P8.11 | The /clear command feedback. |
| `difficulty.` | 2 | P1.15, P9.4 | Difficulty strings. |
| `editGamerule.` | 2 | EXCLUDED | Game rule editing. Singleplayer, section 3. |
| `outOfMemory.` | 2 | P2.24 | The out-of-memory screen. |
| `particle.` | 2 | P4.19 | Particle parse errors in commands. |
| `permissions.` | 2 | P8.11 | Permission denial messages. |
| `screenshot.` | 2 | P9.13 | The screenshot confirmation with its click-to-open link. |
| `slot.` | 2 | P7.2 | Slot names for commands and tooltips. |
| `advancement.` | 1 | P11.1 | The advancement root string. |
| `build.` | 1 | P9.9 | The build identifier on the F3 screen. |
| `component.` | 1 | P1.5 | Data component parse errors. |
| `hanging_sign.` | 1 | P7.20 | The hanging sign editing screen. |
| `item_modifier.` | 1 | P8.11 | Item modifier command errors. |
| `lectern.` | 1 | P7.19 | The lectern screen. |
| `loading.` | 1 | P2.18 | The loading screen. |
| `mount.` | 1 | P7.24 | The mount screen. |
| `predicate.` | 1 | P8.11 | Predicate command errors. |
| `record.` | 1 | P11.7 | The jukebox "Now Playing" action-bar line. |
| `sign.` | 1 | P7.20 | The sign editing screen. |
| `stats.` | 1 | P11.3 | The statistics screen title. |
| `upgrade.` | 1 | P7.18 | The smithing table upgrade strings. |

---

## Appendix D — what cannot be read out of the jar

Everything else in this plan comes from a file. These seven things do not, and
each one needs a decision about where the numbers come from. This is the
appendix to read before Phase 3 starts, because P3.11 blocks eleven weeks of
work behind it.

### D1. Entity geometry — the big one

**The fact.** `assets/minecraft/models/` contains exactly two subdirectories,
`block/` and `item/`. **`models/entity/` does not exist.** Zero entries.
Confirmed by listing the jar. Every one of the ~90 mob models, every block-entity
model (chest, sign, bed, conduit, shulker, bell, banner, decorated pot, boat,
dragon), every armour layer model and every animation is compiled Java in the
client jar.

The 631 textures under `textures/entity/` are present, so the *skins* are
readable. Only the geometry is not.

`assets/minecraft/items/` also names 11 items whose model is
`minecraft:special`, meaning Mojang hand-builds it rather than reading JSON:
chest, bed, banner, shulker_box, copper_golem_statue, head, player_head,
trident, shield, conduit, decorated_pot. **That is Mojang's own published list of
models a client has to hand-build just to draw items in a slot**, and it
overlaps exactly with the block-entity list, which is why P4.13 and P6.8 share
the work.

**Route A — hand-measure from the UV layout.** Mojang's cube unwrap is
deterministic: a box of size (w, h, d) occupies a texture island of exactly
(2w + 2d) by (h + d) pixels, laid out in a fixed arrangement. So **the box
dimensions are recoverable from the texture, not guessed at.** A 64x64 player
skin's head island is 32x16, which is the only (w,h,d) = (8,8,8) solution; the
body island is 24x16, which is (8,12,4). That recovers every box's size for
every entity, from a file the player owns, with no reference to Mojang's code.

What the UV layout does *not* give you is each part's **pivot and offset** — the
position of the shoulder joint, the hip, the neck — because those are not
encoded in the texture. Those have to be measured by eye against the vanilla
client and iterated: render, screenshot, overlay, adjust. For the humanoid
model that is defensible (there is one, everyone knows what it looks like, and
`plasma-client entityshot` from P3.14 is exactly the tool for it). For 90 mobs it
is weeks of eyeballing and it will never be exact.

- **Cost:** 3 d for the player model, about 6 w for the rest, and the result is
  approximate for everything but the player.
- **Clean-room:** unimpeachable.

**Route B — an install-time exporter.** Compile a small Java class against the
player's own client jar, run it under a JDK at install time, have it walk the
baked model tree and serialise every part's pivot, rotation, visibility and
boxes to JSON. Bake that into P3.10's format. The exporter never ships; its
output never ships; it runs on the player's machine against the jar they own.

This is **exactly the same category as `--reports`**, which this project already
depends on and which `docs/ARCHITECTURE.md` already defends: *"That is observable
output of the game, not decompiled code."* An exporter that asks the game what
its own model tree is, at install time, on the player's machine, is observable
output of the game by the same argument. It reads data, not source. Nothing from
Mojang enters the repository or the download.

- **Cost:** 2 d for the exporter and the bake step, and the result is **exact**
  for all ~90 mobs, all block entities, all armour layers and the keyframe
  animation definitions P3.62 needs.
- **Clean-room:** defensible on the same grounds as `--reports`, but it is a
  wider door than `--reports` and the decision should be made deliberately and
  written down, not drifted into. The honest distinction: `--reports` is a
  documented, supported Mojang feature invoked by a documented flag; an exporter
  is code you wrote that reflects into private fields of a jar you did not
  write. Both read only data. The second is not a supported interface.
- **Precedent:** this is what Horizon does, and their own documentation states
  the reason in one line — *"vanilla's own entity models are code in 26.3"*.
  Their exporter is 74 lines. They do not port the model builders; they ask the
  game and serialise the answer.

**Recommendation: Route B, with Route A's UV derivation kept as the
verification.** Build the exporter, and separately derive each box's dimensions
from its texture island. Where the two disagree, you have found a bug in one of
them. That gives exact geometry *and* an independent check on it, for 2 days
instead of 6 weeks, and the check is the part that makes the exporter trustworthy
rather than magic.

**Whichever route: build `plasma-client entityshot` (P3.14) first.**
`docs/ARCHITECTURE.md` already records that the missing-top-face bug survived a
passing test, a rendered frame and good timings, and was found only by looking at
a single block from a known angle. A 20-part skeletal model has twenty times the
surface for that mistake.

### D2. Entity dimensions

157 entity types, each with a width, a height and an eye height. **Not in
`registries.json`** — the registry gives the id and the name and nothing else.
Needed for hitboxes, the entity raycast, nametag placement and shadow size.
Hand-authored table of 157 rows (1 d), or the same exporter as D1 (free, since
`EntityType` carries its dimensions).

### D3. Collision shapes

Covered in P1.9. Not in any report; the block *models* give visual geometry which
coincides with collision for about 80% of blocks and not for the ones players
notice. Hand-authored over about 40 shape families: 3 d, and checkable by
standing in a doorway and walking a staircase beside the vanilla client.

### D4. Block hardness and tool tiers

Covered in P1.14. Destroy time per block and which tool tier is correct for it.
Public data, published in many places, not in any report. A hand-entered table of
1,166 rows most of which take a family default: 1 d. The multipliers that act on
it (Efficiency `level² + 1`, Haste `0.2·level + 1`, Mining Fatigue `÷5`, in-water
`÷5`, off-ground `÷5`, correct-tool `÷30` versus `÷100`) are also public.

### D5. Block sound groups

Covered in P5.10. Which of about 60 sound groups each block belongs to — stone,
wood, gravel, grass, metal, glass, wool, sand, snow, slime, honey, netherrack,
nylium, bamboo, deepslate, tuff, copper, amethyst, sculk, cherry wood, pale moss,
resin and the rest — plus each group's step, break, place, hit and fall events
with their volume and pitch. The 1838 sound *events* are in the registry; the
mapping from block to group is not. Hand table, 1 d.

### D6. Particle behaviour

`assets/minecraft/particles/` is 104 JSON files and each one contains only
`{"textures": [...]}`. **All particle physics — lifetime, gravity, drag, size
curve, colour curve, collision, the sprite-frame progression — is code.** The
texture frame lists are readable; nothing else is. P4.14 implements vanilla's
general particle behaviour from observation and public documentation, and
individual providers will be approximate. This is the one place in the plan where
"approximate" is the honest answer and the error is invisible: nobody can tell
whether a smoke particle's drag coefficient is right.

### D7. Entity animation

Two systems. The older one is per-mob `setupAnim` code, which is pure code and
has no data form. The newer one, from 1.19, is a declarative keyframe system
(bone channels, timestamps, values, interpolation modes) and **its definitions
are also compiled in**, not in the jar — which is why P3.62 is listed as needing
the same decision as D1. Route B's exporter gets the keyframe definitions as
data; Route A does not, and the keyframe-animated mobs (warden, breeze, camel,
armadillo, sniffer, creaking, copper golem, happy ghast, bogged) would have to be
animated by eye.

**The summary.** Of the seven, one (D1) is a decision worth taking seriously and
worth writing down once, and it unblocks D2 and D7 for free. Three (D3, D4, D5)
are hand-entered data tables totalling five days, all of them public data and all
of them checkable against the vanilla client by standing in the same place. One
(D6) is honestly approximate and the approximation does not matter. None of them
requires reading a line of Mojang's source.

---

## Appendix E — what the Horizon study is good for

`~/horizon-eigen` is a checkout of a Rust/wgpu from-scratch client solving the
same problem, targeting MC 26.3. It was read for this plan for **approach and
coverage only**; none of its code is reproduced here or anywhere in this project,
and nothing below is a transliteration. What follows is prose description of
method, which is what it is legitimate to take.

**Calibration first, because it changes how you read my estimates.** Horizon is
1,082,104 lines of Rust across 2,069 files, plus 108,518 lines of Python test
drivers, 46,110 lines of Java verification oracles and about 23,500 lines of
documentation. That is roughly 65 times Plasma's Rust-equivalent line count. It
includes a dedicated server and full world generation, both out of scope here, so
the right comparison is not the total. The right comparisons are these two:

1. **About 14% of its code is verification infrastructure.** Roughly one line of
   oracle and harness for every seven lines of game code. If you want its
   confidence level, that is the tax.
2. **Its own port ledger reports 833 player-visible gaps still open**, and the
   two largest buckets are 197 in "screens, HUD and client packets" and 129 in
   "entity and block rendering". At a million lines with 379 differential
   harnesses all passing. Those are exactly the areas Phases 2, 3, 7 and 8 cover,
   and that is the strongest available evidence that my estimates for those
   phases are low rather than high.

Do not calibrate the *schedule* against Horizon's calendar — every dated artifact
in that tree falls between 3 October and 2 November 2026, with a 22 KB agent
house-rules file, a supervisor that runs one git worktree per work area and a
harness runner that farms tests across four machines over ssh. It is an
agent-fleet project, not a human-year project. Calibrate against its ratios, not
its clock.

**Three methods worth borrowing, described in prose.**

*Install-time exporters.* Rather than porting Mojang's code for anything that is
code-not-data, they compile small Java classes against the player's own jars, run
them under a JDK at install time, and load the serialised output as data. Content
hashes over the exporter sources invalidate the bundle when the jar or the
exporter changes. This is D1's Route B and it is the single most consequential
idea in that repository for this project. Their entity-model exporter is 74 lines;
it bootstraps the game's shared constants and walks the baked model tree.

*Differential oracles.* For each subsystem, a driver runs the real vanilla class
on a set of inputs and runs their own implementation on the same inputs, and
demands identical traces. The two instances of this most applicable here: for
GUI, they discovered that 26.3 ships a graphics extractor that emits a *draw-call
list* instead of drawing, and compared ordered lists of blits, fills and text
calls against their layout code. For sound, they ran the real sound engine
against OpenAL Soft's "No Output" device and compared each voice's gain, pitch,
position, looping and distance model after each scripted step. Both are cheap
ways to be exact about something that otherwise has to be eyeballed. Both depend
on being able to run the game's own code at test time, which is the same decision
as D1.

*A coverage ledger rather than TODO comments.* They have essentially no TODO
markers in a million lines; the backlog lives in a tool that matches every
vanilla method to the function that ports it and reports the unmatched ones by
category. This document is the low-tech version of the same instinct, and the
three appendices are its enumerations.

**What they admit they have not done, which is the most useful part of the
survey**, because it marks where parity work is genuinely hard rather than merely
long. Their own documentation states, in their words:

- *"Real skins are not downloaded: every head uses its default skin"* and
  *"Skins served over https show the default skin unless already cached"*. At a
  million lines. **Test P3.21 against a real server with a real skin before
  calling it done.**
- *"worn trims aren't drawn yet"* (P3.43) and, on villagers, *"the type layer's
  hat is always drawn; vanilla hides it under a profession hat marked full or
  partial"* — which is the `villager` mcmeta flag P3.51 covers, and they got it
  wrong.
- On their meshing oracle: *"Not compared: block entities and their renderers,
  animated sprite frames, translucent sorting, chunk-border light outside loaded
  chunks and GPU pixels."* Four of those five are Phase 0 and Phase 6 items here.
- *"Horizon has no credits screen yet"*, *"the locator bar isn't drawn yet"*,
  *"Fonts: ttf — no — no TrueType rasteriser yet"*, *"a pack that changes the
  blur/clamp flags is ignored"*, *"the border is taken in texels, so HD GUI
  sprites get thin borders; stretch_inner and tile aren't read"* — that last one
  is the nine-slice detail P0.13 has to get right and they did not.
- *"Shader packs have been removed for now. They will come back later, written
  again from scratch."* And of the core shaders: *"core/text draws the GUI's
  text; the other core shaders aren't run."* P10.10 is correctly placed as the
  most deferrable item in this plan.
- *"item tooltips lack attribute lines"* — P7.4, and it is why I priced it at
  two days rather than four hours.
- On their own honesty framing, which is worth adopting verbatim as a policy:
  *"100% means 100% of what the harnesses compare, not that nothing else
  differs."* And: *"No finite corpus proves every vanilla behavior."*

**The one thing not to borrow.** They maintain a Vineflower decompilation of both
jars as a reading aid. That is outside this project's rule and there is no reason
to go near it: every gap in this document is sourced from `--reports`, from the
jar's data files, from the object store, from the public protocol documentation
or from bytes off a real server. The exporter question in D1 is the only place
where the line needs thinking about at all, and it is a question about reading
*data* out of a running jar, not about reading source.

---

*Written 3 November 2026 against protocol 774 (Minecraft 1.21.11). The reports
this is checked against were generated for it and are reproducible with the
command in section 1. When the target version moves, regenerate them and
re-run the three appendix enumerations before trusting any of this.*
