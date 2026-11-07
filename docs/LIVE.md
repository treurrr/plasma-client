# What a live session shows

Four waves of work were each proved by oracles and offscreen renders, and almost
none of it had ever run together in one session against a real server. This is
the list of what does not hold up when it does, found with
`plasma-client livetest`, and what is left to do about each one.

`livetest` is a session with no window: it builds a site on the vanilla test
server, joins it, and runs the client's own frame loop - the screen manager, the
raw input events, the one tick clock - with the keys, the clicks, the pointer
and glfw's clock coming from a script instead of a keyboard, writing frames to
PNG as it goes. Nothing is reached around: a phase presses W and the player
walks because the window reports W held, exactly as it does under a hand.

    export PLASMA_AUDIO=null PLASMA_CONSOLE=~/plasma-testserver/console.in
    ./build-m5/plasma-client livetest 127.0.0.1:25601 PlasmaL ~/plasma-work/live 1280 720
    # one phase only:
    ./build-m5/plasma-client livetest 127.0.0.1:25601 PlasmaL out 1280 720 rain

Each phase prints a line of state when it ends (position, health, held and
offhand stacks, entity count, whether a screen has the keyboard), and keeps the
frames it was told to keep, some of them from three more yaws so a thing can be
looked at from its sides as well as its front.

Everything below says plainly whether it was **seen** in a frame or only
**inferred** from the code. As of this writing **nothing has been seen**: the
harness was finished and the test server stopped before a single frame of a
live session was rendered. Every entry is inferred from reading the code, and
each one names what to put in front of the camera to confirm it.

## Fixed in this wave

- **Every key meant a fixed key, not its binding.** `Window::poll` read W, A,
  S, D, space, shift, control, T, Q, E and 1-9 off the keyboard directly, so
  Options -> Controls wrote `options.txt`, the Controls screen showed the new
  key, and the game went on reading the old one. The one binding that was
  honoured was `key.inventory`, which `screens_input` looks up properly, so the
  client was inconsistent with itself. `poll` now reads every gameplay field
  through `Keybinds`, falling back to the key the game ships that binding on
  when no registry is set. `tests/test_window.cpp` rebinds forward, attack,
  drop and a hotbar slot and checks each one follows; it fails 8 checks against
  the old `poll`.
- **Drop, swap-to-offhand and pick-block were never even reported.**
  `FrameInput` has `drop`, `drop_stack`, `swap_offhand`, `pick_item_clicks`,
  `attack_clicks` and `use_clicks`; `poll` set only `drop`, and nothing
  downstream acted on any of them. `poll` now fills all six. What acts on them
  is still missing - see below.
- **The chat line was a stand-in drawn by the hud.** `ChatScreen` - caret,
  history, the completions off the server's command tree, clickable lines - was
  finished and tested in `chattest`, and the live client never opened it: the
  chat key called `Window::begin_typing`, and `hud_render` drew
  `window.typed() + "_"` in a grey box with the comment "until the screens area
  draws its own". That path also dropped every character outside ASCII 32-126,
  so a Dutch keyboard could not type "ë" at all. The chat key now opens the
  real `ChatScreen`, handed the hud's own chat log through the new
  `hud_chat`/`hud_ticks` hooks, so the history and the scroll are the log the
  player is looking at. The window's typing path is gone from the client.

## Still open

Ranked by what a player notices first.

### 1. Nothing acts on drop, swap-offhand or pick-block

**Inferred.** `poll` now reports all three, and no area reads them.
`Session::send_dig_action` already carries the two drop actions (3 for one item,
4 for the stack) and `send_held_slot`/`send_container_click` can do the swap, so
this is wiring, not new protocol work:

- `key.drop` -> `send_dig_action(3 or 4, 0, 0, 0, face)` and predict the stack
  shrinking, `LocalPlayer.drop`.
- `key.swapOffhand` -> `send_dig_action(6, ...)`, `Minecraft.handleKeybinds`.
- `key.pickItem` -> `MultiPlayerGameMode.handlePickItem`: the aimed block's item
  into a free hotbar slot, creative-only for a block with data.

Belongs next to the other `MultiPlayerGameMode` work in
`src/client/game_mode.cpp`. To confirm: the `drop` phase of `livetest` presses
Q three times and reads the held stack's count out of the session afterwards;
today the count will not move and no item entity will appear in front of the
player.

### 2. The server list is unreachable, and `ConnectScreen` never says anything

**Inferred, from reading the whole title flow.** `title_screen` wires
`manager.host.multiplayer` to a bool and leaves the loop the moment Multiplayer
is pressed; `play.cpp` then connects to whatever was on the command line. So
`JoinMultiplayerScreen` - the list, the pings, the icons, the Add/Edit/Delete
flow, all built and tested - is reachable only from `screenshot`. Nothing sets
`manager.servers`, `host.connect` or `host.cancel_connect` either, so even if
the screen came up, Join would put up a `ConnectScreen` that sits on
"Connecting to the server…" forever: `ConnectScreen::status` is written by
nobody, and `Session`'s own `on_status` lines ("Connecting", "Encrypting",
"Joining") are printed to stdout instead.

The fix is one flow, not one line: the title screen's Multiplayer button sets
`JoinMultiplayerScreen`, `host.connect` starts the session on a thread, the
status lines go to `ConnectScreen::status`, a failure puts up
`DisconnectedScreen`, and `play.cpp`'s address argument becomes a shortcut that
presses Join for you.

### 3. Item particles never exist

**Inferred.** `ParticleOptions::has_texture` is set in exactly one place in the
tree, `tests/test_particles.cpp`. `make_particle` opens the four breaking-item
types with `if (!options.has_texture) return std::nullopt`, so eating, a
snowball landing, a slimeball and a cobweb being broken all make nothing. Two
things are missing:

- the particle packet's item has to be resolved to a sprite, which is what
  `has_texture`, `texture` and `texture_layer` are for;
- `shaders/particle.frag` samples one `sampler2DArray atlas`, the terrain one,
  and `QuadLayer::Items` is read nowhere in the tree. A particle on the items
  atlas needs that atlas bound, so the pipeline needs a second sampler and the
  shader a branch on the layer.

To confirm: the `eat` phase of `livetest` holds use on a golden apple for four
seconds with hunger on, which in vanilla throws a steady stream of item chips.

### 4. Armour trims are baked and never drawn

**Inferred.** `EquipmentTrims` loads `equipment_trims-<protocol>.plat` and
`EquipmentTrims::sprite_id` makes the id `ArmorTrim.layerAssetId` makes. No
file outside `src/entity/equipment.cpp` mentions the class: nothing opens it,
`EntitiesArea` holds only `EquipmentAssets`, and nothing decodes the
`minecraft:trim` component off an `ItemStack`. Every trimmed piece of armour,
on the player and on every mob, draws as plain armour.

To confirm: the `build` phase puts a gold-eye-trimmed netherite chestplate on
the zombie and the runner puts a redstone-ward-trimmed iron helmet on the
second player; the `mobs` phase looks at both.

### 5. One texture per mob type

**Inferred.** `MobType` holds a single `texture`, so every pig is temperate,
every cat a tabby, every llama creamy, every axolotl lucy. Vanilla picks the
texture from the entity's variant: `PigRenderer` through `PigVariants`,
`CatRenderer` through `CatVariants`, and so on for frogs, axolotls, llamas,
rabbits, parrots, pandas, mooshrooms and wolves. `MobType` needs a variant
table and `Scene` needs to read the variant off the metadata.

To confirm: the `build` phase summons a temperate pig next to a
`{variant:"minecraft:cold"}` pig and a siamese cat; in vanilla the two pigs do
not match.

### 6. Block entities that draw nothing

**Inferred from the renderer list; the `props` phase exists to see it.**
`BlockEntityKind` covers 13 kinds. Vanilla's `BlockEntityRenderers` registers
these as well, and each of them draws nothing here:

| vanilla renderer | what is missing |
| --- | --- |
| `SpawnerRenderer` | the miniature mob turning inside the cage |
| `BeaconRenderer` | the beam, which is visible across a world |
| `PistonHeadRenderer` | an extended piston's head and arm |
| `CampfireRenderer` | the food lying on it |
| `LecternRenderer` | the book on the stand |
| `VaultRenderer` | the item turning above it |
| `TheEndPortalRenderer`, `TheEndGatewayRenderer` | the portal surface |
| `TrialSpawnerRenderer` | its mob and its light |
| `BrushableBlockRenderer` | the item being brushed out |
| `ShelfRenderer` | what is on the shelf |
| `ConduitRenderer` (active) | the cage, the wind and the eye; the inactive shell is drawn |

`ChestRenderer`'s lid, which the owner found missing, is drawn now. The `props`
phase sets one of each in a row at the test site and looks at it from the front
and both sides: a hole in the row is one of these.

### 7. The held item takes no fog

**Inferred.** `shaders/hand.frag` samples the items atlas and the skin and
writes the colour straight out. There is no fog uniform in it at all, while
`entity.frag` and `world.frag` both mix towards `fog.colour`. Under water, in
lava, in powder snow or at the far edge of a low render distance, the held item
and the arm stay at full brightness while the holder fades. `ItemInHandRenderer`
draws under the same fog as everything else.

To confirm: the `swim` phase walks into a four-deep pool holding a pickaxe.

### 8. The Christmas chest

**Inferred.** `tools/bake_block_entities.py` bakes one texture per chest state.
Vanilla's `ChestRenderer` swaps to `textures/entity/chest/christmas.png` on
December 24th to 26th. Small, dated, and the kind of thing that is noticed only
once a year.

## What the session still has to be run for

Every entry above is a reading of the code. The whole point of `livetest` is to
stop guessing, and it has not yet been run end to end: the test server stopped
before the first frame. The run to do, once it is back:

    bash run-live.sh ~/plasma-work/live

and then look at every PNG it writes - close up, and at the three extra yaws
for the phases that ask for them - before trusting any line of this file.
