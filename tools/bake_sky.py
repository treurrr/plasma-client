#!/usr/bin/env python3
"""Lift the sky textures out of a Minecraft jar.

Usage: bake_sky.py <client.jar> <out.plsk>

Layout after the 4-byte magic and a little-endian version:
  clouds      256x256 alpha, one byte per cell of the cloud sheet
  celestials  10 layers of 64x64 RGBA: the sun, the eight moon phases in the
              game's phase order, the end flash; smaller ones scaled up by
              whole texels so nearest sampling reads them unchanged
  end sky     128x128 RGBA
  rain, snow  64x256 RGBA each

All of it is Mojang's art, read from the installation the player already owns
and never committed here.
"""
import io
import struct
import sys
import zipfile
from pathlib import Path

from PIL import Image

MAGIC = b"PLSK"
VERSION = 2
ENVIRONMENT = "assets/minecraft/textures/environment"
CELESTIAL_SIDE = 64
MOON_PHASES = ("full_moon", "waning_gibbous", "third_quarter", "waning_crescent",
               "new_moon", "waxing_crescent", "first_quarter", "waxing_gibbous")


def read(jar, name, size=None):
    image = Image.open(io.BytesIO(jar.read(f"{ENVIRONMENT}/{name}"))).convert("RGBA")
    if size is not None and image.size != size:
        sys.exit(f"{name}: expected {size}, got {image.size}")
    return image


def celestial(jar, name):
    image = read(jar, f"celestial/{name}.png")
    if image.width != image.height or CELESTIAL_SIDE % image.width:
        sys.exit(f"{name}: {image.size} does not scale to {CELESTIAL_SIDE}")
    return image.resize((CELESTIAL_SIDE, CELESTIAL_SIDE), Image.NEAREST).tobytes()


def main(args):
    if len(args) != 2:
        sys.exit(__doc__)
    jar_path, out_path = Path(args[0]), Path(args[1])
    with zipfile.ZipFile(jar_path) as jar:
        clouds = read(jar, "clouds.png", (256, 256)).getchannel("A").tobytes()
        layers = [celestial(jar, "sun")]
        layers += [celestial(jar, f"moon/{phase}") for phase in MOON_PHASES]
        layers.append(celestial(jar, "end_flash"))
        end_sky = read(jar, "end_sky.png", (128, 128)).tobytes()
        rain = read(jar, "rain.png", (64, 256)).tobytes()
        snow = read(jar, "snow.png", (64, 256)).tobytes()

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(MAGIC + struct.pack("<I", VERSION) + clouds + b"".join(layers) +
                         end_sky + rain + snow)
    print(f"{out_path}: clouds, {len(layers)} celestials, end sky, rain, snow")


if __name__ == "__main__":
    main(sys.argv[1:])
