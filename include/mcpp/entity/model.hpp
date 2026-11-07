#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::entity {

// The game's model space, kept as the game builds it so every number can be
// checked against the exported entity_models table: pixels, y pointing down, the model
// facing -z with its own right at -x.
struct Box {
    std::array<float, 3> origin{};
    std::array<float, 3> size{};
    std::array<float, 3> grow{};
    std::array<uint16_t, 2> uv{};
    // The width and height its uvs are divided by: the layer's texture size,
    // unless the box scales its own.
    std::array<float, 2> texture_size{64.0f, 64.0f};
    bool mirror = false;
    // Down, up, north, south, west, east; a box can leave faces out.
    uint8_t faces = 0x3F;
};

// Where a part sits relative to its parent and how it is turned: what the
// animation writes every frame, starting from the layer's own values.
struct PartPose {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float x_rot = 0.0f, y_rot = 0.0f, z_rot = 0.0f;
    float x_scale = 1.0f, y_scale = 1.0f, z_scale = 1.0f;
    bool visible = true;
    // Its own boxes hidden, its children still drawn.
    bool skip_draw = false;
};

struct Part {
    std::string path;   // "head/hat"
    int32_t parent = -1;
    PartPose initial;
    std::vector<Box> boxes;
};

// The path of a layer's own root. A layer may have a part named "root" of its
// own - the allay, the frog, the vex, the creaking and the nautilus all do -
// so the root itself cannot be called that.
inline constexpr std::string_view kRootPath = ".";

// One layer as the game builds it ("player", "zombie#outer_armor"), parents
// always before their children.
struct Model {
    std::string name;
    uint32_t texture_width = 64;
    uint32_t texture_height = 64;
    std::vector<Part> parts;

    // By path, "right_arm" or "head/hat"; -1 when there is none.
    int32_t find(std::string_view path) const;
};

class ModelSet {
public:
    // Throws with the line number on anything it does not understand.
    void parse(std::string_view text);
    const Model* find(std::string_view name) const;
    const std::vector<Model>& all() const { return models_; }

private:
    std::vector<Model> models_;
};

// A model's whole pose, one entry per part, reset from the layer every frame.
struct Pose {
    std::vector<PartPose> parts;
    Pose() = default;
    explicit Pose(const Model& model) { reset(model); }
    void reset(const Model& model);
    PartPose& operator[](int32_t index) { return parts[static_cast<size_t>(index)]; }
    const PartPose& operator[](int32_t index) const { return parts[static_cast<size_t>(index)]; }
};

// A vertex of a baked model: in its part's own space, in blocks, with the
// part it hangs from, so one buffer holds every model and the gpu poses it.
struct Vertex {
    float position[3];
    float uv[2];
    int8_t normal[4];   // w unused
    uint32_t part;
};
static_assert(sizeof(Vertex) == 28);

// Column-major 4x4.
using Matrix = std::array<float, 16>;
Matrix identity();
Matrix multiply(const Matrix& a, const Matrix& b);
Matrix translation(float x, float y, float z);
Matrix scaling(float x, float y, float z);
// Rotations by radians about one axis, and the game's z-then-y-then-x turn.
Matrix rotation_x(float radians);
Matrix rotation_y(float radians);
Matrix rotation_z(float radians);
Matrix rotation_zyx(float z, float y, float x);

// Every box's faces as two triangles, in the order and with the uvs the game
// gives them, all in part space. Appends; returns how many vertices.
uint32_t bake(const Model& model, std::vector<Vertex>& out);

// `PoseStack.Pose.translate` and the matrix under it, to the bit. The game builds a
// placement one operation at a time, in single precision, and takes a shorter
// route through each one when the matrix still has a shape it recognises; a
// plain 4x4 multiply gets the same answer but not always the same last bit,
// and an oracle that compares with the game cares.
struct Placement {
    // What the matrix still is, as `Matrix4fc` counts it.
    enum Shape : uint8_t { kPerspective = 1, kAffine = 2, kIdentity = 4, kTranslation = 8,
                           kOrthonormal = 16 };
    Matrix matrix = identity();
    uint8_t shape = kIdentity | kAffine | kTranslation | kOrthonormal;

    void translate(float x, float y, float z);
    void scale(float x, float y, float z);
    // x, y, z, w.
    void rotate(const std::array<float, 4>& quaternion);
};

// `Axis.rotation`: the turns a renderer asks a pose stack for, built from
// half angles the way the matrix library builds them.
std::array<float, 4> quaternion_x(float radians);
std::array<float, 4> quaternion_y(float radians);
std::array<float, 4> quaternion_z(float radians);
std::array<float, 4> quaternion_xyz(float x, float y, float z);
// Degrees through a float, which is what `rotationDegrees` passes on.
float to_radians(float degrees);

// Each part's matrix from part space to where `root` puts the model, in the
// order of model.parts. A hidden part, and everything below it, gets a zero
// matrix, which draws nothing.
void pose_matrices(const Model& model, const Pose& pose, const Matrix& root,
                   std::span<Matrix> out);

} // namespace mcpp::entity
