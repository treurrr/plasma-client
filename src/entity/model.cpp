#include "mcpp/entity/model.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <stdexcept>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979f;

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

// The corners of each face as they are seen from outside, top left first, in
// the orientation the skin's texture island has.
struct Face {
    int corners[4][3];   // 0 picks the low side of an axis, 1 the high
    float normal[3];
};

constexpr Face kFaces[6] = {
    {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {0, 0, -1}},   // front
    {{{1, 0, 1}, {0, 0, 1}, {0, 1, 1}, {1, 1, 1}}, {0, 0, 1}},    // back
    {{{0, 0, 1}, {0, 0, 0}, {0, 1, 0}, {0, 1, 1}}, {-1, 0, 0}},   // own right
    {{{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}}, {1, 0, 0}},    // own left
    {{{0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}}, {0, -1, 0}},   // top
    {{{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}}, {0, 1, 0}},    // bottom
};

// Where each face's rectangle sits in a box's island of (2d + 2w) by (d + h):
// the top and bottom in the first row, then right, front, left and back.
void face_uv(int face, const Box& box, float out[4]) {
    const float u = box.uv[0];
    const float v = box.uv[1];
    const float w = box.size[0];
    const float h = box.size[1];
    const float d = box.size[2];
    switch (face) {
    case 0: out[0] = u + d;         out[1] = v + d; out[2] = u + d + w;         out[3] = v + d + h; break;
    case 1: out[0] = u + 2 * d + w; out[1] = v + d; out[2] = u + 2 * d + 2 * w; out[3] = v + d + h; break;
    case 2: out[0] = u;             out[1] = v + d; out[2] = u + d;             out[3] = v + d + h; break;
    case 3: out[0] = u + d + w;     out[1] = v + d; out[2] = u + 2 * d + w;     out[3] = v + d + h; break;
    case 4: out[0] = u + d;         out[1] = v;     out[2] = u + d + w;         out[3] = v + d;     break;
    default: out[0] = u + d + w;    out[1] = v;     out[2] = u + d + 2 * w;     out[3] = v + d;     break;
    }
}

void apply(const Matrix& m, const float in[3], float w, float out[3]) {
    for (int row = 0; row < 3; ++row) {
        out[row] = m[0 * 4 + row] * in[0] + m[1 * 4 + row] * in[1] + m[2 * 4 + row] * in[2] +
                   m[3 * 4 + row] * w;
    }
}

void emit_box(const Box& box, const Matrix& m, float texture_width, float texture_height,
              std::vector<Vertex>& out) {
    const float low[3] = {box.origin[0] - box.inflate, box.origin[1] - box.inflate,
                          box.origin[2] - box.inflate};
    const float high[3] = {box.origin[0] + box.size[0] + box.inflate,
                           box.origin[1] + box.size[1] + box.inflate,
                           box.origin[2] + box.size[2] + box.inflate};
    for (int face = 0; face < 6; ++face) {
        const Face& f = kFaces[face];
        float uv[4];
        face_uv(face, box, uv);
        const float corner_uv[4][2] = {{uv[0], uv[1]}, {uv[2], uv[1]}, {uv[2], uv[3]},
                                       {uv[0], uv[3]}};
        Vertex corners[4];
        for (int c = 0; c < 4; ++c) {
            float local[3];
            for (int axis = 0; axis < 3; ++axis) {
                local[axis] = f.corners[c][axis] == 0 ? low[axis] : high[axis];
            }
            // A mirrored box is the plain one reflected across its own middle,
            // which carries its left face's texture to the right and back.
            if (box.mirror) local[0] = low[0] + high[0] - local[0];
            apply(m, local, 1.0f, corners[c].position);
            float normal[3] = {f.normal[0] * (box.mirror ? -1.0f : 1.0f), f.normal[1],
                               f.normal[2]};
            apply(m, normal, 0.0f, corners[c].normal);
            const float length = std::sqrt(corners[c].normal[0] * corners[c].normal[0] +
                                           corners[c].normal[1] * corners[c].normal[1] +
                                           corners[c].normal[2] * corners[c].normal[2]);
            for (float& n : corners[c].normal) n /= length > 0.0f ? length : 1.0f;
            corners[c].uv[0] = corner_uv[c][0] / texture_width;
            corners[c].uv[1] = corner_uv[c][1] / texture_height;
        }
        static constexpr int kPlain[6] = {0, 1, 2, 0, 2, 3};
        static constexpr int kReflected[6] = {0, 2, 1, 0, 3, 2};
        for (const int c : box.mirror ? kReflected : kPlain) out.push_back(corners[c]);
    }
}

} // namespace

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

Matrix placement(float body_yaw_degrees, float scale) {
    Matrix m = rotation_y((180.0f - body_yaw_degrees) * kPi / 180.0f);
    m = multiply(m, scaling(-scale, -scale, scale));
    // A thousandth of a block above the ground, so the soles do not fight
    // with the block they stand on.
    m = multiply(m, translation(0.0f, -1.501f, 0.0f));
    return multiply(m, scaling(1.0f / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f));
}

int32_t Model::find(std::string_view part) const {
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].name == part) return static_cast<int32_t>(i);
    }
    return -1;
}

void ModelSet::parse(std::string_view text) {
    Model* model = nullptr;
    Part* part = nullptr;
    Reader reader;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = std::min(text.find('\n', start), text.size());
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ++reader.line;
        if (const size_t hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        reader.tokens = words(line);
        reader.at = 0;
        if (reader.tokens.empty()) continue;

        const auto keyword = reader.word();
        if (keyword.starts_with('[')) {
            if (!keyword.ends_with(']') || keyword.size() < 3) reader.fail("bad section name");
            models_.push_back(Model{});
            model = &models_.back();
            model->name = std::string(keyword.substr(1, keyword.size() - 2));
            part = nullptr;
            continue;
        }
        if (model == nullptr) reader.fail("outside any [section]");
        if (keyword == "texture") {
            model->texture = std::string(reader.word());
            model->texture_width = static_cast<uint32_t>(reader.number());
            model->texture_height = static_cast<uint32_t>(reader.number());
        } else if (keyword == "scale") {
            model->scale = reader.number();
        } else if (keyword == "size") {
            model->width = reader.number();
            model->height = reader.number();
        } else if (keyword == "shadow") {
            model->shadow = reader.number();
        } else if (keyword == "part") {
            Part next;
            next.name = std::string(reader.word());
            if (model->find(next.name) >= 0) reader.fail(std::format("part {} twice", next.name));
            while (!reader.done()) {
                const auto option = reader.word();
                if (option == "parent") {
                    const auto parent = reader.word();
                    next.parent = model->find(parent);
                    if (next.parent < 0) reader.fail(std::format("no part {} before this", parent));
                } else if (option == "pivot") {
                    next.pivot = reader.three();
                } else if (option == "rotate") {
                    next.rotation = reader.three();
                    for (float& angle : next.rotation) angle *= kPi / 180.0f;
                } else {
                    reader.fail(std::format("unknown part option '{}'", option));
                }
            }
            model->parts.push_back(std::move(next));
            part = &model->parts.back();
        } else if (keyword == "box") {
            if (part == nullptr) reader.fail("box before any part");
            Box box;
            box.origin = reader.three();
            box.size = reader.three();
            while (!reader.done()) {
                const auto option = reader.word();
                if (option == "uv") {
                    box.uv = {reader.number(), reader.number()};
                } else if (option == "inflate") {
                    box.inflate = reader.number();
                } else if (option == "mirror") {
                    box.mirror = true;
                } else {
                    reader.fail(std::format("unknown box option '{}'", option));
                }
            }
            part->boxes.push_back(box);
        } else {
            reader.fail(std::format("unknown keyword '{}'", keyword));
        }
    }
}

const Model* ModelSet::find(std::string_view name) const {
    for (const auto& model : models_) {
        if (model.name == name) return &model;
    }
    return nullptr;
}

void build_mesh(const Model& model, const Pose& pose, const Matrix& transform,
                std::vector<Vertex>& out) {
    build_mesh(model, pose, transform, {}, out);
}

void build_mesh(const Model& model, const Pose& pose, const Matrix& transform,
                std::span<const int32_t> only, std::vector<Vertex>& out) {
    std::vector<Matrix> placed(model.parts.size());
    std::vector<bool> shown(model.parts.size());
    for (size_t i = 0; i < model.parts.size(); ++i) {
        const Part& part = model.parts[i];
        const PartPose& moved = pose.parts[i];
        const bool parent_shown = part.parent < 0 || shown[static_cast<size_t>(part.parent)];
        shown[i] = parent_shown && moved.visible;
        const Matrix& base = part.parent < 0 ? transform : placed[static_cast<size_t>(part.parent)];
        Matrix m = multiply(base, translation(part.pivot[0] + moved.offset[0],
                                              part.pivot[1] + moved.offset[1],
                                              part.pivot[2] + moved.offset[2]));
        std::array<float, 3> angle = moved.rotation;
        if (!moved.absolute) {
            for (int axis = 0; axis < 3; ++axis) angle[axis] += part.rotation[axis];
        }
        if (angle[2] != 0.0f) m = multiply(m, rotation_z(angle[2]));
        if (angle[1] != 0.0f) m = multiply(m, rotation_y(angle[1]));
        if (angle[0] != 0.0f) m = multiply(m, rotation_x(angle[0]));
        placed[i] = m;
    }
    const auto wanted = [&](size_t index) {
        if (only.empty()) return true;
        for (const int32_t one : only) {
            if (static_cast<size_t>(one) == index) return true;
        }
        return false;
    };
    for (size_t i = 0; i < model.parts.size(); ++i) {
        if (!shown[i] || !wanted(i)) continue;
        for (const Box& box : model.parts[i].boxes) {
            emit_box(box, placed[i], static_cast<float>(model.texture_width),
                     static_cast<float>(model.texture_height), out);
        }
    }
}

} // namespace mcpp::entity
