#!/usr/bin/env python3
"""The matrix the game places a model with, to the bit.

`PoseStack.Pose` is a `Matrix4f` and a `Matrix3f`, and both are single
precision with a fixed order of operations: a fused multiply-add per term, a
fast path per matrix shape, a quaternion built from one sine. Doing the same
arithmetic in double and rounding once at the end is off by an ulp here and
there, which is nothing on screen but makes an oracle useless, so this is the
same arithmetic: every value a float, every term in the game's own order.

Positions ride `Matrix4f`; normals ride `Matrix3f`, which only ever decides how
bright a face is, and stays in double.
"""
import math
import struct

import numpy as np

# `Matrix4fc`: what a matrix's shape lets the next operation skip.
PERSPECTIVE = 1 << 0
AFFINE = 1 << 1
IDENTITY = 1 << 2
TRANSLATION = 1 << 3
ORTHONORMAL = 1 << 4

def f32(value):
    """One float, as the game would hold it."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


PI_OVER_2 = f32(math.pi * 0.5)
PI_TIMES_2 = f32(math.pi * 2.0)
PI = f32(math.pi)


def fma(a, b, c):
    """`Math.fma` as the game gets it: `joml.useMathFma` is off by default, so
    this is a plain multiply and add, each rounded on its own."""
    return f32(f32(a * b) + c)


def sin(angle):
    return f32(math.sin(angle))


def cos_from_sin(sine, angle):
    """`Math.cosFromSinInternal`: the cosine that belongs with a sine already had."""
    cosine = f32(math.sqrt(f32(1.0 - f32(sine * sine))))
    a = f32(angle + PI_OVER_2)
    b = f32(a - f32(float(int(f32(a / PI_TIMES_2))) * PI_TIMES_2))
    if b < 0.0:
        b = f32(PI_TIMES_2 + b)
    return -cosine if b >= PI else cosine


def quaternion_x(angle):
    """`Quaternionf.rotationX`, and `Axis.XP` with it."""
    half = f32(f32(angle) * 0.5)
    s = sin(half)
    return (s, 0.0, 0.0, cos_from_sin(s, half))


def quaternion_y(angle):
    half = f32(f32(angle) * 0.5)
    s = sin(half)
    return (0.0, s, 0.0, cos_from_sin(s, half))


def quaternion_z(angle):
    half = f32(f32(angle) * 0.5)
    s = sin(half)
    return (0.0, 0.0, s, cos_from_sin(s, half))


def quaternion_xyz(x_angle, y_angle, z_angle):
    """`Quaternionf.rotationXYZ`: a part's turn, and an item transform's."""
    hx, hy, hz = f32(f32(x_angle) * 0.5), f32(f32(y_angle) * 0.5), f32(f32(z_angle) * 0.5)
    sx, sy, sz = sin(hx), sin(hy), sin(hz)
    cx, cy, cz = cos_from_sin(sx, hx), cos_from_sin(sy, hy), cos_from_sin(sz, hz)
    cycz, sysz = f32(cy * cz), f32(sy * sz)
    sycz, cysz = f32(sy * cz), f32(cy * sz)
    return (f32(f32(sx * cycz) + f32(cx * sysz)),
            f32(f32(cx * sycz) - f32(sx * cysz)),
            f32(f32(cx * cysz) + f32(sx * sycz)),
            f32(f32(cx * cycz) - f32(sx * sysz)))


def quaternion_zyx(z_angle, y_angle, x_angle):
    """`Quaternionf.rotationZYX`: the order a model part is turned in."""
    hx, hy, hz = f32(f32(x_angle) * 0.5), f32(f32(y_angle) * 0.5), f32(f32(z_angle) * 0.5)
    sx, sy, sz = sin(hx), sin(hy), sin(hz)
    cx, cy, cz = cos_from_sin(sx, hx), cos_from_sin(sy, hy), cos_from_sin(sz, hz)
    cycz, sysz = f32(cy * cz), f32(sy * sz)
    sycz, cysz = f32(sy * cz), f32(cy * sz)
    return (f32(f32(sx * cycz) - f32(cx * sysz)),
            f32(f32(cx * sycz) + f32(sx * cysz)),
            f32(f32(cx * cysz) - f32(sx * sycz)),
            f32(f32(cx * cycz) + f32(sx * sysz)))


def _abs_equals_one(value):
    return abs(value) == 1.0


class Matrix4f:
    """`Matrix4f`, column major: `m[column][row]`, with the shape it tracks."""

    __slots__ = ("m", "properties")

    def __init__(self, other=None):
        if other is None:
            self.m = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                      [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]]
            self.properties = IDENTITY | AFFINE | TRANSLATION | ORTHONORMAL
        else:
            self.m = [column[:] for column in other.m]
            self.properties = other.properties

    def translate(self, x, y, z):
        x, y, z = f32(x), f32(y), f32(z)
        if self.properties & IDENTITY:
            # `Matrix4f.translation`: an identity matrix just takes the offset.
            self.m = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                      [0.0, 0.0, 1.0, 0.0], [x, y, z, 1.0]]
            self.properties = AFFINE | TRANSLATION | ORTHONORMAL
            return self
        # `Matrix4f.translateGeneric`: the last column, one fused term per axis.
        m = self.m
        for row in range(4):
            m[3][row] = fma(m[0][row], x,
                            fma(m[1][row], y, fma(m[2][row], z, m[3][row])))
        self.properties &= ~(PERSPECTIVE | IDENTITY)
        return self

    def scale(self, x, y, z):
        x, y, z = f32(x), f32(y), f32(z)
        one = _abs_equals_one(x) and _abs_equals_one(y) and _abs_equals_one(z)
        if self.properties & IDENTITY:
            # `Matrix4f.scaling`.
            self.m = [[x, 0.0, 0.0, 0.0], [0.0, y, 0.0, 0.0],
                      [0.0, 0.0, z, 0.0], [0.0, 0.0, 0.0, 1.0]]
            self.properties = AFFINE | (ORTHONORMAL if one else 0)
            return self
        # `Matrix4f.scaleGeneric`.
        m = self.m
        for column, factor in enumerate((x, y, z)):
            for row in range(4):
                m[column][row] = f32(m[column][row] * factor)
        self.properties &= ~(PERSPECTIVE | IDENTITY | TRANSLATION
                             | (0 if one else ORTHONORMAL))
        return self

    def rotate(self, quaternion):
        """`Matrix4f.rotate`: the quaternion as a basis, then this matrix times it."""
        qx, qy, qz, qw = (f32(v) for v in quaternion)
        w2, x2, y2, z2 = f32(qw * qw), f32(qx * qx), f32(qy * qy), f32(qz * qz)
        zw = f32(qz * qw)
        dzw = f32(zw + zw)
        xy = f32(qx * qy)
        dxy = f32(xy + xy)
        xz = f32(qx * qz)
        dxz = f32(xz + xz)
        yw = f32(qy * qw)
        dyw = f32(yw + yw)
        yz = f32(qy * qz)
        dyz = f32(yz + yz)
        xw = f32(qx * qw)
        dxw = f32(xw + xw)
        r = [[f32(f32(f32(w2 + x2) - z2) - y2), f32(dxy + dzw), f32(dxz - dyw)],
             [f32(-dzw + dxy), f32(f32(f32(y2 - z2) + w2) - x2), f32(dyz + dxw)],
             [f32(dyw + dxz), f32(dyz - dxw), f32(f32(f32(z2 - y2) - x2) + w2)]]
        m = self.m
        if self.properties & IDENTITY:
            # `Matrix4f.rotation`.
            self.m = [[r[0][0], r[0][1], r[0][2], 0.0], [r[1][0], r[1][1], r[1][2], 0.0],
                      [r[2][0], r[2][1], r[2][2], 0.0], [0.0, 0.0, 0.0, 1.0]]
            self.properties = AFFINE | ORTHONORMAL
            return self
        if self.properties & TRANSLATION:
            # `Matrix4f.rotateTranslation`: nothing of this matrix survives but
            # its offset, so the basis goes in as it is.
            self.m = [[r[0][0], r[0][1], r[0][2], 0.0], [r[1][0], r[1][1], r[1][2], 0.0],
                      [r[2][0], r[2][1], r[2][2], 0.0], m[3][:]]
            self.properties &= ~(PERSPECTIVE | IDENTITY | TRANSLATION)
            return self
        if self.properties & AFFINE:
            # `Matrix4f.rotateAffine`: three plain terms, the fourth row zero.
            out = [[f32(f32(f32(m[0][row] * r[column][0]) + f32(m[1][row] * r[column][1]))
                        + f32(m[2][row] * r[column][2])) for row in range(3)] + [0.0]
                   for column in range(3)]
        else:
            # `Matrix4f.rotateGeneric`.
            out = [[fma(m[0][row], r[column][0],
                        fma(m[1][row], r[column][1], f32(m[2][row] * r[column][2])))
                    for row in range(4)] for column in range(3)]
        self.m = [out[0], out[1], out[2], m[3][:]]
        self.properties &= ~(PERSPECTIVE | IDENTITY | TRANSLATION)
        return self

    def point(self, x, y, z):
        """`Vector3f.mulPosition`: a corner of a box, in the space this places."""
        x, y, z = f32(x), f32(y), f32(z)
        m = self.m
        if self.properties & IDENTITY:
            return (x, y, z)
        if self.properties & TRANSLATION:
            return (f32(x + m[3][0]), f32(y + m[3][1]), f32(z + m[3][2]))
        return tuple(fma(m[0][row], x, fma(m[1][row], y, fma(m[2][row], z, m[3][row])))
                     for row in range(3))

    def as_array(self):
        """Row major, for anything that would rather have numpy."""
        return np.array([[self.m[column][row] for column in range(4)] for row in range(4)],
                        dtype=np.float64)


class Pose:
    """`PoseStack.Pose`: where a model goes, and the matrix its normals ride.

    The same interface as the plain double pose in tools/item_render.py, so a
    renderer can be handed either.
    """

    def __init__(self):
        self.pose = Matrix4f()
        self.normal = np.eye(3)

    @property
    def matrix(self):
        return self.pose.as_array()

    def copy(self):
        other = Pose()
        other.pose = Matrix4f(self.pose)
        other.normal = self.normal.copy()
        return other

    def set(self, other):
        self.pose = Matrix4f(other.pose)
        self.normal = other.normal.copy()

    def translate(self, x, y, z):
        self.pose.translate(x, y, z)

    def rotate(self, rotation):
        """A turn, as a quaternion or as the 3x3 matrix the plain pose takes."""
        if isinstance(rotation, tuple):
            self.pose.rotate(rotation)
            self.normal = self.normal @ _basis(rotation)
        else:
            raise TypeError("this pose turns by quaternion, as the game does")

    def scale(self, x, y, z):
        self.pose.scale(x, y, z)
        if abs(x) == abs(y) == abs(z):
            if x < 0.0 or y < 0.0 or z < 0.0:
                self.normal = self.normal @ np.diag([np.sign(x), np.sign(y), np.sign(z)])
        else:
            self.normal = self.normal @ np.diag([1.0 / x, 1.0 / y, 1.0 / z])

    def point(self, position):
        return np.array(self.pose.point(*position))

    def direction(self, vector):
        out = self.normal @ np.asarray(vector, dtype=np.float64)
        length = np.linalg.norm(out)
        return out / length if length > 0 else out


def _basis(quaternion):
    """The same turn as a 3x3, in double, for the normal matrix."""
    x, y, z, w = quaternion
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ], dtype=np.float64)
