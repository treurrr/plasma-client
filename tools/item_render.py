#!/usr/bin/env python3
"""Draw an item model into a square tile, the way the game draws one in a slot.

The game has no picture for most items that are blocks: it renders the model
into a cell of its own item atlas, once per item state, and blits that cell
into the slot. This does the same work once, at bake time, so a slot costs one
textured quad instead of a model.

`ItemTransform.apply` places the model, `FaceBakery.bakeQuad` lays the texture
on each face, and `Lighting`'s two directional lights shade it; a z buffer
sorts the faces out. Nothing here is a guess at what the game draws: the
numbers all come from its own model files and its own shader include.
"""
import math

import numpy as np

FACES = ("down", "up", "north", "south", "west", "east")

# `FaceInfo`: which corner of the box each of a face's four vertices takes, as
# (axis, use-max) per coordinate.
FACE_VERTS = {
    "down": (((0, 0), (1, 0), (2, 1)), ((0, 0), (1, 0), (2, 0)),
             ((0, 1), (1, 0), (2, 0)), ((0, 1), (1, 0), (2, 1))),
    "up": (((0, 0), (1, 1), (2, 0)), ((0, 0), (1, 1), (2, 1)),
           ((0, 1), (1, 1), (2, 1)), ((0, 1), (1, 1), (2, 0))),
    "north": (((0, 1), (1, 1), (2, 0)), ((0, 1), (1, 0), (2, 0)),
              ((0, 0), (1, 0), (2, 0)), ((0, 0), (1, 1), (2, 0))),
    "south": (((0, 0), (1, 1), (2, 1)), ((0, 0), (1, 0), (2, 1)),
              ((0, 1), (1, 0), (2, 1)), ((0, 1), (1, 1), (2, 1))),
    "west": (((0, 0), (1, 1), (2, 0)), ((0, 0), (1, 0), (2, 0)),
             ((0, 0), (1, 0), (2, 1)), ((0, 0), (1, 1), (2, 1))),
    "east": (((0, 1), (1, 1), (2, 1)), ((0, 1), (1, 0), (2, 1)),
             ((0, 1), (1, 0), (2, 0)), ((0, 1), (1, 1), (2, 0))),
}

AXIS_DIRS = {"down": (0.0, -1.0, 0.0), "up": (0.0, 1.0, 0.0),
             "north": (0.0, 0.0, -1.0), "south": (0.0, 0.0, 1.0),
             "west": (-1.0, 0.0, 0.0), "east": (1.0, 0.0, 0.0)}

LIGHT_POWER = 0.6
AMBIENT_LIGHT = 0.4
WHITE = np.ones(4)


def _rot_x(angle):
    c, s = math.cos(angle), math.sin(angle)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], dtype=np.float64)


def _rot_y(angle):
    c, s = math.cos(angle), math.sin(angle)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float64)


def _rot_z(angle):
    c, s = math.cos(angle), math.sin(angle)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], dtype=np.float64)


def _lights():
    """`Lighting.Lighting`: the two directions each item lighting mode shades by."""
    first = np.array([0.2, 1.0, -0.7])
    second = np.array([-0.2, 1.0, 0.7])
    first /= np.linalg.norm(first)
    second /= np.linalg.norm(second)
    flat = _rot_y(-math.pi / 8) @ _rot_x(math.pi * 3 / 4)
    solid = (np.diag([1.0, -1.0, 1.0])
             @ (_rot_y(1.0821041) @ _rot_x(3.2375858))
             @ (_rot_y(-math.pi / 8) @ _rot_x(math.pi * 3 / 4)))
    return ((flat @ first, flat @ second), (solid @ first, solid @ second))


LIGHT_FLAT, LIGHT_3D = _lights()


def _default_uv(low, high, face):
    """`FaceBakery.defaultFaceUV`: the texture window a face takes when it names none."""
    if face == "down":
        return (low[0], 16.0 - high[2], high[0], 16.0 - low[2])
    if face == "up":
        return (low[0], low[2], high[0], high[2])
    if face == "north":
        return (16.0 - high[0], 16.0 - high[1], 16.0 - low[0], 16.0 - low[1])
    if face == "south":
        return (low[0], 16.0 - high[1], high[0], 16.0 - low[1])
    if face == "west":
        return (low[2], 16.0 - high[1], high[2], 16.0 - low[1])
    return (16.0 - high[2], 16.0 - high[1], 16.0 - low[2], 16.0 - low[1])


def _vertex_uv(uvs, quadrant, index):
    """`BlockElementFace.getU`, `BlockElementFace.getV`, `Quadrant.rotateVertexIndex`."""
    turned = (index + quadrant) % 4
    u = uvs[0] if turned in (0, 1) else uvs[2]
    v = uvs[1] if turned in (0, 3) else uvs[3]
    return u / 16.0, v / 16.0


def _element_rotation(rotation):
    """`BlockElementRotation.computeTransform`: one element's turn about an axis."""
    axis = rotation["axis"]
    angle = math.radians(float(rotation["angle"]))
    matrix = {"x": _rot_x, "y": _rot_y, "z": _rot_z}[axis](angle)
    if rotation.get("rescale"):
        scale = []
        for i in range(3):
            unit = np.zeros(3)
            unit[i] = 1.0
            scale.append(1.0 / np.max(np.abs(matrix @ unit)))
        matrix = matrix @ np.diag(scale)
    origin = np.array(rotation.get("origin", [8, 8, 8]), dtype=np.float64) / 16.0
    return matrix, origin


def _closest_direction(normal):
    """`FaceBakery.findClosestDirection`: the axis a quad's normal points along."""
    best, best_dot = None, 0.0
    for name, direction in AXIS_DIRS.items():
        dot = float(np.dot(normal, direction))
        if dot >= 0.0 and dot > best_dot:
            best, best_dot = name, dot
    return best


class Pose:
    """`PoseStack.Pose`: the placement, and the separate matrix normals ride."""

    def __init__(self):
        self.matrix = np.eye(4)
        self.normal = np.eye(3)

    def translate(self, x, y, z):
        move = np.eye(4)
        move[0, 3], move[1, 3], move[2, 3] = x, y, z
        self.matrix = self.matrix @ move

    def rotate(self, rotation):
        turn = np.eye(4)
        turn[:3, :3] = rotation
        self.matrix = self.matrix @ turn
        self.normal = self.normal @ rotation

    def scale(self, x, y, z):
        self.matrix = self.matrix @ np.diag([x, y, z, 1.0])
        if abs(x) == abs(y) == abs(z):
            if x < 0.0 or y < 0.0 or z < 0.0:
                self.normal = self.normal @ np.diag([np.sign(x), np.sign(y), np.sign(z)])
        else:
            self.normal = self.normal @ np.diag([1.0 / x, 1.0 / y, 1.0 / z])

    def copy(self):
        other = Pose()
        other.matrix = self.matrix.copy()
        other.normal = self.normal.copy()
        return other

    def set(self, other):
        self.matrix = other.matrix.copy()
        self.normal = other.normal.copy()

    def point(self, position):
        return (self.matrix @ np.array([*position, 1.0]))[:3]

    def direction(self, vector):
        out = self.normal @ np.asarray(vector, dtype=np.float64)
        length = np.linalg.norm(out)
        return out / length if length > 0 else out


def apply_transform(pose, transform, left_handed=False):
    """`ItemTransform.apply`: where a display context puts the model."""
    if transform is None:
        pose.translate(-0.5, -0.5, -0.5)
        return
    rotation = transform.get("rotation", [0.0, 0.0, 0.0])
    translation = [min(max(v * 0.0625, -5.0), 5.0)
                   for v in transform.get("translation", [0.0, 0.0, 0.0])]
    scale = [min(max(v, -4.0), 4.0) for v in transform.get("scale", [1.0, 1.0, 1.0])]
    sign = -1.0 if left_handed else 1.0
    pose.translate(sign * translation[0], translation[1], translation[2])
    pose.rotate(_rot_x(math.radians(rotation[0]))
                @ _rot_y(math.radians(sign * rotation[1]))
                @ _rot_z(math.radians(sign * rotation[2])))
    pose.scale(*scale)
    pose.translate(-0.5, -0.5, -0.5)


def model_quads(elements, textures, deref):
    """Every face of a model as four corners in block space, with its texture window.

    `FaceBakery.bakeQuad` without the atlas: the uvs stay the sprite's own, so
    each face carries the name of the picture it reads rather than a place in a
    sheet.
    """
    quads = []
    for element in elements:
        low = np.array(element["from"], dtype=np.float64)
        high = np.array(element["to"], dtype=np.float64)
        rotation = element.get("rotation")
        turn = _element_rotation(rotation) if rotation else None
        for face_name, face in (element.get("faces") or {}).items():
            if face_name not in FACE_VERTS:
                continue
            texture = deref(face.get("texture"), textures)
            if not isinstance(texture, str):
                continue
            uvs = face.get("uv") or _default_uv(low, high, face_name)
            quadrant = (int(face.get("rotation", 0)) // 90) % 4
            corners, uv = [], []
            for index, picks in enumerate(FACE_VERTS[face_name]):
                point = np.array([(high[axis] if use_max else low[axis]) / 16.0
                                  for axis, use_max in picks])
                if turn is not None:
                    matrix, origin = turn
                    point = matrix @ (point - origin) + origin
                corners.append(point)
                uv.append(_vertex_uv(uvs, quadrant, index))
            corners = np.array(corners)
            geometric = np.cross(corners[2] - corners[0], corners[3] - corners[1])
            length = np.linalg.norm(geometric)
            facing = _closest_direction(geometric / length) if length > 0 else None
            quads.append({
                "corners": corners,
                "uv": np.array(uv),
                "texture": texture,
                "tint": face.get("tintindex", -1),
                "normal": AXIS_DIRS[facing or "up"],
                "shade": element.get("shade", True),
            })
    return quads


def _sample(image, u, v):
    """Nearest texel, which is the only filter the game ever uses on a sprite."""
    height, width = image.shape[:2]
    x = np.clip((u * width).astype(np.int32), 0, width - 1)
    y = np.clip((v * height).astype(np.int32), 0, height - 1)
    return image[y, x]


def _triangle(colour, depth, points, uvs, image, shade, tint):
    """One triangle, nearest sampled, against a depth buffer."""
    size = colour.shape[0]
    min_x = max(int(math.floor(points[:, 0].min())), 0)
    max_x = min(int(math.ceil(points[:, 0].max())), size - 1)
    min_y = max(int(math.floor(points[:, 1].min())), 0)
    max_y = min(int(math.ceil(points[:, 1].max())), size - 1)
    if min_x > max_x or min_y > max_y:
        return
    ys, xs = np.mgrid[min_y:max_y + 1, min_x:max_x + 1]
    px = xs + 0.5
    py = ys + 0.5
    (x0, y0), (x1, y1), (x2, y2) = points[0, :2], points[1, :2], points[2, :2]
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if abs(area) < 1e-12:
        return
    w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area
    w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area
    w2 = 1.0 - w0 - w1
    inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
    if not inside.any():
        return
    z = w0 * points[0, 2] + w1 * points[1, 2] + w2 * points[2, 2]
    near = z >= depth[min_y:max_y + 1, min_x:max_x + 1]
    inside &= near
    if not inside.any():
        return
    u = w0 * uvs[0, 0] + w1 * uvs[1, 0] + w2 * uvs[2, 0]
    v = w0 * uvs[0, 1] + w1 * uvs[1, 1] + w2 * uvs[2, 1]
    texel = _sample(image, u, v).astype(np.float64)
    alpha = texel[..., 3] * tint[3] / 255.0
    inside &= alpha > 0.0
    if not inside.any():
        return
    source = texel[..., :3] * shade * tint[:3]
    target = colour[min_y:max_y + 1, min_x:max_x + 1]
    blend = np.where(inside[..., None], alpha[..., None], 0.0)
    target[..., :3] = source * blend + target[..., :3] * (1.0 - blend)
    target[..., 3] = 255.0 * blend[..., 0] + target[..., 3] * (1.0 - blend[..., 0])
    zbuf = depth[min_y:max_y + 1, min_x:max_x + 1]
    np.copyto(zbuf, z, where=inside)


def render(quads, pose, textures, tile, flat_light=False, tints=None):
    """A model, placed and shaded, into one `tile` by `tile` picture.

    `GuiRenderer.renderItemToAtlas` puts the cell's middle at the origin and
    scales the model up to it, y the other way round; `light.glsl` mixes the
    two lights into one brightness per face.
    """
    colour = np.zeros((tile, tile, 4), dtype=np.float64)
    depth = np.full((tile, tile), -1e30)
    lights = LIGHT_FLAT if flat_light else LIGHT_3D
    middle = tile * 0.5

    placed = []
    for quad in quads:
        screen = np.empty((4, 3))
        for index, corner in enumerate(quad["corners"]):
            point = pose.point(corner)
            screen[index] = (middle + tile * point[0], middle - tile * point[1],
                             tile * point[2])
        normal = pose.direction(quad["normal"])
        normal = np.array([normal[0], -normal[1], normal[2]])
        lit = min(1.0, (max(0.0, float(np.dot(lights[0], normal)))
                        + max(0.0, float(np.dot(lights[1], normal)))) * LIGHT_POWER
                  + AMBIENT_LIGHT)
        placed.append((screen[:, 2].mean(), screen, quad, lit))

    placed.sort(key=lambda item: item[0])
    for _, screen, quad, lit in placed:
        image = textures(quad["texture"])
        if image is None:
            continue
        # `ItemRenderer.getLayerColorSafe`: a tint index nothing answers is white.
        # A quad may carry its own colour instead, which is how a dyed layer of
        # a banner or a shield reaches the model it is drawn over.
        tint = quad.get("colour")
        if tint is None:
            tint = tints(quad["tint"]) if tints is not None and quad["tint"] >= 0 else WHITE
        for a, b, c in ((0, 1, 2), (0, 2, 3)):
            _triangle(colour, depth, screen[[a, b, c]], quad["uv"][[a, b, c]], image,
                      lit, tint)
    return np.clip(colour, 0, 255).astype(np.uint8)
