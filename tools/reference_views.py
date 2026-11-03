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

# name, recording, eye, target, day time, what it is for
VIEWS = [
    ("glass-front", "glass.plrc", (91, 88, 94), (106, 87, 104), 6000,
     "stained glass panes in one section, red nearest: blending order"),
    ("glass-back", "glass.plrc", (115, 88, 94), (100, 87, 104), 6000,
     "the same panes from behind, blue nearest"),
    ("lake", "spawn-200.plrc", (30, 80, 30), (50, 63, 50), 6000,
     "water surface and shore: fluid heights, water colour"),
    ("lake-close", "spawn-200.plrc", (8, 80, 10), (8, 62, -50), 6000,
     "the lake from the hill: translucency, the bed through the water"),
    ("lake-level", "spawn-200.plrc", (0, 63.6, -30), (-10, 62.5, -50), 6000,
     "eye just above the water: corner heights, the still texture, sand banks"),
    ("forest-far", "spawn-200.plrc", (-20, 95, -20), (60, 70, 60), 6000,
     "leaves at a distance: the leaves' mipmap strategy"),
    ("spawn-ground", "spawn-200.plrc", (4, 72, 3), (14, 68, 13), 6000,
     "grass and flowers close up: biome tint, cutout mipmaps"),
    ("clouds", "spawn-200.plrc", (0, 101, 0), (100, 150, -100), 6000,
     "looking up at the clouds: cloud height, shading, fog towards the horizon"),
    ("sunrise", "spawn-200.plrc", (0, 101, 0), (100, 130, 0), 23200,
     "facing east at dawn: the sun, the sunrise fan, the fog pulled to its glow"),
    ("dusk", "spawn-200.plrc", (8, 101, 102), (8, 85, -68), 12500,
     "the eye view at dusk: sky gradient, fog colour, the lightmap's evening"),
    ("early-night", "spawn-200.plrc", (8, 101, 102), (8, 85, -68), 13500,
     "just after sunset: stars appearing, sky going black, sky light falling"),
    ("midnight", "spawn-200.plrc", (8, 101, 102), (8, 85, -68), 18000,
     "midnight: night sky light, star brightness, the moon's phase"),
    ("moon", "spawn-200.plrc", (0, 101, 0), (-100, 140, 0), 17000,
     "looking west before midnight: moon and stars"),
    ("noon", "spawn-200.plrc", (8, 101, 102), (8, 85, -68), 6000,
     "the eye view at noon: the reference for brightness and colour"),
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
    print("  Video: FOV 70, GUI scale 3, view bobbing OFF, clouds Fancy, cloud distance 128,")
    print("  render distance 10, brightness 50% (the default), smooth lighting ON, biome blend 5x5.")
    print("  Window: 1600 x 900. Hide the hud with F1, take the picture with F2.")
    print("  Join the test server (localhost:25601) in spectator mode, and first:")
    print("    /gamerule minecraft:advance_time false   /weather clear   /gamemode spectator")
    print("  Afterwards: /gamerule minecraft:advance_time true and /time set to what it was.\n")
    for name, recording, eye, target, time, purpose in VIEWS:
        yaw, pitch = yaw_pitch(eye, target)
        # The camera is the eye; the player's feet are 1.62 below it.
        feet = (eye[0], eye[1] - 1.62, eye[2])
        print(f"{name}  ({purpose})")
        print(f"  vanilla:  /time set {time}")
        print(f"            /tp @s {feet[0]} {feet[1]:.2f} {feet[2]} {yaw:.2f} {pitch:.2f}")
        print(f"  plasma:   plasma-client worldshot replay:corpus/{recording} {name}.png 329 "
              f"at:{eye[0]},{eye[1]},{eye[2]},{target[0]},{target[1]},{target[2]}@t={time},rd=10")
        print()


if __name__ == "__main__":
    main()
