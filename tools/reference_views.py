#!/usr/bin/env python3
"""Print the views to take reference screenshots from in the vanilla client.

Usage: reference_views.py

Each view is an eye and a point it looks at, the same at:x,y,z,tx,ty,tz that
`plasma-client worldshot` takes. Vanilla is placed with /tp and a yaw and a
pitch instead, so each one is turned into the command to paste. A screenshot
taken there and a worldshot of the matching recording show the same scene from
the same place, and can be laid over one another.
"""
import math

# name, recording, eye, target, what it is for
VIEWS = [
    ("glass-front", "glass.plrc", (91, 88, 94), (106, 87, 104),
     "stained glass panes in one section, red nearest: blending order"),
    ("glass-back", "glass.plrc", (115, 88, 94), (100, 87, 104),
     "the same panes from behind, blue nearest"),
    ("lake", "spawn-200.plrc", (30, 80, 30), (50, 63, 50),
     "water surface and shore: fluid heights, water colour"),
    ("forest-far", "spawn-200.plrc", (-20, 95, -20), (60, 70, 60),
     "leaves at a distance: the leaves' mipmap strategy"),
    ("spawn-ground", "spawn-200.plrc", (4, 72, 3), (14, 68, 13),
     "grass and flowers close up: biome tint, cutout mipmaps"),
]


def yaw_pitch(eye, target):
    """The game's angles: yaw 0 looks south (+z) and grows towards west, and
    pitch is positive looking down."""
    dx, dy, dz = (t - e for e, t in zip(eye, target))
    yaw = math.degrees(math.atan2(-dx, dz))
    pitch = -math.degrees(math.atan2(dy, math.hypot(dx, dz)))
    return yaw, pitch


def main():
    print("Settings in the vanilla client before taking these:")
    print("  Video: FOV 70, GUI scale 3, view bobbing OFF, clouds Fancy, render distance 10")
    print("  Window: 1600 x 900. Hide the hud with F1, take the picture with F2.")
    print("  Join the test server, and /time set noon and /weather clear first.\n")
    for name, recording, eye, target, purpose in VIEWS:
        yaw, pitch = yaw_pitch(eye, target)
        # The camera is the eye; the player's feet are 1.62 below it.
        feet = (eye[0], eye[1] - 1.62, eye[2])
        print(f"{name}  ({purpose})")
        print(f"  vanilla:  /tp @s {feet[0]} {feet[1]:.2f} {feet[2]} {yaw:.2f} {pitch:.2f}")
        print(f"  plasma:   plasma-client worldshot replay:corpus/{recording} {name}.png 329 "
              f"at:{eye[0]},{eye[1]},{eye[2]},{target[0]},{target[1]},{target[2]}")
        print()


if __name__ == "__main__":
    main()
