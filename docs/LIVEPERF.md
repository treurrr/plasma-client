Recording a played session
==========================

`worldbench` measures the draw path: a world that is already meshed and
uploaded, flown through on a fixed path, with nothing left to load. A game is
not that. The frame recorder measures the game: the server's packets, the
physics, chunks streaming in, entities, particles, the hand, sound, the hud
and whatever screen is open, frame by frame, while the owner plays.

Recording
---------

Three ways in, all the same recorder:

- **F10** in the Plasma profile, in the window, starts and stops it. A red dot
  and `REC mm:ss` sit in the top right while it runs, and when it stops the
  path it wrote is on screen for twelve seconds and on stdout.
- **The feature menu** (`m`), the *Frame recorder* card: clicking it starts and
  stops the same recording.
- **`PLASMA_PERF=1 plasma-client play <host>`** records from the first frame,
  which is how to catch the start of a session - logging in, chunks arriving,
  the first walk - that a key press would miss. `PLASMA_PERF=<file>` writes to
  that file instead of a dated one.

Two more knobs: `PLASMA_PERF_FRAMES` is how many frames the ring holds
(262144 by default, about 16 MB, twenty minutes at 200 fps) and
`PLASMA_PERF_DIR` is where a dated recording goes
(`$XDG_DATA_HOME/plasma-client/perf` by default). The ring is allocated when
the recording starts and never grows: a session longer than the ring keeps its
newest frames, drops the oldest, and the report says how many it dropped.

Reading it
----------

    plasma-client perfreport ~/.local/share/plasma-client/perf/frames-20261107-231402.plperf

It prints what the session was (server, dimension, profile, window size,
render distance, how many frames had a screen open, how many entities,
sections and particles were in them), the whole session in the columns
`worldbench` prints - average fps, 1% low, 0.1% low, p99 frame time, cpu ms,
gpu ms and the renderer's own cpu split - and then one row a minute, so a drop
in the middle of a game is visible instead of averaged away. It writes a png
of the frame times beside the recording: every column of pixels is the fastest
to the slowest frame in that slice of the session, with the mean in white, cpu
in green, gpu in purple and the 60 and 120 fps lines dashed across it.

What it costs
-------------

A push into the ring is 2.6 ns and the frame hook's two reads of the thread's
cpu clock are about 1.15 us together, both from `tests/test_perf.cpp`; the
hook makes one of those reads a frame (the cpu ms column) and a second on one
frame in 64 (to measure itself), so the recorder is about 0.6 to 0.7 us of cpu
a frame. In context - the same offscreen bench run with it off and on, three
times each, interleaved - the difference was +0.02 ms of cpu a frame on a mean
of 0.44, which is inside the spread of the same configuration between runs on
a machine that was busy: it is not a measurement of the recorder, only a bound
saying the recorder is smaller than the noise. Nothing allocates while a
recording runs.

Which low is which
------------------

- **avg fps** is 1000 / the mean frame time.
- **1% low** is 1000 / the mean frame time of the slowest 1% of frames. This is
  the average-of-the-worst-1% definition - what CapFrameX calls a 1% low
  average - and it is the one `worldbench` prints, so the two tables are in the
  same units. It is *not* the 99th percentile frame expressed as fps, which is
  a higher, friendlier number.
- **p99 ms** is one frame: the frame at index `floor(0.99 * n)` of the frames
  sorted fastest first. No interpolation.

Both live in `mcpp::perf::mean_of_slowest` and `mcpp::perf::percentile_ms`, and
`tests/test_perf.cpp` checks them against series worked out by hand, including
one built so that the two definitions disagree.

Comparing with vanilla, honestly
--------------------------------

The owner will want the same number out of the vanilla client. Without
instrumenting vanilla - which we are not going to do - here is what is
actually available, and what a comparison may and may not claim.

**What vanilla itself gives you.** The F3 debug screen has an fps counter, and
`F3 + 2` draws a frame-time chart. The counter is frames counted inside a wall
second, refreshed once a second; it is not 1000 / the mean frame time, it has
no 1% low, no 0.1% low, no p99 and no cpu or gpu split, and the chart cannot be
exported. It is good enough to notice that something is wrong and not good
enough to put in a table next to this one. Mods do not fix this: Spark profiles
the client thread and reports MSPT, not frame times, and Sodium, Iris and
OptiFine publish no per-frame log.

**What does give you the same measurement.** An outside frame-time capture that
does not care which client it is looking at. On Linux that is MangoHud: it logs
one frame time per frame to a CSV for any OpenGL or Vulkan program, so it can
be put on the vanilla launcher's java process and on `plasma-client` in turn.
That is the fair instrument, because it is the *same* instrument. Tools disagree
about what a 1% low is, so compare the raw frame times, not the summary lines:
take MangoHud's CSV of each client and run both through the same arithmetic -
the definitions above - rather than quoting one tool's "1% low" against
another's.

**How to make the two sessions comparable.** Same spot, same settings, same
length:

- the same server and the same place in it, standing and walking the same way,
  looking at the same things (a session facing a mob farm and a session facing
  a wall are two different measurements, not two clients);
- the same render distance, window size, fullscreen state, gui scale, graphics
  and particle settings, and the same vsync setting - with vsync on, both
  clients measure the monitor, and the comparison is worthless;
- the same length, at least two or three minutes, and the first minute thrown
  away on both sides: shaders, the page cache and the gpu's clocks all make the
  start of a session slower than the rest, which is exactly why this report has
  a per-minute table;
- alternate the runs - vanilla, Plasma, vanilla, Plasma - because the machine
  is not the same machine from one hour to the next, and nothing else heavy
  running on it either time;
- say which vanilla. "Vanilla 1.21.11, unmodded" and "vanilla with Sodium" are
  very different numbers, and quoting the unmodded one while players use Sodium
  is picking a weak opponent.

**What not to claim.**

- Never a `worldbench` row against any vanilla number. Different work, both
  ways: `worldbench` leaves the game out, and vanilla's counter is a different
  statistic.
- Never a gpu ms comparison. Our gpu ms is a Vulkan timestamp query inside our
  own frame; vanilla has no such number and MangoHud's gpu busy percentage is
  not it.
- Never a cpu ms comparison with anything but ourselves: ours is this thread's
  own cpu time, which no other client publishes.
- Never one run as a result. Two runs each, alternating, and the spread quoted
  with the number; if the two runs of one client differ more than the two
  clients do, there is no result yet.
- Never a session whose render distance or window size moved while it ran. The
  report names the second it changed, and a changed setting means the session
  is two measurements averaged into one.
