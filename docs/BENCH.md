Benchmark
=========

What `worldbench` measures, what it does not, our numbers, and the numbers the
Horizon client publishes. The two tables are printed in the same columns so
they can sit next to each other, and the section after them says which of those
columns may honestly be compared. Most of them may not.

Running it
----------

    plasma-client worldbench replay:corpus/world-<scene>.plrc <chunks> <frames> <distances> [day time] [square]
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
distance, so at 32 the path leaves the square that was recorded - 27 by 27
chunks, or 31 by 31 for the ocean - and the camera spends the flight looking
at sky. Those rows measure the sky, the
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
of geometry here. Our client could not draw that when this was written,
whatever the frame rate of a flight through a quarter of it said; the last
section of this file is the change that made the room, and the measurement of
a full 4225-chunk render distance drawn.

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
recordings carry no entities, and the output says so on its third line. Two
reasons, both in the recording rather than the bench: the recorder was
teleported across each square at y=200, which is above vanilla's entity
tracking range, and it stops reading two seconds after the last chunk it asked
for, which is sooner than a server finishes introducing what is around. The
path is in the frame and its cost is in the `prep` column; the content is not.
Recording the squares at ground level, and reading on for a few seconds after
the chunks, would fix it.

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
Those are the three to do before anyone writes a generator. All three, and the
room a render distance needs, are the four changes the last section of this
file measures.

Wave 5: what changed, and what it did to the numbers
----------------------------------------------------

The four changes the counters above ranked are in. The tables in this section
are the same flights over the same recordings, the left half of each pair with
the client at e09ab43 and the right half with the four changes, run back to
back on the same machine.

The recordings are new ones. The four the tables above were measured on were
not on this machine any more - a recording is server output and the repository
holds only corpus/README - so they were made again by the procedure in that
file, four stops 64 blocks apart around the same four places. A recorded
square is not the same square twice, so the rows above and the rows below are
not the same world: only the two halves of a row below may be read against
each other.

### 1. Room for a whole render distance

`world_start` reserved a fixed 384 MB of vertices and 160 MB of indices, which
held 940 to 1470 chunks whatever the render distance in the options said. It
now asks the gpu: `Device::device_local_free` reads `VK_EXT_memory_budget`, and
`client::geometry_budget` multiplies the `(2d+1)^2` chunks a render distance
holds by 620 kB - a little over the heaviest of the four scenes - and keeps
inside what the device has free after a gigabyte of headroom for the targets,
the atlases, the staging buffers and the driver.

The benchmark now goes through the same path. It used to call
`WorldRenderer::upload`, which allocates exactly the geometry it is handed and
so could never run out of room; it now calls `reserve` and then `append` a
chunk at a time, the way a streaming client does, and a chunk the room will
not take is a chunk that row never drew. The `chunks` column therefore now
says what the client held, not what the recording had.

The vertex stayed 20 bytes. Three 16-bit coordinates, an array layer and two
16-bit texture coordinates are 12 of them, and the corner's brightness, the
tint kind and the two light channels pack into one more word: 16. What does
not fit is the blended biome colour, which is three bytes of its own because
the average of a square of biomes is not any one biome's colour. A 16-byte
vertex therefore means quantising either that colour or the texture
coordinates, and both change pixels. Asking the device for the room instead
changes none, and the device has it.

At render distance 32 the reservation is 1958 MB of vertices and 724 MB of
indices, and the four scenes measure 403, 588, 509 and 533 kB a chunk, so that
room holds 6646, 4560, 5273 and 5028 chunks. A vanilla render distance 32
holds 4225. The room is there in all four.

Drawing it needed a world that size, and the recordings are 27 by 27 to 31 by
31 chunks of real terrain: a server sends what its view-distance covers, and
corpus/README's four stops cover about render distance 16. `worldbench` takes
one more argument for this - a square of chunks to fill - where every place
the render distance holds that the recording has no chunk for takes one of the
recorded chunks in turn, shifted into place. It is not the terrain a server
would send, but it is real geometry in every place, and it weighs what the
recording weighs a chunk rather than what its heaviest chunk weighs.

    plasma-client worldbench replay:corpus/world-village.plrc 1200 300 32 6000 65

| flight | chunks / want | drawn | ktris | avg FPS | 1% low | p99 ms | CPU ms | GPU ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| low pass over the water | 4225 / 4225 | 653 | 767 | 266 | 116 | 8.205 | 1.387 | 3.732 |
| circle over the trees | 4225 / 4225 | 16846 | 18340 | 122 | 97 | 10.039 | 2.901 | 8.159 |
| walk across the hills | 4225 / 4225 | 576 | 669 | 216 | 90 | 10.893 | 1.630 | 4.625 |
| climb to the clouds | 4225 / 4225 | 21110 | 22921 | 100 | 81 | 11.997 | 3.446 | 9.991 |

1717 MB uploaded, 406 kB a chunk. That is the first row on this page with its
first column full: 4225 chunks held, and up to 21110 sections drawn in a
frame. The client at e09ab43 could not be asked the question - 544 MB of room
at 406 kB a chunk is 1340 chunks, which is render distance 18 - and the rows
with `697 / 4225` in them below are what it answered instead.

What is still out of reach on this machine is a *recorded* square that big.
`worldbench` unpacks and meshes every chunk of the distance in one pass, and
4225 chunks of real terrain is about 1.7 GB of meshes with the unpacked chunks
beside them, on a 14 GB laptop the owner is using. The tiled square above is
bounded by the geometry the gpu holds instead, which is what the change was
about.

### 2. The translucent re-sort on a worker thread

Vanilla sorts a translucent section on its chunk workers; we did it in the
frame, which meant the faster the client ran the more sorting it did a second.
A `Blend` is now fixed once built and held by `shared_ptr`, so a thread can
sort it without holding the map it came from: the frame says where the camera
is, the thread sorts the two dozen nearest sections not already sorted for
that block, and the frame copies whatever is finished into its command buffer.
One frame, one pass, which is the rate the frame sorted at before.

A single picture has no next frame to pick the order up in, so `render` -
which every worldshot goes through - waits for one pass before it records.
That is what keeps a still picture the picture the old frame drew.

### 3. One indirect draw instead of one per section

A draw command's `firstInstance` is the section's row in a table of origins
that the vertex shader reads by `gl_InstanceIndex`, so a section no longer
needs a push constant and a draw call of its own: the whole opaque pass is one
`vkCmdDrawIndexedIndirect` and the whole translucent pass is another. The
table is written straight into device memory the host can map, three frames
deep. It needs `drawIndirectFirstInstance`, which the device now asks for;
without it, or with `PLASMA_NOINDIRECT` set, the per-section path is still
there and the shader takes the origin from the push constant as before.

Sections are walked column by column now, with one frustum test on the box
around a whole chunk column before any section in it is tested. At render
distance 32 a column is up to two dozen sections, and most columns are behind
the camera.

### 4. The cpu overlapped with the gpu

`draw` recorded, submitted and then waited on a fence, so wall time a frame
was cpu plus gpu plus the driver. There are now two command buffers, two
fences and two pairs of timestamps: a frame waits for the one submitted two
frames ago, records, submits and returns. `gpu ms` is that earlier frame's
reading, and the `gpu ms` numbers below are larger than the ones beside them
for that reason - a timestamp pair now spans a submission that was queued
behind another, not one the cpu sat waiting for. Wall time a frame, which is
what `avg FPS` and `p99 ms` are, is what the change is about.

### The two tables

Every row is 300 measured frames of the same flight over the same recording,
`drawn` and `ktris` equal in both halves, 1920 by 1080 offscreen, a fresh
process per scene. The owner's own work was running on the machine throughout,
which is why these rows are slower than the ones higher up the page; both
halves of every row carry it.

| scene | RD | flight | chunks / want | drawn | avg FPS before / after | 1% low before / after | p99 ms before / after | CPU ms before / after | GPU ms before / after |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| village | 8 | low pass over the water | 289 / 289 | 30 | 943 / 1273 | 514 / 441 | 1.751 / 1.950 | 0.326 / 0.196 | 0.396 / 0.721 |
| village | 8 | circle over the trees | 289 / 289 | 1153 | 746 / 836 | 406 / 350 | 2.116 / 2.615 | 0.375 / 0.307 | 0.579 / 1.155 |
| village | 8 | walk across the hills | 289 / 289 | 15 | 984 / 1183 | 541 / 331 | 1.636 / 2.488 | 0.264 / 0.196 | 0.370 / 0.786 |
| village | 8 | climb to the clouds | 289 / 289 | 1200 | 888 / 1117 | 516 / 493 | 1.632 / 2.000 | 0.295 / 0.240 | 0.461 / 0.861 |
| village | 16 | low pass over the water | 697 / 1089 | 95 | 496 / 775 | 260 / 339 | 3.535 / 2.758 | 0.889 / 0.441 | 0.829 / 1.249 |
| village | 16 | circle over the trees | 697 / 1089 | 2342 | 344 / 541 | 217 / 279 | 4.511 / 3.437 | 1.069 / 0.615 | 1.447 / 1.819 |
| village | 16 | walk across the hills | 697 / 1089 | 97 | 457 / 667 | 224 / 267 | 4.384 / 3.594 | 0.770 / 0.427 | 1.009 / 1.471 |
| village | 16 | climb to the clouds | 697 / 1089 | 3288 | 300 / 472 | 232 / 281 | 4.205 / 3.312 | 1.063 / 0.629 | 1.868 / 2.086 |
| village | 32 | low pass over the water | 697 / 4225 | 0 | 603 / 1000 | 328 / 361 | 2.976 / 2.717 | 0.742 / 0.401 | 0.538 / 0.870 |
| village | 32 | circle over the trees | 697 / 4225 | 3603 | 241 / 398 | 188 / 229 | 5.187 / 4.123 | 1.489 / 0.965 | 2.155 / 2.478 |
| village | 32 | walk across the hills | 697 / 4225 | 0 | 446 / 632 | 199 / 207 | 4.801 / 4.015 | 0.841 / 0.519 | 0.997 / 1.486 |
| village | 32 | climb to the clouds | 697 / 4225 | 4912 | 244 / 383 | 203 / 231 | 4.826 / 3.976 | 1.322 / 0.868 | 2.372 / 2.582 |
| forest | 8 | low pass over the water | 289 / 289 | 21 | 721 / 1155 | 387 / 379 | 2.456 / 2.468 | 0.468 / 0.199 | 0.493 / 0.809 |
| forest | 8 | circle over the trees | 289 / 289 | 1334 | 648 / 745 | 380 / 396 | 2.400 / 2.427 | 0.458 / 0.306 | 0.724 / 1.312 |
| forest | 8 | walk across the hills | 289 / 289 | 7 | 874 / 1055 | 521 / 417 | 1.804 / 2.257 | 0.290 / 0.182 | 0.428 / 0.912 |
| forest | 8 | climb to the clouds | 289 / 289 | 1433 | 686 / 878 | 418 / 487 | 2.089 / 1.809 | 0.380 / 0.238 | 0.684 / 1.117 |
| forest | 16 | low pass over the water | 697 / 1089 | 0 | 489 / 822 | 255 / 329 | 3.898 / 2.753 | 0.795 / 0.353 | 0.925 / 1.168 |
| forest | 16 | circle over the trees | 697 / 1089 | 3547 | 274 / 384 | 209 / 234 | 4.730 / 4.210 | 1.169 / 0.734 | 2.186 / 2.576 |
| forest | 16 | walk across the hills | 697 / 1089 | 67 | 397 / 476 | 209 / 231 | 4.662 / 4.159 | 0.895 / 0.474 | 1.332 / 2.062 |
| forest | 16 | climb to the clouds | 697 / 1089 | 3221 | 280 / 390 | 230 / 272 | 4.328 / 3.565 | 1.072 / 0.632 | 2.206 / 2.534 |
| forest | 32 | low pass over the water | 697 / 4225 | 0 | 539 / 901 | 246 / 276 | 3.917 / 3.425 | 0.783 / 0.345 | 0.760 / 1.014 |
| forest | 32 | circle over the trees | 697 / 4225 | 4947 | 203 / 274 | 151 / 174 | 6.374 / 5.467 | 1.572 / 1.173 | 3.075 / 3.619 |
| forest | 32 | walk across the hills | 697 / 4225 | 0 | 375 / 473 | 161 / 196 | 5.979 / 5.021 | 0.941 / 0.580 | 1.417 / 2.040 |
| forest | 32 | climb to the clouds | 697 / 4225 | 5620 | 187 / 270 | 164 / 203 | 6.072 / 4.864 | 1.573 / 1.093 | 3.535 / 3.679 |
| ocean | 8 | low pass over the water | 289 / 289 | 29 | 757 / 1158 | 414 / 381 | 2.259 / 2.016 | 0.515 / 0.221 | 0.465 / 0.821 |
| ocean | 8 | circle over the trees | 289 / 289 | 1186 | 499 / 721 | 329 / 365 | 2.896 / 2.594 | 0.830 / 0.384 | 0.737 / 1.329 |
| ocean | 8 | walk across the hills | 289 / 289 | 41 | 670 / 898 | 368 / 408 | 2.587 / 2.357 | 0.573 / 0.258 | 0.549 / 1.080 |
| ocean | 8 | climb to the clouds | 289 / 289 | 1344 | 695 / 940 | 427 / 503 | 2.047 / 1.700 | 0.404 / 0.257 | 0.635 / 1.035 |
| ocean | 16 | low pass over the water | 835 / 1089 | 163 | 408 / 713 | 222 / 318 | 4.347 / 2.834 | 0.954 / 0.444 | 1.089 / 1.369 |
| ocean | 16 | circle over the trees | 835 / 1089 | 3637 | 231 / 347 | 176 / 198 | 5.500 / 4.551 | 1.673 / 0.889 | 2.260 / 2.860 |
| ocean | 16 | walk across the hills | 835 / 1089 | 121 | 374 / 499 | 202 / 252 | 4.907 / 3.776 | 1.108 / 0.528 | 1.241 / 1.970 |
| ocean | 16 | climb to the clouds | 835 / 1089 | 4397 | 237 / 308 | 183 / 242 | 5.315 / 3.951 | 1.397 / 0.863 | 2.386 / 3.218 |
| ocean | 32 | low pass over the water | 987 / 4225 | 0 | 371 / 631 | 197 / 240 | 4.986 / 3.812 | 1.002 / 0.476 | 1.335 / 1.508 |
| ocean | 32 | circle over the trees | 987 / 4225 | 5391 | 158 / 237 | 129 / 163 | 7.618 / 6.036 | 2.173 / 1.356 | 3.787 / 4.195 |
| ocean | 32 | walk across the hills | 987 / 4225 | 0 | 277 / 366 | 137 / 165 | 7.154 / 5.873 | 1.378 / 0.744 | 1.946 / 2.650 |
| ocean | 32 | climb to the clouds | 987 / 4225 | 7463 | 164 / 218 | 142 / 172 | 6.880 / 5.778 | 2.004 / 1.382 | 3.864 / 4.561 |
| mountain | 8 | low pass over the water | 289 / 289 | 36 | 945 / 1174 | 398 / 380 | 2.132 / 2.205 | 0.307 / 0.181 | 0.455 / 0.787 |
| mountain | 8 | circle over the trees | 289 / 289 | 1181 | 728 / 781 | 393 / 405 | 2.119 / 2.375 | 0.451 / 0.317 | 0.650 / 1.230 |
| mountain | 8 | walk across the hills | 289 / 289 | 1 | 1113 / 1358 | 524 / 426 | 1.853 / 1.936 | 0.219 / 0.159 | 0.347 / 0.687 |
| mountain | 8 | climb to the clouds | 289 / 289 | 1674 | 735 / 867 | 447 / 476 | 1.999 / 1.844 | 0.406 / 0.274 | 0.655 / 1.122 |
| mountain | 16 | low pass over the water | 697 / 1089 | 0 | 758 / 890 | 291 / 307 | 3.214 / 3.195 | 0.517 / 0.285 | 0.593 / 1.077 |
| mountain | 16 | circle over the trees | 697 / 1089 | 3946 | 348 / 417 | 231 / 233 | 4.279 / 4.252 | 1.170 / 0.730 | 1.457 / 2.376 |
| mountain | 16 | walk across the hills | 697 / 1089 | 2 | 556 / 652 | 235 / 229 | 4.051 / 3.783 | 0.698 / 0.399 | 0.858 / 1.497 |
| mountain | 16 | climb to the clouds | 697 / 1089 | 3092 | 317 / 446 | 210 / 299 | 4.274 / 3.329 | 1.126 / 0.585 | 1.763 / 2.213 |
| mountain | 32 | low pass over the water | 814 / 4225 | 0 | 763 / 1104 | 251 / 298 | 3.855 / 3.209 | 0.574 / 0.296 | 0.467 / 0.778 |
| mountain | 32 | circle over the trees | 814 / 4225 | 5601 | 221 / 280 | 162 / 176 | 6.001 / 5.205 | 1.782 / 1.235 | 2.481 / 3.551 |
| mountain | 32 | walk across the hills | 814 / 4225 | 0 | 421 / 585 | 184 / 218 | 5.351 / 4.417 | 0.887 / 0.509 | 1.104 / 1.623 |
| mountain | 32 | climb to the clouds | 814 / 4225 | 6417 | 164 / 264 | 138 / 180 | 7.167 / 5.063 | 1.900 / 1.201 | 3.837 / 3.757 |

Average frames a second is up in all 48 rows and cpu time a frame is down in
all 48, by a third to a half. The 1% low and p99 columns at render distance 8
are sometimes worse: a frame of latency is a frame of latency, and where a
flight already runs over a thousand frames a second, one hitch now shows up a
frame later.

And inside the renderer, where it came from. `draw calls before` is the
sections the opaque pass drew, each one a `vkCmdPushConstants` and a
`vkCmdDrawIndexed`, and every section with translucent quads cost the same
again in the second pass; `after` is the indirect draws, one a pass.

| scene | RD | flight | renderer before / after | sort before / after | geometry before / after | draw calls before / after |
| --- | --- | --- | --- | --- | --- | --- |
| village | 8 | low pass over the water | 0.266 / 0.143 | 0.139 / 0.021 | 0.068 / 0.049 | 30 / 2 |
| village | 8 | circle over the trees | 0.311 / 0.247 | 0.054 / 0.010 | 0.156 / 0.123 | 1153 / 2 |
| village | 8 | walk across the hills | 0.202 / 0.140 | 0.061 / 0.009 | 0.072 / 0.055 | 15 / 2 |
| village | 8 | climb to the clouds | 0.240 / 0.185 | 0.032 / 0.002 | 0.148 / 0.129 | 1200 / 2 |
| village | 16 | low pass over the water | 0.796 / 0.370 | 0.353 / 0.063 | 0.298 / 0.193 | 95 / 2 |
| village | 16 | circle over the trees | 0.968 / 0.542 | 0.222 / 0.016 | 0.529 / 0.338 | 2342 / 2 |
| village | 16 | walk across the hills | 0.682 / 0.364 | 0.194 / 0.022 | 0.344 / 0.225 | 97 / 2 |
| village | 16 | climb to the clouds | 0.952 / 0.542 | 0.138 / 0.001 | 0.698 / 0.449 | 3288 / 2 |
| village | 32 | low pass over the water | 0.654 / 0.339 | 0.266 / 0.043 | 0.218 / 0.145 | 0 / 0 |
| village | 32 | circle over the trees | 1.365 / 0.872 | 0.260 / 0.018 | 0.788 / 0.546 | 3603 / 2 |
| village | 32 | walk across the hills | 0.746 / 0.448 | 0.178 / 0.023 | 0.405 / 0.274 | 0 / 0 |
| village | 32 | climb to the clouds | 1.197 / 0.770 | 0.117 / 0.001 | 0.966 / 0.683 | 4912 / 2 |
| forest | 8 | low pass over the water | 0.419 / 0.167 | 0.254 / 0.029 | 0.083 / 0.053 | 21 / 2 |
| forest | 8 | circle over the trees | 0.415 / 0.273 | 0.111 / 0.011 | 0.195 / 0.148 | 1334 / 2 |
| forest | 8 | walk across the hills | 0.255 / 0.151 | 0.113 / 0.012 | 0.073 / 0.059 | 7 / 1 |
| forest | 8 | climb to the clouds | 0.337 / 0.208 | 0.067 / 0.003 | 0.202 / 0.149 | 1433 / 2 |
| forest | 16 | low pass over the water | 0.747 / 0.320 | 0.333 / 0.033 | 0.282 / 0.173 | 0 / 0 |
| forest | 16 | circle over the trees | 1.100 / 0.686 | 0.201 / 0.005 | 0.679 / 0.471 | 3547 / 2 |
| forest | 16 | walk across the hills | 0.841 / 0.431 | 0.292 / 0.024 | 0.420 / 0.287 | 67 / 2 |
| forest | 16 | climb to the clouds | 1.005 / 0.582 | 0.166 / 0.001 | 0.738 / 0.493 | 3221 / 2 |
| forest | 32 | low pass over the water | 0.732 / 0.312 | 0.280 / 0.024 | 0.259 / 0.145 | 0 / 0 |
| forest | 32 | circle over the trees | 1.484 / 1.110 | 0.165 / 0.002 | 0.982 / 0.791 | 4947 / 2 |
| forest | 32 | walk across the hills | 0.878 / 0.536 | 0.239 / 0.017 | 0.465 / 0.362 | 0 / 0 |
| forest | 32 | climb to the clouds | 1.480 / 1.026 | 0.163 / 0.001 | 1.177 / 0.935 | 5620 / 2 |
| ocean | 8 | low pass over the water | 0.475 / 0.189 | 0.330 / 0.066 | 0.080 / 0.052 | 29 / 2 |
| ocean | 8 | circle over the trees | 0.783 / 0.341 | 0.485 / 0.092 | 0.187 / 0.133 | 1186 / 2 |
| ocean | 8 | walk across the hills | 0.530 / 0.220 | 0.370 / 0.069 | 0.085 / 0.065 | 41 / 2 |
| ocean | 8 | climb to the clouds | 0.363 / 0.222 | 0.086 / 0.004 | 0.203 / 0.152 | 1344 / 2 |
| ocean | 16 | low pass over the water | 0.899 / 0.402 | 0.480 / 0.098 | 0.282 / 0.181 | 163 / 2 |
| ocean | 16 | circle over the trees | 1.599 / 0.830 | 0.729 / 0.094 | 0.657 / 0.500 | 3637 / 2 |
| ocean | 16 | walk across the hills | 1.050 / 0.477 | 0.608 / 0.120 | 0.308 / 0.226 | 121 / 2 |
| ocean | 16 | climb to the clouds | 1.325 / 0.805 | 0.311 / 0.004 | 0.898 / 0.699 | 4397 / 2 |
| ocean | 32 | low pass over the water | 0.937 / 0.432 | 0.360 / 0.027 | 0.391 / 0.240 | 0 / 0 |
| ocean | 32 | circle over the trees | 2.077 / 1.288 | 0.698 / 0.064 | 1.076 / 0.895 | 5391 / 2 |
| ocean | 32 | walk across the hills | 1.310 / 0.693 | 0.656 / 0.105 | 0.508 / 0.420 | 0 / 0 |
| ocean | 32 | climb to the clouds | 1.911 / 1.311 | 0.345 / 0.001 | 1.459 / 1.213 | 7463 / 2 |
| mountain | 8 | low pass over the water | 0.263 / 0.150 | 0.075 / 0.007 | 0.106 / 0.061 | 36 / 2 |
| mountain | 8 | circle over the trees | 0.403 / 0.281 | 0.067 / 0.006 | 0.221 / 0.162 | 1181 / 2 |
| mountain | 8 | walk across the hills | 0.185 / 0.131 | 0.049 / 0.005 | 0.077 / 0.052 | 1 / 1 |
| mountain | 8 | climb to the clouds | 0.363 / 0.240 | 0.045 / 0.002 | 0.259 / 0.179 | 1674 / 2 |
| mountain | 16 | low pass over the water | 0.475 / 0.251 | 0.135 / 0.008 | 0.236 / 0.139 | 0 / 0 |
| mountain | 16 | circle over the trees | 1.106 / 0.680 | 0.182 / 0.005 | 0.756 / 0.467 | 3946 / 2 |
| mountain | 16 | walk across the hills | 0.648 / 0.358 | 0.155 / 0.009 | 0.385 / 0.236 | 2 / 2 |
| mountain | 16 | climb to the clouds | 1.053 / 0.535 | 0.187 / 0.000 | 0.787 / 0.453 | 3092 / 2 |
| mountain | 32 | low pass over the water | 0.530 / 0.265 | 0.161 / 0.008 | 0.231 / 0.121 | 0 / 0 |
| mountain | 32 | circle over the trees | 1.696 / 1.169 | 0.229 / 0.001 | 1.194 / 0.852 | 5601 / 2 |
| mountain | 32 | walk across the hills | 0.826 / 0.467 | 0.188 / 0.008 | 0.491 / 0.319 | 0 / 0 |
| mountain | 32 | climb to the clouds | 1.793 / 1.130 | 0.239 / 0.001 | 1.477 / 1.037 | 6417 / 2 |

The sort is 0.000 to 0.120 ms a frame against 0.032 to 0.729, and what is left
of it is almost all the copy into the command buffer rather than the sorting:
the ocean's walk across the hills at render distance 16 is 0.120 ms of which
0.096 is the copy. The loop that culls and records is 0.049 to 1.213 ms
against 0.068 to 1.477. The rows where it is still the largest part of a frame
are the ones looking down on five thousand sections from above, where the
column test cannot help because every column is in view.

### What the pictures say

Eight of the views in `tools/reference_views.py` - the ones that show
translucency, and some that show none - before and after:

| view | before and after |
| --- | --- |
| glass-front | byte-identical |
| glass-back | byte-identical |
| lake | byte-identical |
| lake-close | byte-identical |
| lake-level | 9 pixels of 2073600 differ |
| forest-far | byte-identical |
| spawn-ground | byte-identical |
| noon | byte-identical |

The nine pixels are at x 1335 to 1343, y 511 to 514, in the water, and the
largest channel difference is 8 of 255. They are the translucent *sections*
drawn in the other order, not the quads inside one: the glass views, which are
one section of stained panes seen from both sides, are byte-identical, and
those are the views that show quad order.

The cause is the order sections are walked in. `load_chunks`, which is what
worldshot uses, keeps every chunk packet it is sent, and a replay sends a
chunk again each time it came back into view, so the same chunk can be in the
list twice and its sections stand twice in the renderer's table. Walking
column by column puts those two copies next to each other where they used to
be far apart, and the translucent pass sorts the visible sections by distance
with an unstable sort, which leaves equal distances in whatever order they
arrived in. Both pictures draw the same geometry; they blend two equidistant
water sections in the other order along one edge. The new order is the same
from run to run - the view was taken twice with each client and each gave one
answer - and it is the order a client that holds each chunk once would have
drawn all along.

### What did not move the number

- Growing the draw table to fit on every append. It waits for the gpu, and a
  streaming world appends a chunk at a time, so it is doubled instead. This
  was a cost the first version of the change added, not a win.
- A device-local host-visible heap for the origin table. It is asked for
  because it costs nothing to ask where the heap exists, and the plain
  host-visible fallback is what a gpu without one gets. The two were not
  measured against each other, so nothing here is a claim that it helped.
