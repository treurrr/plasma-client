#!/usr/bin/env python3
"""Read the entity model layer table and turn its boxes into quads.

The table is what tools/export/export_models.py writes by asking the game for
every layer it builds. A layer is a tree of parts; a part carries boxes and a
pose relative to its parent. This reads that tree, poses it the way the game
poses one, and hands out each box face as four corners in block space with the
texture window it reads - the same shape tools/item_render.py rasterises for a
block model, so an item that the game draws with a block entity's model costs
the same bake as any other.

Model space is the game's: pixels, y pointing down, the model facing -z, and a
part's own space starts at its pivot.
"""
from pathlib import Path

import numpy as np

import joml

# `Direction.values`: the order a cube's faces come out in, and each one's
# outward normal before the box is mirrored.
FACES = ("down", "up", "north", "south", "west", "east")
FACE_NORMALS = {"down": (0.0, -1.0, 0.0), "up": (0.0, 1.0, 0.0),
                "north": (0.0, 0.0, -1.0), "south": (0.0, 0.0, 1.0),
                "west": (-1.0, 0.0, 0.0), "east": (1.0, 0.0, 0.0)}


class Box:
    __slots__ = ("origin", "size", "grow", "uv", "texture_size", "mirror", "faces")

    def __init__(self):
        self.origin = (0.0, 0.0, 0.0)
        self.size = (0.0, 0.0, 0.0)
        self.grow = (0.0, 0.0, 0.0)
        self.uv = (0.0, 0.0)
        self.texture_size = (64.0, 64.0)
        self.mirror = False
        self.faces = set(FACES)


class PartPose:
    __slots__ = ("x", "y", "z", "x_rot", "y_rot", "z_rot", "x_scale", "y_scale", "z_scale",
                 "visible", "skip_draw")

    def __init__(self):
        self.x = self.y = self.z = 0.0
        self.x_rot = self.y_rot = self.z_rot = 0.0
        self.x_scale = self.y_scale = self.z_scale = 1.0
        self.visible = True
        self.skip_draw = False

    def copy(self):
        other = PartPose()
        for name in PartPose.__slots__:
            setattr(other, name, getattr(self, name))
        return other


class Part:
    __slots__ = ("path", "name", "parent", "pose", "boxes", "children")

    def __init__(self, path):
        self.path = path
        self.name = path.rsplit("/", 1)[-1]
        self.parent = None
        self.pose = PartPose()
        self.boxes = []
        self.children = []


class Model:
    def __init__(self, name):
        self.name = name
        self.texture_width = 64
        self.texture_height = 64
        self.root = None
        self.by_path = {}

    def part(self, path):
        """A part by its path under the root, or by its bare name one level in."""
        return self.by_path.get(path)


def _numbers(words, at, count):
    return tuple(float(words[at + i]) for i in range(count))


def parse(text):
    """Every layer in the table, by name. Mirrors the client's own reader."""
    models = {}
    model = None
    part = None
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split(" #", 1)[0]
        words = line.split()
        if not words or words[0].startswith("#"):
            continue
        head = words[0]
        if head.startswith("["):
            model = Model(head[1:-1])
            models[model.name] = model
            part = None
            continue
        if model is None:
            raise ValueError(f"line {number}: outside any [model]")
        if head == "texture":
            model.texture_width, model.texture_height = (int(words[1]), int(words[2]))
        elif head == "part":
            path = words[1]
            part = Part(path)
            if "/" in path:
                part.parent = model.by_path[path.rsplit("/", 1)[0]]
            elif path != "root":
                part.parent = model.root
            if part.parent is not None:
                part.parent.children.append(part)
            else:
                model.root = part
            # The first part named `root` is the mesh root; a child of the root
            # may be called `root` too, and keeps its own entry.
            model.by_path.setdefault(path, part)
            at = 2
            while at < len(words):
                key = words[at]
                if key == "pivot":
                    part.pose.x, part.pose.y, part.pose.z = _numbers(words, at + 1, 3)
                elif key == "rotate":
                    part.pose.x_rot, part.pose.y_rot, part.pose.z_rot = _numbers(words, at + 1, 3)
                elif key == "scale":
                    (part.pose.x_scale, part.pose.y_scale,
                     part.pose.z_scale) = _numbers(words, at + 1, 3)
                else:
                    raise ValueError(f"line {number}: unknown part key '{key}'")
                at += 4
        elif head == "box":
            box = Box()
            box.origin = _numbers(words, 1, 3)
            box.size = _numbers(words, 4, 3)
            box.texture_size = (float(model.texture_width), float(model.texture_height))
            at = 7
            while at < len(words):
                key = words[at]
                if key == "uv":
                    box.uv = _numbers(words, at + 1, 2)
                    at += 3
                elif key == "grow":
                    grow = float(words[at + 1])
                    at += 2
                    if at + 1 < len(words) and words[at] not in ("mirror", "texsize", "faces"):
                        box.grow = (grow, float(words[at]), float(words[at + 1]))
                        at += 2
                    else:
                        box.grow = (grow, grow, grow)
                elif key == "mirror":
                    box.mirror = True
                    at += 1
                elif key == "texsize":
                    box.texture_size = _numbers(words, at + 1, 2)
                    at += 3
                elif key == "faces":
                    letters = words[at + 1]
                    box.faces = {f for f in FACES if f[0] in letters}
                    at += 2
                else:
                    raise ValueError(f"line {number}: unknown box key '{key}'")
            part.boxes.append(box)
        else:
            raise ValueError(f"line {number}: unknown line '{head}'")
    return models


def load(path):
    return parse(Path(path).read_text())


def box_quads(box):
    """One box's faces: four corners in part space, in blocks, and their uvs.

    `ModelPart.Cube.Cube` and `ModelPart.Polygon.Polygon`: eight corners, six
    faces in a fixed order, each face's rectangle cut from the box's island on
    the sheet, and a mirrored box swapping its x ends and reversing the winding
    of every face.
    """
    x0, y0, z0 = box.origin
    x1 = x0 + box.size[0]
    y1 = y0 + box.size[1]
    z1 = z0 + box.size[2]
    x0 -= box.grow[0]
    y0 -= box.grow[1]
    z0 -= box.grow[2]
    x1 += box.grow[0]
    y1 += box.grow[1]
    z1 += box.grow[2]
    if box.mirror:
        x0, x1 = x1, x0

    corner = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
              (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    width, height, depth = box.size
    u, v = box.uv
    u0, u1, u2 = u, u + depth, u + depth + width
    u3, u4, u5 = u2 + width, u2 + depth, u2 + depth + width
    v0, v1, v2 = v, v + depth, v + depth + height
    sheet_w, sheet_h = box.texture_size

    plan = (("down", (5, 4, 0, 1), u1, v0, u2, v1),
            ("up", (2, 3, 7, 6), u2, v1, u3, v0),
            ("west", (0, 4, 7, 3), u0, v1, u1, v2),
            ("north", (1, 0, 3, 2), u1, v1, u2, v2),
            ("east", (5, 1, 2, 6), u2, v1, u4, v2),
            ("south", (4, 5, 6, 7), u4, v1, u5, v2))

    quads = []
    for face, picks, left, top, right, bottom in plan:
        if face not in box.faces:
            continue
        points = [np.array(corner[i], dtype=np.float64) / 16.0 for i in picks]
        uvs = [(right / sheet_w, top / sheet_h), (left / sheet_w, top / sheet_h),
               (left / sheet_w, bottom / sheet_h), (right / sheet_w, bottom / sheet_h)]
        normal = FACE_NORMALS[face]
        if box.mirror:
            points.reverse()
            uvs.reverse()
            normal = (-normal[0], normal[1], normal[2])
        quads.append({"corners": np.array(points), "uv": np.array(uvs),
                      "normal": normal})
    return quads


def apply_part(pose, part_pose):
    """`ModelPart.translateAndRotate`: move to the pivot, turn z-y-x, then scale."""
    if part_pose.x or part_pose.y or part_pose.z:
        pose.translate(part_pose.x / 16.0, part_pose.y / 16.0, part_pose.z / 16.0)
    if part_pose.x_rot or part_pose.y_rot or part_pose.z_rot:
        pose.rotate(joml.quaternion_zyx(part_pose.z_rot, part_pose.y_rot, part_pose.x_rot))
    if part_pose.x_scale != 1.0 or part_pose.y_scale != 1.0 or part_pose.z_scale != 1.0:
        pose.scale(part_pose.x_scale, part_pose.y_scale, part_pose.z_scale)


class Rig:
    """One layer with a pose per part, which is what an animation writes.

    `Model.resetPose` starts every part from the layer's own numbers; a
    renderer then moves the few it animates.
    """

    def __init__(self, model):
        self.model = model
        self.poses = {path: part.pose.copy() for path, part in model.by_path.items()}

    def pose_of(self, part):
        return self.poses[part.path]

    def set(self, path, **values):
        pose = self.poses[path]
        for name, value in values.items():
            setattr(pose, name, value)
        return pose

    def emit(self, pose, texture, out, root=None, tint=None, kind="geometry"):
        """Every box under `root` (the layer's root by default) as placed quads.

        `ModelPart.render`: a part that is invisible, and everything under it,
        draws nothing; one that skips drawing still carries its children. The
        corners and normals come out already placed by `pose`, so the caller
        rasterises them with no transform of its own.
        """
        part = self.model.by_path[root] if isinstance(root, str) else (root or self.model.root)
        self._walk(part, pose, texture, out, tint, kind)

    def _walk(self, part, pose, texture, out, tint, kind):
        part_pose = self.poses[part.path]
        if not part_pose.visible or (not part.boxes and not part.children):
            return
        saved = pose.copy()
        apply_part(pose, part_pose)
        if not part_pose.skip_draw:
            for box in part.boxes:
                for quad in box_quads(box):
                    out.append({
                        "corners": np.array([pose.point(c) for c in quad["corners"]]),
                        "uv": quad["uv"],
                        "normal": pose.direction(quad["normal"]),
                        "texture": texture,
                        "tint": -1,
                        "shade": True,
                        "colour": tint,
                        "pass": kind,
                    })
        for child in part.children:
            self._walk(child, pose, texture, out, tint, kind)
        pose.set(saved)
