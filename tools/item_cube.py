#!/usr/bin/env python3
"""Draw a block the way the game draws it in an inventory slot.

The game has no picture for a block item: it renders the block model into the
slot, turned 30 degrees down and 225 around, so you see the top, the north face
and the east face. For a plain cube that is three parallelograms, which is what
this builds - one affine warp of each texture, shaded the way the world shades
those three faces.
"""
import numpy as np
from PIL import Image, ImageDraw

# The same numbers the world renderer shades by: full on top, 0.8 on the north
# and south faces, 0.6 on east and west.
SHADE_TOP = 1.0
SHADE_NORTH = 0.8
SHADE_EAST = 0.6
SHADE_DOWN = 0.5


def _corners(size):
    """The eight cube corners, projected.

    Turned 225 degrees around the upright axis and 30 degrees down, which puts
    the near vertical edge in the middle and makes every face a parallelogram.
    """
    angle_y = np.radians(225.0)
    angle_x = np.radians(30.0)
    points = []
    for x in (0, 1):
        for y in (0, 1):
            for z in (0, 1):
                px = (x - 0.5)
                py = (y - 0.5)
                pz = (z - 0.5)
                rx = px * np.cos(angle_y) + pz * np.sin(angle_y)
                rz = -px * np.sin(angle_y) + pz * np.cos(angle_y)
                ry = py * np.cos(angle_x) - rz * np.sin(angle_x)
                points.append((rx, ry))
    scale = size * 0.5 / max(max(abs(p[0]), abs(p[1])) for p in points)
    return {(x, y, z): (size * 0.5 + points[i][0] * scale,
                        size * 0.5 - points[i][1] * scale)
            for i, (x, y, z) in enumerate(
                (x, y, z) for x in (0, 1) for y in (0, 1) for z in (0, 1))}


def render(faces, size=16):
    """One inventory icon for a cube, from its six face textures.

    The three faces pointing away are drawn first and the three pointing at the
    viewer over them, so a block you can see through - glass, leaves, bars -
    shows its own far side rather than the background.
    """
    corner = _corners(size)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    def face(texture, points, shade):
        if texture is None:
            return
        tile = texture.convert("RGBA").resize((size, size), Image.NEAREST)
        shaded = np.array(tile, dtype=np.float32)
        shaded[..., :3] *= shade
        tile = Image.fromarray(np.clip(shaded, 0, 255).astype(np.uint8), "RGBA")
        # PIL maps the output rectangle back onto these four source points, so
        # the polygon is handed over in the order it reads them: top left, then
        # bottom left, bottom right, top right.
        warped = tile.transform((size, size), Image.QUAD,
                                _inverse_quad(points, size), Image.NEAREST)
        # The transform fills the whole output square, so the face is cut back
        # to its own outline before it goes on.
        mask = Image.new("L", (size, size), 0)
        ImageDraw.Draw(mask).polygon(points, fill=255)
        canvas.paste(warped, (0, 0), Image.fromarray(
            np.minimum(np.array(mask), np.array(warped.getchannel("A"))), "L"))

    # Away from the viewer first.
    face(faces.get("down"),
         (corner[(0, 0, 0)], corner[(0, 0, 1)], corner[(1, 0, 1)], corner[(1, 0, 0)]), SHADE_DOWN)
    face(faces.get("south"),
         (corner[(0, 1, 1)], corner[(0, 0, 1)], corner[(1, 0, 1)], corner[(1, 1, 1)]), SHADE_NORTH)
    face(faces.get("west"),
         (corner[(0, 1, 1)], corner[(0, 0, 1)], corner[(0, 0, 0)], corner[(0, 1, 0)]), SHADE_EAST)
    # Then the three that point at it.
    face(faces.get("up"),
         (corner[(0, 1, 0)], corner[(0, 1, 1)], corner[(1, 1, 1)], corner[(1, 1, 0)]), SHADE_TOP)
    face(faces.get("north"),
         (corner[(0, 1, 0)], corner[(0, 0, 0)], corner[(1, 0, 0)], corner[(1, 1, 0)]), SHADE_NORTH)
    face(faces.get("east"),
         (corner[(1, 1, 0)], corner[(1, 0, 0)], corner[(1, 0, 1)], corner[(1, 1, 1)]), SHADE_EAST)
    return canvas


def _inverse_quad(points, size):
    """Source coordinates for the four corners of the output square.

    Image.QUAD reads the output square and samples the given quadrilateral in
    the source, so the parallelogram has to be inverted into source space.
    """
    matrix = _solve(points, size)
    out = []
    for ox, oy in ((0, 0), (0, size), (size, size), (size, 0)):
        x = matrix[0] * ox + matrix[1] * oy + matrix[2]
        y = matrix[3] * ox + matrix[4] * oy + matrix[5]
        out.extend((x, y))
    return out


def _solve(points, size):
    """Affine map from the destination parallelogram back to the unit square."""
    (x0, y0), (x1, y1), (_, _), (x3, y3) = points
    # Along the first edge is one texture axis, along the other the second.
    a = (x3 - x0) / size
    b = (x1 - x0) / size
    c = x0
    d = (y3 - y0) / size
    e = (y1 - y0) / size
    f = y0
    determinant = a * e - b * d
    if abs(determinant) < 1e-9:
        return (1, 0, 0, 0, 1, 0)
    return (e / determinant, -b / determinant,
            (b * f - e * c) / determinant,
            -d / determinant, a / determinant,
            (d * c - a * f) / determinant)
