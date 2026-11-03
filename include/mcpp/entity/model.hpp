#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::entity {

// The game's model space, kept as the game has it so that every number in the
// data file can be checked against the skin layout: pixels, y pointing down,
// the model facing -z, its own right at -x, and the feet 24 pixels below the
// origin.
struct Box {
    std::array<float, 3> origin{};   // the corner nearest -x, -y, -z
    std::array<float, 3> size{};
    float inflate = 0.0f;            // grows every side, leaving the uvs alone
    std::array<float, 2> uv{};       // the top left of the box's texture island
    bool mirror = false;
};

struct Part {
    std::string name;
    int32_t parent = -1;
    std::array<float, 3> pivot{};
    std::array<float, 3> rotation{};   // radians, applied z, then y, then x
    std::vector<Box> boxes;
};

struct Model {
    std::string name;
    std::string texture;   // under textures/entity/, without the extension
    uint32_t texture_width = 64;
    uint32_t texture_height = 64;
    float scale = 1.0f;
    // The entity's box, for its shadow and where its name floats.
    float width = 0.6f;
    float height = 1.8f;
    float shadow = 0.5f;
    std::vector<Part> parts;

    int32_t find(std::string_view part) const;
};

// The models of every entity, read from data/entity_models.txt.
class ModelSet {
public:
    // Throws with the line number on anything it does not understand.
    void parse(std::string_view text);
    const Model* find(std::string_view name) const;
    const std::vector<Model>& all() const { return models_; }

private:
    std::vector<Model> models_;
};

// How the animation moved a part this frame, on top of where the model puts
// it. Rotations add to the model's own; the offset is in pixels.
struct PartPose {
    std::array<float, 3> rotation{};
    std::array<float, 3> offset{};
    bool visible = true;
    // A part whose rotation replaces the model's rather than adding to it.
    bool absolute = false;
};

struct Pose {
    std::vector<PartPose> parts;
    explicit Pose(const Model& model) : parts(model.parts.size()) {}
    PartPose& operator[](int32_t index) { return parts[static_cast<size_t>(index)]; }
};

struct Vertex {
    float position[3];
    float uv[2];
    float normal[3];
};

// Column-major 4x4, the way the shaders take them.
using Matrix = std::array<float, 16>;
Matrix identity();
Matrix multiply(const Matrix& a, const Matrix& b);
Matrix translation(float x, float y, float z);
Matrix scaling(float x, float y, float z);
Matrix rotation_x(float radians);
Matrix rotation_y(float radians);
Matrix rotation_z(float radians);

// Where the model's origin sits in the world for an entity standing at the
// origin and turned to the given body yaw in degrees: the game turns the
// model, flips it upright and lifts it so the feet are on the ground.
Matrix placement(float body_yaw_degrees, float scale);

// Six faces per box, two triangles per face, in world space. Parts the pose
// hides are left out, and so are their children.
void build_mesh(const Model& model, const Pose& pose, const Matrix& transform,
                std::vector<Vertex>& out);
// Only the parts named, for drawing one layer of a model on its own.
void build_mesh(const Model& model, const Pose& pose, const Matrix& transform,
                std::span<const int32_t> parts, std::vector<Vertex>& out);

} // namespace mcpp::entity
