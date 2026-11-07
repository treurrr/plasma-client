#include "mcpp/entity/model.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <stdexcept>

namespace mcpp::entity {

constexpr float kPi = 3.1415927f;
constexpr float kHalfPi = kPi / 2.0f;
constexpr float kTwoPi = kPi * 2.0f;

namespace {

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
        size_t end = at;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t') ++end;
        if (end > at) out.push_back(line.substr(at, end - at));
        at = end;
    }
    return out;
}

struct Reader {
    std::vector<std::string_view> tokens;
    size_t at = 0;
    size_t line = 0;

    [[noreturn]] void fail(std::string_view what) const {
        throw std::runtime_error(std::format("entity models, line {}: {}", line, what));
    }
    bool done() const { return at >= tokens.size(); }
    std::string_view word() {
        if (done()) fail("line ends too soon");
        return tokens[at++];
    }
    float number() {
        const auto text = word();
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size()) {
            fail(std::format("'{}' is not a number", text));
        }
        return value;
    }
    std::array<float, 3> three() { return {number(), number(), number()}; }
};

enum Face : uint8_t { Down, Up, North, South, West, East };

struct Corner {
    float x, y, z, u, v;
};

void emit(std::vector<Vertex>& out, const Corner (&quad)[4], const float normal[3],
          uint32_t part) {
    static constexpr int kOrder[6] = {0, 1, 2, 0, 2, 3};
    for (const int i : kOrder) {
        Vertex vertex{};
        vertex.position[0] = quad[i].x / 16.0f;
        vertex.position[1] = quad[i].y / 16.0f;
        vertex.position[2] = quad[i].z / 16.0f;
        vertex.uv[0] = quad[i].u;
        vertex.uv[1] = quad[i].v;
        for (int axis = 0; axis < 3; ++axis) {
            vertex.normal[axis] = static_cast<int8_t>(normal[axis] * 127.0f);
        }
        vertex.part = part;
        out.push_back(vertex);
    }
}

// `ModelPart.Cube.Cube`, `ModelPart.Polygon.Polygon`: corners, faces, UVs.
// The game's cube: eight corners, six faces in a fixed order, each face's
// rectangle cut from the box's island, and a mirrored box flipping x and
// reversing every face's winding.
void bake_box(const Box& box, uint32_t part, std::vector<Vertex>& out) {
    float x0 = box.origin[0];
    float y0 = box.origin[1];
    float z0 = box.origin[2];
    float x1 = x0 + box.size[0];
    float y1 = y0 + box.size[1];
    float z1 = z0 + box.size[2];
    x0 -= box.grow[0];
    y0 -= box.grow[1];
    z0 -= box.grow[2];
    x1 += box.grow[0];
    y1 += box.grow[1];
    z1 += box.grow[2];
    if (box.mirror) std::swap(x0, x1);

    const Corner c0{x0, y0, z0, 0, 0}, c1{x1, y0, z0, 0, 0}, c2{x1, y1, z0, 0, 0},
        c3{x0, y1, z0, 0, 0}, c4{x0, y0, z1, 0, 0}, c5{x1, y0, z1, 0, 0}, c6{x1, y1, z1, 0, 0},
        c7{x0, y1, z1, 0, 0};

    const float w = box.size[0];
    const float h = box.size[1];
    const float d = box.size[2];
    const auto u = static_cast<float>(box.uv[0]);
    const auto v = static_cast<float>(box.uv[1]);
    const float u0 = u, u1 = u + d, u2 = u + d + w, u3 = u + d + w + w, u4 = u + d + w + d,
                u5 = u + d + w + d + w;
    const float v0 = v, v1 = v + d, v2 = v + d + h;

    const auto face = [&](Face which, Corner a, Corner b, Corner c, Corner e, float left,
                          float top, float right, float bottom) {
        if ((box.faces & (1u << which)) == 0) return;
        const float tw = box.texture_size[0];
        const float th = box.texture_size[1];
        Corner quad[4] = {a, b, c, e};
        quad[0].u = right / tw;
        quad[0].v = top / th;
        quad[1].u = left / tw;
        quad[1].v = top / th;
        quad[2].u = left / tw;
        quad[2].v = bottom / th;
        quad[3].u = right / tw;
        quad[3].v = bottom / th;
        if (box.mirror) {
            std::swap(quad[0], quad[3]);
            std::swap(quad[1], quad[2]);
        }
        static constexpr float kNormals[6][3] = {{0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                                 {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
        float normal[3] = {kNormals[which][0], kNormals[which][1], kNormals[which][2]};
        if (box.mirror) normal[0] = -normal[0];
        emit(out, quad, normal, part);
    };
    face(Down, c5, c4, c0, c1, u1, v0, u2, v1);
    face(Up, c2, c3, c7, c6, u2, v1, u3, v0);
    face(West, c0, c4, c7, c3, u0, v1, u1, v2);
    face(North, c1, c0, c3, c2, u1, v1, u2, v2);
    face(East, c5, c1, c2, c6, u2, v1, u4, v2);
    face(South, c4, c5, c6, c7, u4, v1, u5, v2);
}

uint8_t parse_faces(Reader& reader) {
    uint8_t mask = 0;
    for (const char c : reader.word()) {
        switch (c) {
        case 'd': mask |= 1u << Down; break;
        case 'u': mask |= 1u << Up; break;
        case 'n': mask |= 1u << North; break;
        case 's': mask |= 1u << South; break;
        case 'w': mask |= 1u << West; break;
        case 'e': mask |= 1u << East; break;
        default: reader.fail(std::format("'{}' is no face", c));
        }
    }
    return mask;
}

} // namespace

int32_t Model::find(std::string_view path) const {
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].path == path) return static_cast<int32_t>(i);
    }
    return -1;
}

void ModelSet::parse(std::string_view text) {
    Model* model = nullptr;
    size_t line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (const size_t hash = line.find(" #"); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        Reader reader{words(line), 0, line_number};
        if (reader.done() || reader.tokens[0].starts_with('#')) continue;
        const auto head = reader.word();
        if (head.starts_with('[')) {
            if (!head.ends_with(']') || reader.tokens.size() != 1) reader.fail("bad section");
            models_.push_back({});
            model = &models_.back();
            model->name = std::string(head.substr(1, head.size() - 2));
            continue;
        }
        if (model == nullptr) reader.fail("outside any [model]");
        if (head == "texture") {
            model->texture_width = static_cast<uint32_t>(reader.number());
            model->texture_height = static_cast<uint32_t>(reader.number());
        } else if (head == "part") {
            Part part;
            part.path = std::string(reader.word());
            if (const size_t slash = part.path.rfind('/'); slash != std::string::npos) {
                part.parent = model->find(std::string_view(part.path).substr(0, slash));
                if (part.parent < 0) reader.fail("part before its parent");
            } else if (part.path != kRootPath) {
                part.parent = model->find(kRootPath);
            }
            while (!reader.done()) {
                const auto key = reader.word();
                if (key == "pivot") {
                    const auto p = reader.three();
                    part.initial.x = p[0];
                    part.initial.y = p[1];
                    part.initial.z = p[2];
                } else if (key == "rotate") {
                    const auto r = reader.three();
                    part.initial.x_rot = r[0];
                    part.initial.y_rot = r[1];
                    part.initial.z_rot = r[2];
                } else if (key == "scale") {
                    const auto s = reader.three();
                    part.initial.x_scale = s[0];
                    part.initial.y_scale = s[1];
                    part.initial.z_scale = s[2];
                } else {
                    reader.fail(std::format("unknown part key '{}'", key));
                }
            }
            model->parts.push_back(std::move(part));
        } else if (head == "box") {
            if (model->parts.empty()) reader.fail("box before any part");
            Box box;
            box.origin = reader.three();
            box.size = reader.three();
            box.texture_size = {static_cast<float>(model->texture_width),
                                static_cast<float>(model->texture_height)};
            while (!reader.done()) {
                const auto key = reader.word();
                if (key == "uv") {
                    box.uv = {static_cast<uint16_t>(reader.number()),
                              static_cast<uint16_t>(reader.number())};
                } else if (key == "grow") {
                    const float g = reader.number();
                    box.grow = {g, g, g};
                    if (!reader.done() && reader.tokens[reader.at] != "mirror" &&
                        reader.tokens[reader.at] != "texsize" && reader.tokens[reader.at] != "faces") {
                        box.grow[1] = reader.number();
                        box.grow[2] = reader.number();
                    }
                } else if (key == "mirror") {
                    box.mirror = true;
                } else if (key == "texsize") {
                    box.texture_size = {reader.number(), reader.number()};
                } else if (key == "faces") {
                    box.faces = parse_faces(reader);
                } else {
                    reader.fail(std::format("unknown box key '{}'", key));
                }
            }
            model->parts.back().boxes.push_back(box);
        } else {
            reader.fail(std::format("unknown line '{}'", head));
        }
    }
}

const Model* ModelSet::find(std::string_view name) const {
    for (const auto& model : models_) {
        if (model.name == name) return &model;
    }
    return nullptr;
}

// `Model.resetPose`: every part back to its initial pose.
void Pose::reset(const Model& model) {
    parts.resize(model.parts.size());
    for (size_t i = 0; i < parts.size(); ++i) parts[i] = model.parts[i].initial;
}

Matrix identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[column * 4 + k];
            out[column * 4 + row] = sum;
        }
    }
    return out;
}

Matrix translation(float x, float y, float z) {
    Matrix m = identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}

Matrix scaling(float x, float y, float z) {
    Matrix m = identity();
    m[0] = x;
    m[5] = y;
    m[10] = z;
    return m;
}

Matrix rotation_x(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Matrix m = identity();
    m[5] = c;
    m[6] = s;
    m[9] = -s;
    m[10] = c;
    return m;
}

Matrix rotation_y(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Matrix m = identity();
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
    return m;
}

Matrix rotation_z(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Matrix m = identity();
    m[0] = c;
    m[1] = s;
    m[4] = -s;
    m[5] = c;
    return m;
}

Matrix rotation_zyx(float z, float y, float x) {
    const float cz = std::cos(z), sz = std::sin(z);
    const float cy = std::cos(y), sy = std::sin(y);
    const float cx = std::cos(x), sx = std::sin(x);
    // Rz * Ry * Rx, written out.
    Matrix m = identity();
    m[0] = cz * cy;
    m[1] = sz * cy;
    m[2] = -sy;
    m[4] = cz * sy * sx - sz * cx;
    m[5] = sz * sy * sx + cz * cx;
    m[6] = cy * sx;
    m[8] = cz * sy * cx + sz * sx;
    m[9] = sz * sy * cx - cz * sx;
    m[10] = cy * cx;
    return m;
}

uint32_t bake(const Model& model, std::vector<Vertex>& out) {
    const size_t before = out.size();
    for (size_t i = 0; i < model.parts.size(); ++i) {
        for (const Box& box : model.parts[i].boxes) bake_box(box, static_cast<uint32_t>(i), out);
    }
    return static_cast<uint32_t>(out.size() - before);
}


namespace {

// The fused multiply and add the matrix library asks for, as the game gets
// it: useMathFma is off by default, so it is a plain multiply and an add, each
// rounded on its own.
float fma(float a, float b, float c) { return a * b + c; }

// The cosine that belongs with a sine already in hand, the way the matrix
// library finds it: a square root and the quadrant.
float cos_from_sin(float sine, float angle) {
    const float cosine = std::sqrt(1.0f - sine * sine);
    const float a = angle + kHalfPi;
    float b = a - static_cast<float>(static_cast<int32_t>(a / kTwoPi)) * kTwoPi;
    if (b < 0.0f) b = kTwoPi + b;
    return b >= kPi ? -cosine : cosine;
}

bool abs_equals_one(float value) { return std::fabs(value) == 1.0f; }

} // namespace

std::array<float, 4> quaternion_x(float radians) {
    const float half = radians * 0.5f;
    const float s = std::sin(half);
    return {s, 0.0f, 0.0f, cos_from_sin(s, half)};
}

std::array<float, 4> quaternion_y(float radians) {
    const float half = radians * 0.5f;
    const float s = std::sin(half);
    return {0.0f, s, 0.0f, cos_from_sin(s, half)};
}

std::array<float, 4> quaternion_z(float radians) {
    const float half = radians * 0.5f;
    const float s = std::sin(half);
    return {0.0f, 0.0f, s, cos_from_sin(s, half)};
}

// The turn an item transform and a face's own rotation ask for: x, then y,
// then z, all from half angles.
std::array<float, 4> quaternion_xyz(float x, float y, float z) {
    const float hx = x * 0.5f, hy = y * 0.5f, hz = z * 0.5f;
    const float sx = std::sin(hx), sy = std::sin(hy), sz = std::sin(hz);
    const float cx = cos_from_sin(sx, hx), cy = cos_from_sin(sy, hy), cz = cos_from_sin(sz, hz);
    const float cycz = cy * cz, sysz = sy * sz, sycz = sy * cz, cysz = cy * sz;
    return {sx * cycz + cx * sysz, cx * sycz - sx * cysz, cx * cysz + sx * sycz,
            cx * cycz - sx * sysz};
}

// `Axis.rotationDegrees`: degrees through a float on their way to a turn.
float to_radians(float degrees) { return degrees * static_cast<float>(M_PI / 180.0); }

// `PoseStack.Pose.translate`: an identity matrix just takes the offset;
// anything else gets a fused term per axis in the last column.
void Placement::translate(float x, float y, float z) {
    if (shape & kIdentity) {
        matrix = identity();
        matrix[12] = x;
        matrix[13] = y;
        matrix[14] = z;
        shape = kAffine | kTranslation | kOrthonormal;
        return;
    }
    for (int row = 0; row < 4; ++row) {
        matrix[12 + row] = fma(matrix[row], x,
                               fma(matrix[4 + row], y, fma(matrix[8 + row], z, matrix[12 + row])));
    }
    shape &= static_cast<uint8_t>(~(kPerspective | kIdentity));
}

// `PoseStack.Pose.scale`.
void Placement::scale(float x, float y, float z) {
    const bool one = abs_equals_one(x) && abs_equals_one(y) && abs_equals_one(z);
    if (shape & kIdentity) {
        matrix = identity();
        matrix[0] = x;
        matrix[5] = y;
        matrix[10] = z;
        shape = static_cast<uint8_t>(kAffine | (one ? kOrthonormal : 0));
        return;
    }
    const float factor[3] = {x, y, z};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 4; ++row) matrix[column * 4 + row] *= factor[column];
    }
    shape &= static_cast<uint8_t>(
        ~(kPerspective | kIdentity | kTranslation | (one ? 0 : kOrthonormal)));
}

// `PoseStack.Pose.rotate`: the quaternion as a basis, then this matrix times
// it, down whichever of the four routes the matrix's shape allows.
void Placement::rotate(const std::array<float, 4>& quaternion) {
    const float qx = quaternion[0], qy = quaternion[1], qz = quaternion[2], qw = quaternion[3];
    const float w2 = qw * qw, x2 = qx * qx, y2 = qy * qy, z2 = qz * qz;
    const float zw = qz * qw, dzw = zw + zw, xy = qx * qy, dxy = xy + xy;
    const float xz = qx * qz, dxz = xz + xz, yw = qy * qw, dyw = yw + yw;
    const float yz = qy * qz, dyz = yz + yz, xw = qx * qw, dxw = xw + xw;
    const float r[3][3] = {{w2 + x2 - z2 - y2, dxy + dzw, dxz - dyw},
                           {-dzw + dxy, y2 - z2 + w2 - x2, dyz + dxw},
                           {dyw + dxz, dyz - dxw, z2 - y2 - x2 + w2}};
    if (shape & kIdentity) {
        matrix = identity();
        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 3; ++row) matrix[column * 4 + row] = r[column][row];
        }
        shape = kAffine | kOrthonormal;
        return;
    }
    if (shape & kTranslation) {
        const Matrix kept = matrix;
        matrix = Matrix{};
        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 3; ++row) matrix[column * 4 + row] = r[column][row];
        }
        matrix[12] = kept[12];
        matrix[13] = kept[13];
        matrix[14] = kept[14];
        matrix[15] = kept[15];
        shape &= static_cast<uint8_t>(~(kPerspective | kIdentity | kTranslation));
        return;
    }
    Matrix out = matrix;
    const bool affine = (shape & kAffine) != 0;
    for (int column = 0; column < 3; ++column) {
        const int rows = affine ? 3 : 4;
        for (int row = 0; row < rows; ++row) {
            out[column * 4 + row] =
                affine ? matrix[row] * r[column][0] + matrix[4 + row] * r[column][1] +
                             matrix[8 + row] * r[column][2]
                       : fma(matrix[row], r[column][0],
                             fma(matrix[4 + row], r[column][1], matrix[8 + row] * r[column][2]));
        }
        if (affine) out[column * 4 + 3] = 0.0f;
    }
    matrix = out;
    shape &= static_cast<uint8_t>(~(kPerspective | kIdentity | kTranslation));
}

void pose_matrices(const Model& model, const Pose& pose, const Matrix& root,
                   std::span<Matrix> out) {
    static constexpr size_t kMaxParts = 256;
    if (model.parts.size() > kMaxParts) throw std::runtime_error("model has too many parts");
    std::array<Matrix, kMaxParts> chain;
    std::array<bool, kMaxParts> shown;
    for (size_t i = 0; i < model.parts.size(); ++i) {
        const Part& part = model.parts[i];
        const PartPose& p = pose.parts[i];
        const auto parent = static_cast<size_t>(part.parent);
        shown[i] = p.visible && (part.parent < 0 || shown[parent]);
        if (!shown[i]) {
            out[i] = Matrix{};
            continue;
        }
        Matrix m = part.parent < 0 ? root : chain[parent];
        // `ModelPart.translateAndRotate`: translate, rotate ZYX, then scale.
        if (p.x != 0.0f || p.y != 0.0f || p.z != 0.0f) {
            m = multiply(m, translation(p.x / 16.0f, p.y / 16.0f, p.z / 16.0f));
        }
        if (p.x_rot != 0.0f || p.y_rot != 0.0f || p.z_rot != 0.0f) {
            m = multiply(m, rotation_zyx(p.z_rot, p.y_rot, p.x_rot));
        }
        if (p.x_scale != 1.0f || p.y_scale != 1.0f || p.z_scale != 1.0f) {
            m = multiply(m, scaling(p.x_scale, p.y_scale, p.z_scale));
        }
        chain[i] = m;
        out[i] = p.skip_draw ? Matrix{} : m;
    }
}

} // namespace mcpp::entity
