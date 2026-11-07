# What a live session shows

Four waves of work were each proved by oracles and offscreen renders, and
almost none of it had ever run together in one session against a real server.
This is what does not hold up when it does.

`livetest` is a session with no window: it builds a site on the vanilla test
server, joins it, and runs the client's own frame loop - the screen manager,
the raw input events, the one tick clock - with the keys, the clicks, the
pointer and glfw's clock coming from a script instead of a keyboard, writing
frames to PNG as it goes. Nothing is reached around: a phase presses W and the
player walks because the window reports W held, exactly as it does under a
hand.

    export PLASMA_AUDIO=null PLASMA_CONSOLE=~/plasma-testserver/console.in
    ./build-m6/plasma-client livetest 127.0.0.1:25601 PlasmaM2 ~/plasma-work/live 1280 720
    # one phase only; the name is matched as a substring, so several at once
    # is "sitebuildprops"
    ./build-m6/plasma-client livetest 127.0.0.1:25601 PlasmaM2 out 1280 720 rain

Each phase prints a line of state when it ends (position, health, held and
offhand stacks, entity count, whether a screen has the keyboard) and keeps the
frames it was told to keep, some of them from three more yaws so a thing can
be looked at from its sides as well as its front.

Every entry below says **seen**, with the frame that shows it, or **inferred**,
with what has to be put in front of the camera to settle it. A line that only
reads well is worth nothing here. Frame names are under `~/plasma-work/live`.

## Reading the frames

Two things about the harness are worth knowing before trusting a frame.

- The player is given night vision, because the server's time of day belongs to
  everyone on it and is not ours to change. Without it the site is a black
  rectangle with a hotbar in it.
- A screen nobody closes swallows every phase after it. The first run left the
  pause screen up from `chest-close` onwards and six phases were shot behind
  the Game Menu; the close phases now press Escape only when there is a screen
  to close.

## Fixed in this wave

- **Every key meant a fixed key, not its binding.** `Window::poll` read W, A,
  S, D, space, shift, control, T, Q, E and 1-9 off the keyboard directly, so
  Options -> Controls wrote `options.txt`, the Controls screen showed the new
  key, and the game went on reading the old one. The one binding that was
  honoured was `key.inventory`, which `screens_input` looks up properly, so the
  client was inconsistent with itself. `poll` now reads every gameplay field
  through `Keybinds`, falling back to the key the game ships that binding on
  when no registry is set. `tests/test_window.cpp` rebinds forward, attack,
  drop, jump and a hotbar slot and checks each one follows; it fails eight
  checks against the old `poll`.
- **Drop, swap-to-offhand and pick-block were never even reported.**
  `FrameInput` has `drop`, `drop_stack`, `swap_offhand`, `pick_item_clicks`,
  `attack_clicks` and `use_clicks`; `poll` set only `drop`. It now fills all
  six, plus `control`, which is what vanilla reads for the whole-stack drop and
  for picking a block with its data.
- **The chat line was a stand-in drawn by the hud.** `ChatScreen` - caret,
  history, the completions off the server's command tree, clickable lines - was
  finished and tested in `chattest`, and the live client never opened it: the
  chat key called `Window::begin_typing`, and `hud_render` drew
  `window.typed() + "_"` in a grey box. That path also dropped every character
  outside ASCII 32-126, so a Dutch keyboard could not type "e-diaeresis" at
  all. The chat key now opens the real `ChatScreen`, handed the hud's own chat
  log through the new `hud_chat`/`hud_ticks` hooks.
- **Nothing acted on drop, swap-offhand or pick-block.** Seen, in the first
  run: `live/run1/drop-030.png` through `drop-115.png`, and the phase's own
  line, `held 36x63` before the phase and `held 36x63 offhand 0x0` after three
  Q taps, an F tap and a middle click. No item left the hand, nothing went to
  the offhand, nothing was picked, and no item entity appeared in front of the
  player.

  `interact` in `src/client/player.cpp` now does what `Minecraft.handleKeybinds`
  does with them, through two new sends: `Session::send_player_action` (the
  actions that carry no block: 3 drops the whole stack, 4 drops one, 6 swaps
  hands, all at the origin facing down under sequence 0, which is what vanilla
  sends and what `send_dig_action` could not do without bumping the block
  prediction sequence) and `Session::send_pick_item_from_block`. The presses
  are counted in `player_input` and taken in the tick, the way
  `KeyMapping.consumeClick` does: a tap lasts one frame, and at 60 frames to 20
  ticks two frames in three carry no tick at all, so reading the edge inside
  the tick would have thrown most taps away.

## Seen, and still open

### 1. The inventory screen's player panel is a black rectangle

**Seen**: `live/run1/inventory-040.png`, and the same in `inventory-010` and
every `inv-click` frame. Where vanilla draws the player turning to follow the
pointer, there is a solid black rectangle the size of the panel. Everything
around it is right: the four armour slots with their empty-slot sprites, the
offhand slot, the 2x2 grid, the recipe book button, the item icons and their
counts. Nothing in the tree renders an entity into a gui panel, so the panel
is drawn and then nothing is put in it.

### 2. The held item takes no fog

**Seen**: `live/run1/swim-070.png` against `live/run1/walk-020.png`. The same
oak-planks block is in hand in both, and it has exactly the same shading in
both, while the world behind it goes from a lit stone floor to a flat
brown-grey haze four blocks under water. `shaders/hand.frag` samples the items
atlas and the skin and writes the colour straight out; there is no fog uniform
in it at all, while `entity.frag` and `world.frag` both mix towards
`fog.colour`. `ItemInHandRenderer` draws under the same fog as everything else.

### 3. Armour trims are baked and never drawn

**Seen**: `live/run1/site-030.png`, the second player's head at the right of
the frame. He is wearing an iron helmet with a redstone ward trim and it draws
as plain, untrimmed iron. `EquipmentTrims` loads `equipment_trims-<protocol>.plat`
and `EquipmentTrims::sprite_id` makes the id `ArmorTrim.layerAssetId` makes. No
file outside `src/entity/equipment.cpp` mentions the class: nothing opens it,
`EntitiesArea` holds only `EquipmentAssets`, and nothing decodes the
`minecraft:trim` component off an `ItemStack`.

## Inferred, and what would settle it

### 4. Item particles never exist

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

To settle it: the `eat` phase holds use on a golden apple for four seconds with
hunger on, which in vanilla throws a steady stream of item chips. It has not
yet produced a usable frame - in the first run it was shot behind the pause
screen.

### 5. One texture per mob type

**Inferred.** `MobType` holds a single `texture`, so every pig is temperate,
every cat a tabby, every llama creamy, every axolotl lucy. Vanilla picks the
texture from the entity's variant: `PigRenderer` through `PigVariants`,
`CatRenderer` through `CatVariants`, and so on for frogs, axolotls, llamas,
rabbits, parrots, pandas, mooshrooms and wolves. `MobType` needs a variant
table and `Scene` needs to read the variant off the metadata.

To settle it: the `build` phase summons a temperate pig next to a
`{variant:"minecraft:cold"}` pig and a siamese cat, and the `mobs` phase looks
at them. The first run only ever had one pig in frame at a time.

### 6. Block entities that draw nothing

**Inferred from the renderer list.** `BlockEntityKind` covers 13 kinds.
Vanilla's `BlockEntityRenderers` registers these as well, and each of them
draws nothing here:

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
| `ConduitRenderer` (active) | the cage, the wind and the eye |

`ChestRenderer`'s lid, which the owner found missing, is drawn: the chest and
its latch are there in `live/run1/drop-070.png`.

To settle it: the `props` phase now sets eighteen of these two blocks apart and
teleports the player in front of each one in turn, keeping one frame per prop
in the order the row was built, so a frame with nothing in it but floor names
the renderer that draws nothing. The first run had them one block apart in a
single wide shot and they hid each other.

### 7. The server list is unreachable, and `ConnectScreen` never says anything

**Inferred, from reading the whole title flow, and `livetest` cannot see it:**
the harness is handed an address and joins it, which is the same shortcut the
real client takes. `title_screen` wires `manager.host.multiplayer` to a bool
and leaves its loop the moment Multiplayer is pressed; `play.cpp` then connects
to whatever was on the command line. So `JoinMultiplayerScreen` - the list, the
pings, the icons, the Add/Edit/Delete flow, all built and tested - is reachable
only from `screenshot`. Nothing sets `manager.servers`, `host.connect` or
`host.cancel_connect` either, so even if the screen came up, Join would put up a
`ConnectScreen` that sits on "Connecting to the server..." forever:
`ConnectScreen::status` is written by nobody, and `Session`'s own `on_status`
lines are printed to stdout instead.

The fix is one flow, not one line: the title screen's Multiplayer button sets
`JoinMultiplayerScreen`, `host.connect` starts the session on a thread, the
status lines go to `ConnectScreen::status`, a failure puts up
`DisconnectedScreen`, and `play.cpp`'s address argument becomes a shortcut that
presses Join for you.

### 8. The Christmas chest

**Inferred.** `tools/bake_block_entities.py` bakes one texture per chest state
and nothing in the tree mentions `christmas`. Vanilla's `ChestRenderer` swaps
to `textures/entity/chest/christmas.png` on December 24th to 26th. Small,
dated, and the kind of thing that is noticed only once a year.

## Looked at and found right

Worth saying, because the point of looking is to find out either way. In the
first run these all drew the way they should: the world and its lighting, the
hud (hearts, hunger, hotbar, the held item's name, the chat log, the boss-free
tab list), the block outline, the break stages spreading over the aimed face
and the stone particles coming off it, placing a block, the second player with
his name tag and his helmet, the chest and its latch, the pause screen over a
blurred world, the inventory and crafting screens with their item icons and
counts, and five mob types (pig, cow, cat, horse, zombie) with their models,
their legs moving and their shadows.
