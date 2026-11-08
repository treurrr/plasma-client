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
- The site is roofed: the `site` phase clears ten blocks of air above the floor
  and whatever terrain was above that stays, so no sky light reaches it. That
  is why night vision matters here and why the `rain` phase shows no rain worth
  trusting - nothing about the weather has been settled yet.
- The `chest` phase still does not open the chest. Looking at it from
  thirty-five degrees passed over a block fourteen sixteenths tall; at
  forty-five it should land, and the phase now ends with no screen rather than
  with a pause screen, so whatever is wrong is in the aim or the use, not in
  the screen stack.

## Fixed in this wave

- **Every key meant a fixed key, not its binding.** `Window::poll` read W, A,
  S, D, space, shift, control, T, Q, E and 1-9 off the keyboard directly, so
  Options -> Controls wrote `options.txt`, the Controls screen showed the new
  key, and the game went on reading the old one. The one binding that was
  honoured was `key.inventory`, which `screens_input` looks up properly, so the
  client was inconsistent with itself. `poll` now reads every gameplay field
  through `Keybinds`, falling back to the key the game ships that binding on
  when no registry is set. `tests/test_window.cpp` rebinds forward, attack,
  drop, jump and a hotbar slot and checks each one follows. Measured, by making
  `bound_down` ignore the registry again and running it: eight checks fail, and
  they are the eight that matter - the old forward key still walks and the new
  one does not, the left button still attacks after attack is moved to a key
  and the key does not, a binding on nothing is still held, the drop binding
  does nothing with or without control, and a rebound hotbar key picks no slot.
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

  Proved on the second run, by the server: the `place` phase ends
  `held 36x63 offhand 0x0 entities 131` and the `drop` phase ends
  `held 0x0 offhand 36x60 entities 134`. Three Q taps threw three planks, which
  are the three new entities, and the F tap put what was left in the offhand.
  Pick-block is wired and sends, but the phase aims at the horizon with no
  block in reach, so it has not been seen landing.
- **The chat key never opened the real screen.** Seen working on the second
  run: `live/chat-030.png` has `hello world` on the chat screen's own input
  line, and `live/chat-045.png` has `<PlasmaM2> hello world` back from the
  server in the hud's log.
- **The night vision effect did nothing.** `AtmosphereInputs::night_vision` was
  read by the light texture and written by nobody. Seen, at a site that is
  roofed over and has no sky light at all: `live/run2/drop-070.png` against
  `live/drop-070.png`, where the stone floor goes from 37,15,12 to 140,98,63
  and the whole site becomes readable. `world_tick` now fills it the way
  `GameRenderer.getNightVisionScale` does.

## Seen, and still open

### 1. Entities and block entities are lit by a frozen curve, not the lightmap

**Seen**: `live/drop-070.png`, and every frame of the third run. The world is
lit - stone floor, planks, sky - and the horse, the cow, the pigs, the second
player, the dropped items and the chests in the prop row are flat black
silhouettes, around 5 to 10 out of 255. `shaders/world.vert` samples the real
lightmap (`set 0, binding 3`), which `fill_lightmap` builds out of the
atmosphere; `shaders/entity.vert` does not sample it at all. It recomputes the
light from the entity's packed sky and block levels with its own curve, under a
comment saying it is "the same curve the world's blocks are lit by" - which it
no longer is, because the real lightmap also carries the time of day, gamma,
darkness and night vision. Binding the lightmap to the entity pipeline and
sampling it the way the world does is the whole fix.

This one blocks others: no mob's texture, no armour trim and no variant can be
judged while every mob is a black cut-out.

### 2. The inventory screen's player panel is a black rectangle

**Seen**: `live/run1/inventory-040.png`, and the same in `inventory-010` and
every `inv-click` frame. Where vanilla draws the player turning to follow the
pointer, there is a solid black rectangle the size of the panel. Everything
around it is right: the four armour slots with their empty-slot sprites, the
offhand slot, the 2x2 grid, the recipe book button, the item icons and their
counts. Nothing in the tree renders an entity into a gui panel, so the panel
is drawn and then nothing is put in it.

### 3. The held item takes no fog

**Seen**: `live/run1/swim-070.png` against `live/run1/walk-020.png`. The same
oak-planks block is in hand in both, and it has exactly the same shading in
both, while the world behind it goes from a lit stone floor to a flat
brown-grey haze four blocks under water. `shaders/hand.frag` samples the items
atlas and the skin and writes the colour straight out; there is no fog uniform
in it at all, while `entity.frag` and `world.frag` both mix towards
`fog.colour`. `ItemInHandRenderer` draws under the same fog as everything else.

### 4. The chat log is drawn straight through the keystroke display

**Seen**: `live/run1/stand-020.png`, bottom left. The Plasma keystroke overlay
puts W, A, S, D and SPACE exactly where vanilla puts the chat log, and the
chat's background strip is drawn after it, so every line of chat cuts the key
caps in half. Nothing is wrong with either on its own; they are simply put in
the same place. The same corner shows the sections-and-entities readout drawn
over the held item with its descenders clipped by the bottom of the screen.
Both are `draw_hud` in `src/ui/menu.cpp`, which is laid out as if the vanilla
hud were not there.

### 5. Armour trims are baked and never drawn

**Seen**: `live/run1/site-030.png`, the second player's head at the right of
the frame. He is wearing an iron helmet with a redstone ward trim and it draws
as plain, untrimmed iron. `EquipmentTrims` loads `equipment_trims-<protocol>.plat`
and `EquipmentTrims::sprite_id` makes the id `ArmorTrim.layerAssetId` makes. No
file outside `src/entity/equipment.cpp` mentions the class: nothing opens it,
`EntitiesArea` holds only `EquipmentAssets`, and nothing decodes the
`minecraft:trim` component off an `ItemStack`.

## Inferred, and what would settle it

### 6. Item particles never exist

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

Still inferred after three runs, and the `eat` phase cannot settle it, because
something earlier in the chain is missing: **holding use on food does not eat
it.** The phase picks the golden apples with hotbar 6 and holds the use button
from frame 50 to frame 140, which is four and a half seconds, and the phase
ends `held 986x8` with `food 20` - not one apple was consumed. There is no
use-item-with-no-block path: `interact` sends `use_item_on` when a block is
aimed at and nothing at all when there is not, so `ServerboundUseItemPacket`,
the using-item pose, the eat timer and the sounds are all missing too. Eating
has to work before the particles it throws can be looked for.

### 7. One texture per mob type

**Inferred.** `MobType` holds a single `texture`, so every pig is temperate,
every cat a tabby, every llama creamy, every axolotl lucy. Vanilla picks the
texture from the entity's variant: `PigRenderer` through `PigVariants`,
`CatRenderer` through `CatVariants`, and so on for frogs, axolotls, llamas,
rabbits, parrots, pandas, mooshrooms and wolves. `MobType` needs a variant
table and `Scene` needs to read the variant off the metadata.

Still inferred, and it cannot be settled before entry 1 is. The `build` phase
does summon a temperate pig beside a `{variant:"minecraft:cold"}` pig and a
siamese cat, and the `mobs` phase does look at all three - but every one of
them draws as a flat black cut-out, so no texture can be compared with
another. `live/mobs-040.png` and `live/build-050.png` show exactly that.

### 8. Block entities that draw nothing

The `props` phase sets eighteen of these two blocks apart and teleports the
player in front of each one in turn, keeping one frame per prop in the order
the row was built. Nothing is missing from the row: all eighteen draw their
block. What the walk settled, and what it could not:

**Drawn after all - these two lines were wrong and are gone.**

- The lectern's book is on the stand: `live/props-038.png`, the open book with
  its pages and gold spine, and `live/run2/props-038.png` before the lighting
  changed.
- An extended piston's head and arm are drawn: the plate on its shaft is in
  the same frame, left of the lectern. That is a plain block model rather than
  a renderer, which is why it works - `PistonHeadRenderer` in vanilla is for
  the two ticks a piston is moving, which this never tested.

**Seen missing.**

- `TrialSpawnerRenderer`: `live/run2/props-178.png` is a bare cage with the
  bars and nothing inside it. A trial spawner set with `/setblock` carries a
  default mob, so vanilla turns one in there.

**The row cannot settle these, because vanilla draws nothing for them in this
state either.** Naming what each one actually needs, so the next pass does not
repeat the mistake:

| vanilla renderer | what the test has to set up |
| --- | --- |
| `SpawnerRenderer` | a spawner with `SpawnData`; a bare `/setblock` has none |
| `BeaconRenderer` | a pyramid under it, or there is no beam in vanilla either |
| `CampfireRenderer` | food in `container.0` |
| `VaultRenderer` | a vault that has chosen its display item |
| `TheEndPortalRenderer` | a `minecraft:end_portal`, not an `end_portal_frame` |
| `ConduitRenderer` | the prismarine frame and water that make it active |
| `BrushableBlockRenderer` | suspicious sand with an item, mid-brush |
| `ShelfRenderer` | a shelf with something on it |

`ChestRenderer`'s lid, which the owner found missing, is drawn: the chest and
its latch are there in `live/run1/drop-070.png`. The enchanting table's book is
drawn too, in `live/props-158.png`.

### 9. The server list is unreachable, and `ConnectScreen` never says anything

**Measured, and narrower than it looked.** The screen itself is right and the
ping works; only the door to it is missing.

- The list draws like vanilla: `live/serverlist/multiplayer-s2.png`
  (`plasma-client screenshot multiplayer`) has the name, two wrapped lines of
  motd, the icon slot, the player counts, the green five-bar ping meter on the
  reachable row, the red version and crossed-out bars on the incompatible and
  unreachable ones, the "Scanning for games on your local network" line with
  its animated dots, and all seven buttons with the right ones greyed out.
- The ping is real, not canned. `ServerPinger::ping` runs `status::query` off
  the main thread and `take` copies latency, version, protocol, online, max,
  motd, the player sample and the favicon onto the row, then sets Successful or
  Incompatible by protocol. The same `status::query` against the test server
  answers `1.21.11 (protocol 774), 0/20, "Plasma vanilla test server", 0.6 ms`.
- Not seen: a real server icon, because neither the test server nor the canned
  rows send a favicon, so every row falls back to `misc/unknown_server`. The
  decode and upload path is there (`base64_decode` into `set_dynamic`) and has
  not been exercised against a server that sends one.

**Where it stops:** `title_screen` wires `manager.host.multiplayer` to a bool
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

### 10. The Christmas chest

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

The third run added: the chat screen taking a typed line and the server
sending it back, the crafting table and the inventory opening and closing on
their own keys without a screen ever being left behind, all eighteen
block-entity props drawing their block, the whole site readable under night
vision, and the prop row's campfire, beacon, lectern book, enchanting table
book and conduit shell.

Two of those were suspected first and cleared afterwards, which is the only
reason they are worth writing down:

- the hotbar's slots looked a muddy olive rather than grey. Vanilla's
  `hud/hotbar.png` is a dark olive at 186 alpha, so over a night scene that is
  exactly what it should look like.
- the empty experience bar looked like nine dashes with seams between them
  rather than one bar. Vanilla's `hud/experience_bar_background.png` is a
  repeating row of dark green capsules, so that is right as well.

Both were settled by pulling the sprite out of `client-named.jar` and looking
at it, which is quicker than arguing about a screenshot.
