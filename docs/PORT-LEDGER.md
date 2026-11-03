# Port ledger

`tools/port_ledger.py` puts vanilla's code next to ours, method by method: for
every class and method of the decompiled 1.21.11 client it says which C++
function ports it, that it needs no port and why, or that nothing does yet.
Every parity fix starts from it instead of from a grep.

## Use

```sh
python3 tools/port_ledger.py build                      # about 5 s; again after pulling
python3 tools/port_ledger.py where Entity.collide        # its vanilla lines and its ports
python3 tools/port_ledger.py where GuiGraphics           # what of the class is ported, and where
python3 tools/port_ledger.py class LocalPlayer           # every method: ported where, mapped, or missing
python3 tools/port_ledger.py what src/world/physics.cpp:780   # which vanilla method this function ports
python3 tools/port_ledger.py coverage --depth 1          # per package; --package client/gui --depth 4
python3 tools/port_ledger.py todo --package client       # next missing methods, most-used classes first
python3 tools/port_ledger.py todo --package client --untouched   # classes with nothing ported at all
python3 tools/port_ledger.py stale                       # citations and map rows that name nothing
python3 tools/port_ledger.py check --base main           # what this branch adds, closes and breaks
python3 tools/port_ledger.py report                      # .local/port-ledger/report.md
```

`--json` on any query gives machine output. Queries rebuild the ledger when
our code or the map is newer than it.

`build` reads the decompiled sources at `~/mc-reference/1.21.11/src`
(`--sources DIR` takes another directory) and our `src/`, `include/` and
`tools/oracle/*.cpp`, and writes `.local/port-ledger/ledger.json`. The ledger
holds names and line numbers only, and it and the report stay in `.local/`,
which is ignored: nothing from vanilla's sources leaves it. `PORT_LEDGER_OUT`
builds into another directory, so two people can check at once.

Every class and method of `net/minecraft` and `com/mojang` is indexed, nested
classes as `Outer.Inner`, so a citation of any of them resolves. Coverage
counts only `SCOPE`, the list at the top of the tool: the client, and the
shared code the client runs (entity movement and physics, `world/phys`,
blocks, items, menus, protocol, `core`, `Mth`, the random sources, Blaze3D).

## Citations

A function that ports a vanilla method opens with one line naming it:

```cpp
// `Entity.collide`: the move after collision with blocks and the border.
Vec Mover::collide(const Vec& move) {
```

`Class.method`, `Class::method` and `Class#method` all count, nested classes
as `Outer.Inner.method`, a constructor as `Class.Class`, and one function may
cite several (`` // `A.b`, `C.d`: ... ``). The citation goes on the first line
of the comment directly above the definition (or the inline definition in a
header), above any `template<>` line. A comment inside a body that opens with a
citation marks the step as an inline port of that method in the enclosing
function. A backticked method named later in a comment is a mention: it is
checked for staleness but doesn't count as a port.

Only cite what follows vanilla: same order of steps, same constants, same
branches. Our own approach (data layout, caching, the GPU) cites nothing.

## How a method is matched

In this order; the first that applies wins:

1. **ported**: a function cites it.
2. **inline**: a comment inside a function body cites it.
3. **mapped**, **absent**, **skip**: a row in the verdict map.
4. **named**: a function named like the method in snake_case (`getEyeHeight`
   as `get_eye_height` or `eye_height`) in a file that cites the class, which
   is a member of a type named for the class or one of its supertypes, or a
   free function whose name no other class cited in that file has.
5. **trivial**: an abstract method, an empty body, or a single return,
   assignment or call. Left out of the percentages.
6. **missing**: none of these.

A citation of a method a class inherits resolves to the class that declares
it (`LocalPlayer.getEyeHeight` is `Entity.getEyeHeight`). `found` in the
coverage table is ported, inline, mapped and named over all methods but the
skipped and trivial ones.

## The verdict map

`tools/port_map/*.tsv`, tab-separated, one row per decision, `#` comments:

```
Class.method	verdict	our symbol (file::fn)	note
Entity.isInLava	port	include/mcpp/world/physics.hpp::Body::in_lava
LivingEntity.travel	inline	src/world/physics.cpp::tick	folded into the tick
Gui.renderDemoOverlay	skip		demo mode
SpyglassItem.use	absent		no spyglass zoom yet
NarratorStatus	skip		narration: Plasma has no narrator
client/telemetry/	skip		telemetry: usage reports sent to Mojang
```

Verdicts: `port` (this function does the method's job), `inline` (it is
folded into this bigger function), `skip` (needs no port; the note says why),
`absent` (we don't do it yet). The key is a method, a whole class (every
method of it and its nested classes), or a package path under `net/minecraft`
ending in `/` (`mojang/blaze3d/opengl/` for `com/mojang`), which covers its
subpackages too. A citation in the code always beats a row. `stale` lists
rows whose class, method or function doesn't exist.

## check

`check --base main` takes the diff since the merge base, working tree and
untracked files included, rebuilds the ledger, reparses the base's version of
each changed file, and lists the ported functions the diff touches, the
citations it adds, the methods it newly ports or maps (gaps closed) or no
longer does (ports lost), changed functions that look like an uncited port of
a class their file cites (rule 4), and stale citations in the changed files.
It exits 1 when there is a stale one.

## What it can't see

- Whether a port is right. A citation says where to look, not that it matches;
  oracles and tests prove behaviour.
- Calls. It doesn't follow either side's call graph, so a port that never runs,
  or runs on one path only, looks the same as one that does.
- Uncited ports with our own names: rule 4 only catches a function named like
  the method, in a file that already cites the class.
- Lambdas and anonymous classes in vanilla: they have no method name, so they
  are covered by the method that holds them.
- C++ the light parser misreads: macros that open braces, a constructor whose
  initializer list uses braces, and function-like macros look odd to it.
  Function spans are by brace matching after comments and strings are blanked.
- Overloads: `Class.method` names all of a method's overloads at once.
