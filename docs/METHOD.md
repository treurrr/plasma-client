# Method

How Plasma gets to "exactly vanilla, but faster" and how anyone can check it.
Section 5 of `PARITY-REVIEW.md` says who builds what; `CONVENTIONS.md` says
where files go. This says how a piece of vanilla is reproduced and proved.

## The reference

- The vanilla 1.21.11 client, decompiled locally with Mojang's own names
  (official mappings + Vineflower) into `~/mc-reference/1.21.11/src` by
  `~/mc-reference/decompile.sh`. It is read for what the game does: numbers,
  order, layout. It never enters this repository; `tools/check_public_tree.py`
  refuses Mojang's source, jars and assets on every commit
  (`git config core.hooksPath .githooks`).
- Mojang's `--reports`, the data and assets in the player's own jar, and real
  bytes from a server, as before.

## What is followed step for step, and what is ours

Where exactness depends on every step - movement and collision, light and
ambient occlusion, fluid heights, environment timelines, the sound pick,
entity animation, text layout - the C++ follows vanilla's algorithm in the
same order, with the same float and double types. Everything else is built
our own way for speed, and speed never changes a value or a pixel.

## Vanilla next to ours: the port ledger

`tools/port_ledger.py` (`PORT-LEDGER.md`) lists every vanilla method next to
the function that ports it, or the reason it needs none, or that nothing does
yet, so nothing is forgotten and nobody greps for where a method lives.

- Every function that ports a vanilla method opens with one line citing it:
  `` // `Entity.collide`: the move after collision with blocks. `` It is the
  one comment the ~1% rule always allows, and it is what makes the port
  findable for people, agents and the ledger.
- Every parity fix starts with `port_ledger.py where Class.method`, `class
  Class` or `todo --package p`: read the vanilla lines it names, then the port
  beside them, then change the C++.
- A class or package we never port gets a `skip` row with its reason in
  `tools/port_map/*.tsv`, and a known gap an `absent` row, so coverage counts
  what matters.
- Before a branch is merged it runs `port_ledger.py check --base main`: no
  stale citation, and what it ports is cited.

## The game's arithmetic

`include/mcpp/jmath.hpp` holds the places where the game does not compute
what the maths says: `jmath::sin`/`cos` read vanilla's 65536-entry table,
and `jmath::LegacyRandom` is its seeded generator (sound variants, particle
scatter). Code that mirrors vanilla uses these, never `std::sin` or its own
random numbers, or it drifts from what the server predicts.

## Proof

1. **Unit tests with independent expected values**: numbers from vanilla's
   behaviour or a known Java result, never derived from the code under test.
2. **The server as referee**: movement is right when a vanilla server sends no
   correction; digging is right when it drops the block; a layout is right
   when the half-read guard stays silent.
3. **Pixels**: offscreen renders beside vanilla screenshots taken at the same
   position, time, window size and gui scale (`tools/reference_views.py`).
4. **Oracles (to build)**: a small Java program per area, compiled against the
   player's own remapped client jar, feeds vanilla's functions the same inputs
   as a C++ probe and both print JSON that `tools/` compares. Pure functions
   first: collision shapes, the font's widths, timelines, the sound pick, the
   mouse curve.

## Speed

Measured the way other clients publish it, so the numbers compare: fixed
flights over four scenes (village, forest, ocean, mountain) at render
distances 8, 16 and 32, every frame time recorded, reported as average FPS,
1% and 0.1% lows, p99 frame time, CPU and GPU ms per frame, chunk load time
and resident memory. The goal is to beat the vanilla client on the same
machine and the fastest published native client per scene. Input latency
(mouse to photon) is measured separately, because it is what a PvP player
feels.

`plasma-client worldbench` is that measurement and docs/BENCH.md holds its
results, what a measured frame does and does not include, and what may
honestly be compared with another client's published table. A number from a
benchmark that leaves work out is quoted as a number from a benchmark that
leaves work out, or it is not quoted.

## Running things

Tests and probes run with scratch homes (`XDG_DATA_HOME`/`XDG_CONFIG_HOME` in
a temp dir where they write), no audio, offscreen, and never write to the
player's own `.minecraft`. Nothing opens a window or a browser.
