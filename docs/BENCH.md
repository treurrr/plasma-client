Benchmark
=========

What `worldbench` measures, what it does not, our numbers, and the numbers the
Horizon client publishes. The two tables are printed in the same columns so
they can sit next to each other, and the section after them says which of those
columns may honestly be compared. Most of them may not.

Running it
----------

    plasma-client worldbench replay:corpus/world-<scene>.plrc <chunks> <frames> <distances> [day time]
    plasma-client worldbench replay:corpus/world-village.plrc 1200 400 8,16,32

It replays a recorded world (corpus/README says how the four scenes are made),
meshes and uploads it, then flies four fixed paths through it at each render
distance, timing every frame. The recording is server output and is not in the
repository.

What a measured frame holds
---------------------------

In it:

- the sky, fog and lightmap at the camera, worked out the way the client works
  them out: the environment attributes once a tick, the frame between two ticks
  from both of them;
- the entity scene's tick and pose, for the entities the recording carried;
- the hud's build, every frame, as the window builds it;
- the frame's recording into a command buffer, its submit, and the timestamp
  readback.

Not in it:

- world generation. There is none: the world is a recording.
- chunk streaming, meshing and uploading. All of it happens before the flight,
  so a frame here is a frame of a client with nothing left to load.
- the server's packets, physics, block entities, particles, the hand, sound,
  and everything else a game tick does.

`cpu ms` is the frame's own thread cpu time (`CLOCK_THREAD_CPUTIME_ID`), so the
wait for the gpu is not in it. `gpu ms` is the gap between two timestamp
queries around the frame. `drawn` is how many of the world's sections the
frustum kept, and `chunks ... of want` is how many chunks the recording had
against the `(2d+1)^2` a vanilla client holds at that render distance: a row
whose `chunks` is below its `want` is a row that never drew the render distance
in its own first column.

Our numbers
-----------

RTX 4070 Laptop, 1920 by 1080 offscreen, 300 measured frames a flight, a
fresh process per scene and render distance, nothing else running on the
machine. The four flight names describe the camera's path, not the ground
under it: every scene flies all four of them.

`chunks / want` is how many chunks the recording put around the camera against
the `(2d+1)^2` a vanilla client holds at that distance. Only the render
distance 8 rows are full. `drawn` is the sections the frustum kept, and a row
whose `drawn` is 0 drew no world at all: the flight is scaled to its render
distance, so at 32 the path leaves the 27 by 27 chunks that were recorded and
the camera spends the flight looking at sky. Those rows measure the sky, the
hud and an empty depth buffer. They are in the table because leaving them out
would be the kind of tidying this document exists to stop.

| scene | RD | flight | chunks / want | drawn | avg FPS | 1% low FPS | p99 ms | CPU ms | GPU ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| village | 8 | low pass over the water | 289 / 289 | 28 | 1116 | 295 | 1.787 | 0.405 | 0.365 |
| village | 8 | circle over the trees | 289 / 289 | 1083 | 1045 | 336 | 2.415 | 0.417 | 0.442 |
| village | 8 | walk across the hills | 289 / 289 | 13 | 1373 | 591 | 1.602 | 0.332 | 0.287 |
| village | 8 | climb to the clouds | 289 / 289 | 1191 | 1369 | 707 | 1.276 | 0.267 | 0.364 |
| village | 16 | low pass over the water | 697 / 1089 | 4 | 848 | 353 | 2.624 | 0.541 | 0.536 |
| village | 16 | circle over the trees | 697 / 1089 | 2904 | 516 | 273 | 3.569 | 0.950 | 0.843 |
| village | 16 | walk across the hills | 697 / 1089 | 7 | 927 | 444 | 2.074 | 0.469 | 0.502 |
| village | 16 | climb to the clouds | 697 / 1089 | 3390 | 524 | 364 | 2.725 | 0.775 | 0.998 |
| village | 32 | low pass over the water | 697 / 4225 | 0 | 1185 | 367 | 2.502 | 0.454 | 0.305 |
| village | 32 | circle over the trees | 697 / 4225 | 4253 | 371 | 239 | 4.019 | 1.194 | 1.413 |
| village | 32 | walk across the hills | 697 / 4225 | 0 | 795 | 286 | 3.418 | 0.588 | 0.580 |
| village | 32 | climb to the clouds | 697 / 4225 | 4920 | 389 | 245 | 3.964 | 1.016 | 1.461 |
| forest | 8 | low pass over the water | 289 / 289 | 21 | 1215 | 520 | 1.799 | 0.345 | 0.384 |
| forest | 8 | circle over the trees | 289 / 289 | 1337 | 864 | 406 | 2.236 | 0.491 | 0.557 |
| forest | 8 | walk across the hills | 289 / 289 | 7 | 1396 | 551 | 1.652 | 0.299 | 0.323 |
| forest | 8 | climb to the clouds | 289 / 289 | 1433 | 1001 | 561 | 1.732 | 0.348 | 0.526 |
| forest | 16 | low pass over the water | 697 / 1089 | 0 | 770 | 341 | 2.906 | 0.554 | 0.639 |
| forest | 16 | circle over the trees | 697 / 1089 | 3536 | 419 | 244 | 4.027 | 1.020 | 1.263 |
| forest | 16 | walk across the hills | 697 / 1089 | 68 | 590 | 232 | 4.004 | 0.745 | 0.837 |
| forest | 16 | climb to the clouds | 697 / 1089 | 3221 | 432 | 286 | 3.483 | 0.911 | 1.307 |
| forest | 32 | low pass over the water | 697 / 4225 | 0 | 832 | 277 | 3.527 | 0.587 | 0.519 |
| forest | 32 | circle over the trees | 697 / 4225 | 4947 | 289 | 202 | 4.817 | 1.506 | 1.835 |
| forest | 32 | walk across the hills | 697 / 4225 | 0 | 556 | 218 | 4.337 | 0.788 | 0.919 |
| forest | 32 | climb to the clouds | 697 / 4225 | 5620 | 272 | 211 | 4.643 | 1.501 | 2.035 |
| ocean | 8 | low pass over the water | 289 / 289 | 29 | 1067 | 510 | 1.925 | 0.474 | 0.358 |
| ocean | 8 | circle over the trees | 289 / 289 | 1186 | 726 | 356 | 2.594 | 0.706 | 0.564 |
| ocean | 8 | walk across the hills | 289 / 289 | 41 | 983 | 488 | 2.001 | 0.496 | 0.415 |
| ocean | 8 | climb to the clouds | 289 / 289 | 1344 | 1049 | 588 | 1.634 | 0.337 | 0.496 |
| ocean | 16 | low pass over the water | 835 / 1089 | 163 | 571 | 217 | 4.119 | 0.828 | 0.738 |
| ocean | 16 | circle over the trees | 835 / 1089 | 3649 | 346 | 229 | 4.258 | 1.340 | 1.461 |
| ocean | 16 | walk across the hills | 835 / 1089 | 121 | 576 | 252 | 3.585 | 0.842 | 0.806 |
| ocean | 16 | climb to the clouds | 835 / 1089 | 4397 | 338 | 230 | 4.169 | 1.223 | 1.631 |
| ocean | 32 | low pass over the water | 987 / 4225 | 0 | 621 | 301 | 3.312 | 0.776 | 0.731 |
| ocean | 32 | circle over the trees | 987 / 4225 | 5389 | 234 | 165 | 6.017 | 2.081 | 2.039 |
| ocean | 32 | walk across the hills | 987 / 4225 | 0 | 417 | 196 | 5.038 | 1.120 | 1.154 |
| ocean | 32 | climb to the clouds | 987 / 4225 | 7463 | 218 | 154 | 6.341 | 1.984 | 2.426 |
| mountain | 8 | low pass over the water | 289 / 289 | 37 | 1403 | 574 | 1.686 | 0.241 | 0.365 |
| mountain | 8 | circle over the trees | 289 / 289 | 1181 | 1003 | 461 | 2.079 | 0.379 | 0.500 |
| mountain | 8 | walk across the hills | 289 / 289 | 1 | 1871 | 512 | 1.745 | 0.186 | 0.258 |
| mountain | 8 | climb to the clouds | 289 / 289 | 1674 | 1032 | 598 | 1.582 | 0.355 | 0.503 |
| mountain | 16 | low pass over the water | 697 / 1089 | 0 | 995 | 334 | 2.900 | 0.422 | 0.487 |
| mountain | 16 | circle over the trees | 697 / 1089 | 3937 | 512 | 274 | 3.557 | 0.820 | 1.019 |
| mountain | 16 | walk across the hills | 697 / 1089 | 3 | 824 | 279 | 3.284 | 0.479 | 0.631 |
| mountain | 16 | climb to the clouds | 697 / 1089 | 3092 | 525 | 355 | 2.768 | 0.798 | 0.989 |
| mountain | 32 | low pass over the water | 814 / 4225 | 0 | 1096 | 313 | 2.971 | 0.438 | 0.384 |
| mountain | 32 | circle over the trees | 814 / 4225 | 5597 | 294 | 212 | 4.714 | 1.524 | 1.784 |
| mountain | 32 | walk across the hills | 814 / 4225 | 0 | 688 | 244 | 3.871 | 0.638 | 0.720 |
| mountain | 32 | climb to the clouds | 814 / 4225 | 6416 | 256 | 209 | 4.757 | 1.666 | 2.097 |

The same cpu time, split the way Horizon's table splits it, for all frames and
then for the slowest 1%. `world` is zero by construction: the bench uploads
the world before the flight, so nothing streams during it.

| scene | RD | flight | CPU ms | world | tick | prep | renderer | slow 1% CPU | world | tick | prep | renderer |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| village | 8 | low pass over the water | 0.405 | 0.000 | 0.001 | 0.018 | 0.386 | 2.102 | 0.000 | 0.000 | 0.019 | 2.083 |
| village | 8 | circle over the trees | 0.417 | 0.000 | 0.001 | 0.016 | 0.400 | 2.531 | 0.000 | 0.000 | 0.027 | 2.504 |
| village | 8 | walk across the hills | 0.332 | 0.000 | 0.001 | 0.018 | 0.314 | 1.178 | 0.000 | 0.000 | 0.023 | 1.154 |
| village | 8 | climb to the clouds | 0.267 | 0.000 | 0.001 | 0.016 | 0.250 | 0.616 | 0.000 | 0.000 | 0.021 | 0.595 |
| village | 16 | low pass over the water | 0.541 | 0.000 | 0.002 | 0.017 | 0.522 | 1.009 | 0.000 | 0.000 | 0.031 | 0.978 |
| village | 16 | circle over the trees | 0.950 | 0.000 | 0.003 | 0.027 | 0.921 | 2.299 | 0.000 | 0.000 | 0.033 | 2.266 |
| village | 16 | walk across the hills | 0.469 | 0.000 | 0.001 | 0.017 | 0.451 | 1.009 | 0.000 | 0.000 | 0.021 | 0.987 |
| village | 16 | climb to the clouds | 0.775 | 0.000 | 0.003 | 0.021 | 0.751 | 1.480 | 0.000 | 0.000 | 0.045 | 1.435 |
| village | 32 | low pass over the water | 0.454 | 0.000 | 0.002 | 0.013 | 0.438 | 1.726 | 0.000 | 0.000 | 0.024 | 1.702 |
| village | 32 | circle over the trees | 1.194 | 0.000 | 0.006 | 0.025 | 1.162 | 2.422 | 0.000 | 0.001 | 0.029 | 2.393 |
| village | 32 | walk across the hills | 0.588 | 0.000 | 0.002 | 0.018 | 0.568 | 1.535 | 0.000 | 0.000 | 0.026 | 1.509 |
| village | 32 | climb to the clouds | 1.016 | 0.000 | 0.006 | 0.024 | 0.986 | 1.795 | 0.000 | 0.000 | 0.024 | 1.770 |
| forest | 8 | low pass over the water | 0.345 | 0.000 | 0.001 | 0.015 | 0.329 | 1.186 | 0.000 | 0.000 | 0.019 | 1.166 |
| forest | 8 | circle over the trees | 0.491 | 0.000 | 0.001 | 0.018 | 0.472 | 1.960 | 0.000 | 0.000 | 0.026 | 1.933 |
| forest | 8 | walk across the hills | 0.299 | 0.000 | 0.001 | 0.017 | 0.281 | 1.288 | 0.000 | 0.000 | 0.026 | 1.262 |
| forest | 8 | climb to the clouds | 0.348 | 0.000 | 0.001 | 0.018 | 0.329 | 0.798 | 0.000 | 0.000 | 0.035 | 0.763 |
| forest | 16 | low pass over the water | 0.554 | 0.000 | 0.002 | 0.017 | 0.536 | 1.035 | 0.000 | 0.000 | 0.026 | 1.009 |
| forest | 16 | circle over the trees | 1.020 | 0.000 | 0.003 | 0.024 | 0.993 | 1.967 | 0.000 | 0.001 | 0.035 | 1.930 |
| forest | 16 | walk across the hills | 0.745 | 0.000 | 0.002 | 0.020 | 0.723 | 2.194 | 0.000 | 0.000 | 0.026 | 2.168 |
| forest | 16 | climb to the clouds | 0.911 | 0.000 | 0.004 | 0.024 | 0.883 | 1.617 | 0.000 | 0.000 | 0.039 | 1.577 |
| forest | 32 | low pass over the water | 0.587 | 0.000 | 0.004 | 0.018 | 0.565 | 1.894 | 0.000 | 0.081 | 0.033 | 1.779 |
| forest | 32 | circle over the trees | 1.506 | 0.000 | 0.014 | 0.025 | 1.466 | 2.843 | 0.000 | 0.086 | 0.054 | 2.703 |
| forest | 32 | walk across the hills | 0.788 | 0.000 | 0.004 | 0.021 | 0.763 | 2.211 | 0.000 | 0.000 | 0.028 | 2.183 |
| forest | 32 | climb to the clouds | 1.501 | 0.000 | 0.016 | 0.029 | 1.457 | 2.246 | 0.000 | 0.000 | 0.044 | 2.202 |
| ocean | 8 | low pass over the water | 0.474 | 0.000 | 0.001 | 0.015 | 0.457 | 1.458 | 0.000 | 0.000 | 0.018 | 1.440 |
| ocean | 8 | circle over the trees | 0.706 | 0.000 | 0.001 | 0.018 | 0.687 | 2.308 | 0.000 | 0.019 | 0.032 | 2.257 |
| ocean | 8 | walk across the hills | 0.496 | 0.000 | 0.001 | 0.018 | 0.477 | 1.239 | 0.000 | 0.000 | 0.030 | 1.209 |
| ocean | 8 | climb to the clouds | 0.337 | 0.000 | 0.001 | 0.016 | 0.320 | 0.702 | 0.000 | 0.000 | 0.027 | 0.674 |
| ocean | 16 | low pass over the water | 0.828 | 0.000 | 0.003 | 0.029 | 0.796 | 1.390 | 0.000 | 0.001 | 0.040 | 1.350 |
| ocean | 16 | circle over the trees | 1.340 | 0.000 | 0.004 | 0.024 | 1.312 | 2.400 | 0.000 | 0.001 | 0.037 | 2.362 |
| ocean | 16 | walk across the hills | 0.842 | 0.000 | 0.002 | 0.019 | 0.821 | 2.215 | 0.000 | 0.030 | 0.038 | 2.147 |
| ocean | 16 | climb to the clouds | 1.223 | 0.000 | 0.005 | 0.026 | 1.191 | 1.944 | 0.000 | 0.001 | 0.056 | 1.888 |
| ocean | 32 | low pass over the water | 0.776 | 0.000 | 0.006 | 0.022 | 0.749 | 1.758 | 0.000 | 0.000 | 0.027 | 1.731 |
| ocean | 32 | circle over the trees | 2.081 | 0.000 | 0.016 | 0.030 | 2.035 | 3.473 | 0.000 | 0.001 | 0.040 | 3.433 |
| ocean | 32 | walk across the hills | 1.120 | 0.000 | 0.006 | 0.021 | 1.094 | 2.698 | 0.000 | 0.001 | 0.040 | 2.656 |
| ocean | 32 | climb to the clouds | 1.984 | 0.000 | 0.021 | 0.032 | 1.931 | 3.517 | 0.000 | 0.243 | 0.058 | 3.216 |
| mountain | 8 | low pass over the water | 0.241 | 0.000 | 0.001 | 0.017 | 0.223 | 1.193 | 0.000 | 0.000 | 0.026 | 1.167 |
| mountain | 8 | circle over the trees | 0.379 | 0.000 | 0.001 | 0.019 | 0.359 | 1.694 | 0.000 | 0.000 | 0.020 | 1.673 |
| mountain | 8 | walk across the hills | 0.186 | 0.000 | 0.001 | 0.013 | 0.173 | 1.028 | 0.000 | 0.000 | 0.026 | 1.001 |
| mountain | 8 | climb to the clouds | 0.355 | 0.000 | 0.001 | 0.017 | 0.337 | 0.739 | 0.000 | 0.000 | 0.030 | 0.708 |
| mountain | 16 | low pass over the water | 0.422 | 0.000 | 0.001 | 0.017 | 0.404 | 1.532 | 0.000 | 0.033 | 0.044 | 1.454 |
| mountain | 16 | circle over the trees | 0.820 | 0.000 | 0.002 | 0.024 | 0.793 | 2.001 | 0.000 | 0.000 | 0.027 | 1.974 |
| mountain | 16 | walk across the hills | 0.479 | 0.000 | 0.002 | 0.017 | 0.461 | 2.003 | 0.000 | 0.060 | 0.043 | 1.900 |
| mountain | 16 | climb to the clouds | 0.798 | 0.000 | 0.003 | 0.021 | 0.775 | 1.587 | 0.000 | 0.000 | 0.042 | 1.545 |
| mountain | 32 | low pass over the water | 0.438 | 0.000 | 0.002 | 0.014 | 0.422 | 1.478 | 0.000 | 0.000 | 0.030 | 1.448 |
| mountain | 32 | circle over the trees | 1.524 | 0.000 | 0.011 | 0.027 | 1.486 | 2.734 | 0.000 | 0.001 | 0.036 | 2.698 |
| mountain | 32 | walk across the hills | 0.638 | 0.000 | 0.003 | 0.018 | 0.616 | 1.822 | 0.000 | 0.000 | 0.032 | 1.790 |
| mountain | 32 | climb to the clouds | 1.666 | 0.000 | 0.016 | 0.029 | 1.621 | 2.733 | 0.000 | 0.001 | 0.045 | 2.687 |

And inside the renderer, at render distance 16 (the tool prints this for every
run). `sort` is the translucent quads put back to front as the camera moves,
and `of it upload` the part of that which is the copy into the command buffer
rather than the sorting; `geometry` is the loop that tests every section
against the frustum and records a draw for the ones it keeps.

| scene | RD | flight | renderer | sort | of it upload | geometry | sky | hud | submit | rest |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| village | 16 | low pass over the water | 0.522 | 0.270 | 0.020 | 0.157 | 0.004 | 0.001 | 0.009 | 0.081 |
| village | 16 | circle over the trees | 0.921 | 0.244 | 0.012 | 0.485 | 0.006 | 0.003 | 0.022 | 0.162 |
| village | 16 | walk across the hills | 0.451 | 0.166 | 0.012 | 0.203 | 0.004 | 0.001 | 0.008 | 0.069 |
| village | 16 | climb to the clouds | 0.751 | 0.075 | 0.000 | 0.631 | 0.005 | 0.002 | 0.010 | 0.029 |
| forest | 16 | low pass over the water | 0.536 | 0.229 | 0.016 | 0.211 | 0.004 | 0.001 | 0.009 | 0.082 |
| forest | 16 | circle over the trees | 0.993 | 0.109 | 0.002 | 0.661 | 0.005 | 0.002 | 0.019 | 0.197 |
| forest | 16 | walk across the hills | 0.723 | 0.222 | 0.013 | 0.406 | 0.005 | 0.001 | 0.010 | 0.080 |
| forest | 16 | climb to the clouds | 0.883 | 0.093 | 0.000 | 0.719 | 0.006 | 0.002 | 0.012 | 0.051 |
| ocean | 16 | low pass over the water | 0.796 | 0.449 | 0.025 | 0.255 | 0.008 | 0.003 | 0.015 | 0.066 |
| ocean | 16 | circle over the trees | 1.312 | 0.501 | 0.028 | 0.617 | 0.006 | 0.002 | 0.018 | 0.169 |
| ocean | 16 | walk across the hills | 0.821 | 0.421 | 0.024 | 0.300 | 0.005 | 0.001 | 0.010 | 0.084 |
| ocean | 16 | climb to the clouds | 1.191 | 0.220 | 0.002 | 0.899 | 0.007 | 0.002 | 0.013 | 0.049 |
| mountain | 16 | low pass over the water | 0.404 | 0.072 | 0.002 | 0.229 | 0.004 | 0.001 | 0.008 | 0.090 |
| mountain | 16 | circle over the trees | 0.793 | 0.106 | 0.002 | 0.504 | 0.005 | 0.003 | 0.019 | 0.157 |
| mountain | 16 | walk across the hills | 0.461 | 0.093 | 0.003 | 0.281 | 0.004 | 0.001 | 0.009 | 0.073 |
| mountain | 16 | climb to the clouds | 0.775 | 0.078 | 0.000 | 0.642 | 0.005 | 0.002 | 0.010 | 0.038 |

Horizon's published numbers
---------------------------

From `~/horizon-eigen/src/docs/graphics.md`, the right-hand side of its "after"
pairs: commit `9006566b`, their own `tools/graphics_bench.py --offscreen` on a
desktop RTX 4080, one fresh creative world per render distance, four 12-second
flights. These are their numbers, not ours, and they measure a running game.

| RD | scene | avg FPS | 1% low FPS | p99 ms | CPU ms/frame | GPU ms/frame |
| --- | --- | --- | --- | --- | --- | --- |
| 8 | village | 2409 | 160.8 | 1 | 0.4 | 0.10 |
| 8 | forest | 1099 | 82.9 | 10 | 0.9 | 0.26 |
| 8 | ocean | 687 | 47.5 | 16 | 1.4 | 0.37 |
| 8 | mountain | 344 | 30.0 | 33 | 1.6 | 0.34 |
| 16 | village | 1041 | 64.0 | 10 | 1.0 | 0.23 |
| 16 | forest | 316 | 49.6 | 18 | 3.2 | 0.75 |
| 16 | ocean | 385 | 72.0 | 12 | 2.6 | 0.68 |
| 16 | mountain | 220 | 30.0 | 33 | 3.1 | 0.94 |
| 32 | village | 48 | 0.8 | 76 | 9.4 | 1.77 |
| 32 | forest | 28 | 0.9 | 1108 | 40.1 | 2.30 |
| 32 | ocean | 67 | 10.1 | 95 | 14.7 | 2.91 |
| 32 | mountain | 29 | 15.2 | 64 | 16.1 | 8.47 |

Their document adds that render distance 32 still has long stalls in the forest
and village flights, that the slowest frames there are game ticks rather than
rendering, and that results swing between runs on a shared desktop: one probe
saw render distance 32 forest anywhere from 15 to 335 fps.

What may be compared, and what may not
--------------------------------------

Horizon's `tools/graphics_bench.py` and this benchmark do not measure the same
thing, and the differences all run one way: ours leaves work out.

- **The machine.** Theirs is a desktop RTX 4080. Ours is an RTX 4070 Laptop in
  a 14 GB laptop that other work shares. The `gpu ms` columns are two different
  pieces of hardware, and the `cpu ms` columns are two different processors.
- **The work in a frame.** Theirs is a running game: world generation, chunk
  streaming, mob AI, dropped items, physics, a 20 Hz tick, the lot. Ours is a
  replay with the world already uploaded. Their own document says the slowest
  frames of their render distance 32 flights are game ticks, not rendering, so
  their `1% low` and `p99` columns measure a tick that we do not run at all.
  An average fps of ours against an average fps of theirs is not a comparison
  of two renderers; it is a renderer against a game.
- **The world.** Theirs is a fresh creative world from seed 1234, generated by
  their own client. Ours is four recorded squares of a vanilla server's world.
  Different terrain means different geometry, which moves every column.
- **The flight.** Theirs flies four 192-block paths for 12 seconds each. Ours
  flies four paths scaled to the render distance for a fixed frame count.
- **The frame's size.** Ours renders 1920 by 1080 offscreen. Their published
  numbers do not state the size their offscreen frames are.
- **The render distance.** A render distance only means something if the
  chunks are there: the `chunks ... of want` column says how many were. Where
  it falls short of `want`, that row's render distance was never drawn.

What can honestly be said from the two tables: which part of a frame each
client spends its time in, and that both are cpu-bound rather than gpu-bound at
these distances. Everything else needs the two clients run on one machine over
one world, which is the measurement to make before any claim that one is faster
than the other.

What the room for geometry allows
---------------------------------

The runs above uploaded 0.37 MB a chunk in the village, 0.50 in the ocean, 0.53
on the mountain and 0.58 in the dark forest, vertices and indices together. The
client reserves 384 MB for vertices and 160 MB for indices (`world.cpp`), so it
holds somewhere between 940 and 1470 chunks before it runs out of room and
stops appending - a square of 31 by 31 to 38 by 38, which is render distance
15 to 19.

That is the number to keep beside any render distance 32 row, ours or anyone
else's. A vanilla client at 32 holds 4225 chunks, which would be 1.6 to 2.5 GB
of geometry here. Our client cannot draw that today, whatever the frame rate of
a flight through a quarter of it says.

The sky, and what the old figure was really measuring
------------------------------------------------------

The bench used to print "sky, fog and lightmap worked out from scratch each
frame: 0.0400 ms". Most of that was the bench, not the client. It built a fresh
attribute probe every frame and evaluated the whole environment at the camera -
216 biome samples through a linear search of the chunk list - which the client
does once a tick, not once a frame. Two things came out of taking it apart:

- The client's real per-frame sky work is `world::atmosphere`, and about 11 us
  of it was `fill_lightmap`, which rebuilds 256 texels from factors that only a
  tick can change. The table is now kept and reused while its inputs are
  unchanged, compared bitwise. `test_world` checks every frame's table against
  `lightmap_texel` across a whole day with the flicker, gamma, night vision,
  darkening and ambient light moving, and two worldshots of a terrain view, at
  noon and at dusk, are byte-identical before and after.
- What is left is 0.0019 to 0.0039 ms a frame across the twelve runs above,
  against 0.0400 ms reported before. Roughly a quarter of that drop is the
  lightmap; the rest was the bench measuring itself.

Where a frame's cpu time goes
------------------------------

The renderer is 92 to 97 per cent of a frame's cpu time in every row of the
table above; the tick is a thousandth of a millisecond, and getting the frame
ready - camera, sky, entity poses, hud - is 0.013 to 0.027 ms. Inside the
renderer, two things carry it, and which of the two depends on where the
camera is pointing:

- **Putting the translucent quads back to front as the camera moves.** 0.02 to
  0.27 ms a frame, and the largest single part of a frame at render distance 8
  in a flight low over water: 0.236 of that row's 0.386 ms of renderer. Nine
  tenths of it is the sorting and the index shuffle; only a tenth is the copy
  into the command buffer.
- **The loop that culls sections and records one draw each.** 0.07 to 0.19 ms
  at render distance 8 and 0.16 to 0.83 ms at 16 and 32: about 0.17 us per
  section drawn, plus a frustum test for every section held whether it is
  drawn or not. It grows with the square of the render distance, and it is
  what makes the render distance 16 and 32 rows what they are.

The rest of the recording - barriers, attachments, the lightmap and animation
uploads - is 0.03 to 0.28 ms, the submit and timestamp readback 0.007 to
0.022, the sky pass 0.003 to 0.006 and the hud 0.001 to 0.003.

Two micro-optimisations the profile suggested were tried: sending only the
section's corner as a push constant inside a pass instead of the whole block,
and sorting the translucent quads with their keys beside them, out of scratch
that is not reallocated every frame. Both are strictly less work and both are
kept, but neither moved a number outside the run-to-run spread on this machine,
and saying otherwise would be inventing a win. The three that would move it,
in the order the counters rank them:

1. **The translucent re-sort belongs on a worker thread.** Vanilla sorts
   translucent sections on its chunk workers; we do it in the frame. Its budget
   is also per frame, so the faster the client runs the more sorting it does
   per second, which is exactly backwards.
2. **One draw per section is one draw too many.** An indirect draw over a table
   of sections would collapse the recording, and a frustum test on whole chunk
   columns before their sections would cut the culling by about an order of
   magnitude.
3. **A frame does not overlap with the gpu.** Wall time a frame is larger than
   cpu plus gpu, because the client records, submits and then waits on a fence.
   There is a frame of latency to win by building frame n+1 while the gpu draws
   frame n.

What the bench does not yet exercise
-------------------------------------

The entity scene is ticked and posed in every measured frame, but the four
recordings carry no entities: the recorder was teleported across each square at
y=200, which is above vanilla's entity tracking range, so the server never sent
any. The path is in the frame and its cost is in the `prep` column; the content
is not. Recording the same squares at ground level would fix it, and the
`entities` line of the output says how many each world has.

Would generating specialised code help?
---------------------------------------

Not in a frame. The profile says where a frame's cpu time goes, and none of it
is the kind of per-case branching a generator flattens.

- **The mesher's per-state branching.** Meshing is not in a measured frame at
  all: the client meshes on its own thread and the frame only copies the
  result. Making every block state's meshing free would not move a single
  column of the table. What it would buy is a shorter wait for a world to
  appear, and the ceiling on that is the measured meshing time per chunk minus
  the time spent unpacking the chunk, which is not block-state dispatch.
- **The packet codecs' per-field dispatch.** The packets that arrive in bulk
  are chunks, and a chunk's cost is palette and bit unpacking and the zlib
  stream, not a field-by-field dispatch. The packets with many fields arrive a
  handful at a time per tick.
- **Shader permutations.** The gpu is not the limit at these distances, and the
  world shader's branches are driven by push constants that are the same for
  every pixel of a draw, which is the case a gpu handles well.

Where the time actually is, per the renderer's own counters: re-sorting
translucent quads as the camera moves, recording one draw per section, and the
fixed driver work of a frame. A generator does not touch any of them. What
touches them is structural: the translucent sort belongs on a worker thread
(vanilla sorts there too), the per-section draws belong in one indirect draw,
and the frustum test belongs on whole chunk columns before their sections.
Those are the three to do before anyone writes a generator.
