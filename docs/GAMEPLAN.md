# Game plan

Plasma has to feel like an advantage the moment you start it: faster, smoother and sharper than
the vanilla client, never like a cheap imitation. A player who switches back to vanilla should
notice what they lost. Everything below serves that.

## The bar for anything a player sees

- **Never worse than vanilla.** Nothing reaches the Vanilla profile until it matches vanilla:
  translated from vanilla's code through the port ledger (`docs/PORT-LEDGER.md`), proved by an
  oracle, the server as referee or a pixel comparison with a vanilla screenshot.
- **No visible placeholders.** No missing icons, black boxes, empty books, flat items or silent
  actions. A feature that isn't finished stays hidden or keeps vanilla's behaviour; it does not
  ship half done.
- **Measurably better where it can be.** Speed and feel are where Plasma wins, so every area
  reports numbers against vanilla on the same machine (see "The advantage").
- **The owner plays it before it counts as done.** Ten minutes on a real server. An independent
  review scores the release; under 8 out of 10 it goes back with a fix list.

## Phases

1. **Port ledger.** Vanilla's code next to ours, method by method (`tools/port_ledger.py`). Every
   piece of work starts from it.
2. **The vanilla base.** Work through what a player meets in the first five minutes, in ledger
   order: breaking blocks (crack overlay, particles, timing), the hand and held items, item icons
   in every inventory, crafting, block shapes and hitboxes, texture mipmaps, ambient occlusion and
   smooth lighting, the title, multiplayer, pause, options and death screens.
3. **Playable from a launcher, on real servers.** Sign-in, a play button, the server list.
   Server resource packs, chest-style menus, armour stands and holograms, NPCs, dropped items,
   projectiles, armour and items on other players, sword swing, knockback and shields. Before a
   big network such as CubeCraft: a test server with a strict anticheat (Grim) flags nothing,
   and the first join is made on a spare account.
4. **The advantage**, below.
5. **The Plasma profile and mods**, on top of a base that is already exact.

## The advantage

Measured, published and kept from getting worse, each against vanilla (and Sodium where it
applies) on the same machine:

- **Input latency:** mouse and keys to photon. Raw mouse input, the turn applied every frame,
  no frames queued ahead of the display, frame pacing that waits on the input, not the GPU.
- **Frame rate and stutter:** average FPS, 1% and 0.1% lows and p99 over the four scene flights
  in `docs/METHOD.md`. Lows matter more than the average: no hitch when chunks load, on join,
  on teleport or when a big packet arrives (decode and meshing stay off the frame thread).
- **Start-up and joining:** from launch to the title screen and from click to in-world, in
  seconds, without the JVM's warm-up.
- **Memory:** resident set at render distance 8, 16 and 32.
- **Network:** packets handled as they arrive, no tick-sized delay between a packet and what the
  player sees.

Speed work comes after a piece is exact, never instead of it: the oracles and tests are the net
that proves a faster version still does the same.
