#include "mcpp/entity/objects.hpp"

#include "mcpp/entity/held_items.hpp"
#include "mcpp/jmath.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
using Type = gpu::EntityRenderer::Type;
using Kind = ObjectKind;

// `EntityRenderers`: every type above, with the layer and the picture its
// renderer is built with. The boats and the rafts each have a wood of their
// own; `AbstractBoatRenderer` is the same code for all of them.
constexpr ObjectType kTypes[] = {
    {"minecraft:acacia_boat", Kind::Boat, "boat/acacia", "boat/acacia", 0.8f},
    {"minecraft:acacia_chest_boat", Kind::Boat, "chest_boat/acacia", "chest_boat/acacia", 0.8f},
    {"minecraft:arrow", Kind::Arrow, "arrow", "projectiles/arrow", 0.0f},
    {"minecraft:bamboo_chest_raft", Kind::Boat, "chest_boat/bamboo", "chest_boat/bamboo", 0.8f},
    {"minecraft:bamboo_raft", Kind::Boat, "boat/bamboo", "boat/bamboo", 0.8f},
    {"minecraft:birch_boat", Kind::Boat, "boat/birch", "boat/birch", 0.8f},
    {"minecraft:birch_chest_boat", Kind::Boat, "chest_boat/birch", "chest_boat/birch", 0.8f},
    {"minecraft:block_display", Kind::BlockDisplay, {}, {}, 0.0f},
    {"minecraft:cherry_boat", Kind::Boat, "boat/cherry", "boat/cherry", 0.8f},
    {"minecraft:cherry_chest_boat", Kind::Boat, "chest_boat/cherry", "chest_boat/cherry", 0.8f},
    {"minecraft:chest_minecart", Kind::Minecart, "chest_minecart", "minecart", 0.7f},
    {"minecraft:command_block_minecart", Kind::Minecart, "command_block_minecart", "minecart",
     0.7f},
    {"minecraft:dark_oak_boat", Kind::Boat, "boat/dark_oak", "boat/dark_oak", 0.8f},
    {"minecraft:dark_oak_chest_boat", Kind::Boat, "chest_boat/dark_oak", "chest_boat/dark_oak",
     0.8f},
    {"minecraft:dragon_fireball", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, true,
     "minecraft:dragon_breath"},
    {"minecraft:egg", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, false, "minecraft:egg"},
    {"minecraft:ender_pearl", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, false,
     "minecraft:ender_pearl"},
    {"minecraft:experience_bottle", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, false,
     "minecraft:experience_bottle"},
    {"minecraft:experience_orb", Kind::ExperienceOrb, {}, "$experience_orb", 0.15f},
    {"minecraft:falling_block", Kind::FallingBlock, {}, {}, 0.5f},
    {"minecraft:fireball", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, true, "minecraft:fire_charge"},
    {"minecraft:firework_rocket", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, false,
     "minecraft:firework_rocket"},
    {"minecraft:fishing_bobber", Kind::FishingBobber, {}, "$fishing_hook", 0.0f},
    {"minecraft:furnace_minecart", Kind::Minecart, "furnace_minecart", "minecart", 0.7f},
    {"minecraft:hopper_minecart", Kind::Minecart, "hopper_minecart", "minecart", 0.7f},
    {"minecraft:item", Kind::Item, {}, {}, 0.15f},
    {"minecraft:item_display", Kind::ItemDisplay, {}, {}, 0.0f},
    {"minecraft:jungle_boat", Kind::Boat, "boat/jungle", "boat/jungle", 0.8f},
    {"minecraft:jungle_chest_boat", Kind::Boat, "chest_boat/jungle", "chest_boat/jungle", 0.8f},
    {"minecraft:leash_knot", Kind::LeashKnot, "leash_knot", "lead_knot", 0.0f},
    {"minecraft:lingering_potion", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, false,
     "minecraft:lingering_potion"},
    {"minecraft:llama_spit", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, false, {}},
    {"minecraft:mangrove_boat", Kind::Boat, "boat/mangrove", "boat/mangrove", 0.8f},
    {"minecraft:mangrove_chest_boat", Kind::Boat, "chest_boat/mangrove", "chest_boat/mangrove",
     0.8f},
    {"minecraft:minecart", Kind::Minecart, "minecart", "minecart", 0.7f},
    {"minecraft:oak_boat", Kind::Boat, "boat/oak", "boat/oak", 0.8f},
    {"minecraft:oak_chest_boat", Kind::Boat, "chest_boat/oak", "chest_boat/oak", 0.8f},
    {"minecraft:ominous_item_spawner", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, false, {}},
    {"minecraft:pale_oak_boat", Kind::Boat, "boat/pale_oak", "boat/pale_oak", 0.8f},
    {"minecraft:pale_oak_chest_boat", Kind::Boat, "chest_boat/pale_oak", "chest_boat/pale_oak",
     0.8f},
    {"minecraft:snowball", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, false, "minecraft:snowball"},
    {"minecraft:spawner_minecart", Kind::Minecart, "spawner_minecart", "minecart", 0.7f},
    {"minecraft:spectral_arrow", Kind::Arrow, "arrow", "projectiles/spectral_arrow", 0.0f},
    {"minecraft:splash_potion", Kind::ThrownItem, {}, {}, 0.0f, 1.0f, false,
     "minecraft:splash_potion"},
    {"minecraft:spruce_boat", Kind::Boat, "boat/spruce", "boat/spruce", 0.8f},
    {"minecraft:spruce_chest_boat", Kind::Boat, "chest_boat/spruce", "chest_boat/spruce", 0.8f},
    {"minecraft:small_fireball", Kind::ThrownItem, {}, {}, 0.0f, 0.75f, true,
     "minecraft:fire_charge"},
    {"minecraft:text_display", Kind::TextDisplay, {}, {}, 0.0f},
    {"minecraft:tnt", Kind::Tnt, {}, {}, 0.5f},
    {"minecraft:tnt_minecart", Kind::Minecart, "tnt_minecart", "minecart", 0.7f},
    {"minecraft:trident", Kind::Trident, "trident", "trident", 0.0f},
    {"minecraft:wind_charge", Kind::WindCharge, "wind_charge", "projectiles/wind_charge", 0.0f,
     1.0f, false, {}, Type::CutoutNoCull},
    {"minecraft:breeze_wind_charge", Kind::WindCharge, "wind_charge",
     "projectiles/breeze_wind_charge", 0.0f, 1.0f, false, {}, Type::CutoutNoCull},
};

// `ItemTransform` of the `GROUND` context for the models that define their
// own, which is the only place vanilla's tree disagrees with its two
// families. Translation is in pixels, as the model files write it.
struct GroundOverride {
    std::string_view item;
    float translation[3];
    float scale;
    float z_rotation;
};
constexpr std::array<GroundOverride, 9> kGroundOverrides = {{
    {"minecraft:conduit", {0.0f, 3.0f, 0.0f}, 0.5f, 0.0f},
    {"minecraft:heavy_core", {0.0f, 3.0f, 0.0f}, 0.5f, 0.0f},
    {"minecraft:decorated_pot", {0.0f, 1.0f, 0.0f}, 0.25f, 0.0f},
    {"minecraft:shield", {2.0f, 4.0f, 2.0f}, 0.25f, 0.0f},
    {"minecraft:trident", {4.0f, 4.0f, 2.0f}, 0.25f, 0.0f},
    {"minecraft:copper_golem_statue", {0.0f, 0.0f, 0.0f}, 0.35f, 180.0f},
    {"minecraft:weathered_copper_golem_statue", {0.0f, 0.0f, 0.0f}, 0.35f, 180.0f},
    {"minecraft:exposed_copper_golem_statue", {0.0f, 0.0f, 0.0f}, 0.35f, 180.0f},
    {"minecraft:oxidized_copper_golem_statue", {0.0f, 0.0f, 0.0f}, 0.35f, 180.0f},
}};

// The suffixes vanilla's `template_*` models cover, each of which puts the
// ground transform somewhere of its own.
bool ends_with(std::string_view name, std::string_view suffix) {
    return name.size() >= suffix.size() && name.substr(name.size() - suffix.size()) == suffix;
}

// `Mth.equal`.
bool mth_equal(float a, float b) { return std::fabs(b - a) < 1.0e-5f; }

Matrix placed(const Placement& place) { return place.matrix; }

// One model piece, posed from the layer's own values and nothing else: every
// renderer here but the boat animates no part.
ObjectPiece model_piece(const ObjectType& row, const ObjectState& state,
                        const ObjectAssets& assets, const Placement& place) {
    ObjectPiece piece;
    piece.what = ObjectPiece::What::Model;
    piece.model = assets.layer != nullptr ? assets.layer(row.layer) : nullptr;
    piece.texture = row.texture;
    piece.render = row.render;
    piece.root = placed(place);
    piece.sky_light = state.sky_light;
    piece.block_light = state.block_light;
    if (piece.model != nullptr) piece.pose.reset(*piece.model);
    return piece;
}

// `ItemStackRenderState.visitExtents` then `AABB.Builder.build`: the item's
// own corners with its display transform applied, which is the box a dropped
// item's lift and its stack spacing come from.
std::array<float, 6> transformed_bounds(const std::array<float, 6>& box,
                                        const gpu::HandRenderer::Display& display) {
    Placement place;
    apply_display(place, display, false);
    std::array<float, 6> out{std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max(),
                             -std::numeric_limits<float>::max(),
                             -std::numeric_limits<float>::max(),
                             -std::numeric_limits<float>::max()};
    for (int corner = 0; corner < 8; ++corner) {
        const float in[3] = {box[(corner & 1) != 0 ? 3 : 0], box[(corner & 2) != 0 ? 4 : 1],
                             box[(corner & 4) != 0 ? 5 : 2]};
        for (int axis = 0; axis < 3; ++axis) {
            const float value = place.matrix[axis] * in[0] + place.matrix[4 + axis] * in[1] +
                                place.matrix[8 + axis] * in[2] + place.matrix[12 + axis];
            out[static_cast<size_t>(axis)] = std::min(out[static_cast<size_t>(axis)], value);
            out[static_cast<size_t>(axis) + 3] =
                std::max(out[static_cast<size_t>(axis) + 3], value);
        }
    }
    return out;
}

// The item one of these entities is drawn as: what the server sent, or the
// renderer's own default.
int32_t item_of(const ObjectType& row, const ObjectState& state, const ObjectAssets& assets) {
    if (state.stack.count > 0) return state.stack.id;
    if (row.default_item.empty() || assets.items == nullptr) return -1;
    return assets.items->find(row.default_item);
}

// `ItemEntityRenderer.submitMultipleFromCount`: the stack's copies, each with
// the item's own space taken to camera-relative space.
void item_cluster(const ObjectState& state, int32_t item,
                  const gpu::HandRenderer::Display& display, const std::array<float, 6>& box,
                  Placement place, std::vector<ObjectPiece>& out) {
    const int count = rendered_amount(state.stack.count > 0 ? state.stack.count : 1);
    if (count == 0) return;
    jmath::LegacyRandom random(static_cast<uint64_t>(item_cluster_seed(state.stack)));
    const auto push = [&](const Placement& at) {
        ObjectPiece piece;
        piece.what = ObjectPiece::What::Item;
        piece.item = item;
        Placement with = at;
        apply_display(with, display, false);
        piece.root = placed(with);
        piece.sky_light = state.sky_light;
        piece.block_light = state.block_light;
        out.push_back(std::move(piece));
    };
    const float depth = box[5] - box[2];
    if (depth > 0.0625f) {
        push(place);
        for (int copy = 1; copy < count; ++copy) {
            Placement one = place;
            const float dx = (random.next_float() * 2.0f - 1.0f) * 0.15f;
            const float dy = (random.next_float() * 2.0f - 1.0f) * 0.15f;
            const float dz = (random.next_float() * 2.0f - 1.0f) * 0.15f;
            one.translate(dx, dy, dz);
            push(one);
        }
        return;
    }
    const float step = depth * 1.5f;
    place.translate(0.0f, 0.0f, -(step * static_cast<float>(count - 1) / 2.0f));
    push(place);
    place.translate(0.0f, 0.0f, step);
    for (int copy = 1; copy < count; ++copy) {
        Placement one = place;
        const float dx = (random.next_float() * 2.0f - 1.0f) * 0.15f * 0.5f;
        const float dy = (random.next_float() * 2.0f - 1.0f) * 0.15f * 0.5f;
        one.translate(dx, dy, 0.0f);
        push(one);
        place.translate(0.0f, 0.0f, step);
    }
}

// `CameraRenderState.orientation`: the camera's own turn, which every
// billboard is mulPose'd by.
std::array<float, 4> camera_orientation(const ObjectState& state) {
    return quaternion_yxz(kPi - to_radians(state.camera_y_rot), -to_radians(state.camera_x_rot),
                          0.0f);
}

// `Display.RenderState.transformation`: the server's left rotation, scale and
// right rotation around its translation, as `Transformation.getMatrix` builds
// it.
void display_transformation(const ObjectState& state, Placement& place) {
    place.translate(state.translation[0], state.translation[1], state.translation[2]);
    place.rotate(state.rotation_left);
    place.scale(state.display_scale[0], state.display_scale[1], state.display_scale[2]);
    place.rotate(state.rotation_right);
}

// `DisplayRenderer.calculateOrientation`: what the billboard is held against.
std::array<float, 4> display_orientation(const ObjectState& state) {
    const float entity_y = -to_radians(state.y_rot);
    const float entity_x = to_radians(state.x_rot);
    const float camera_y = -to_radians(state.camera_y_rot - 180.0f);
    const float camera_x = to_radians(-state.camera_x_rot);
    switch (state.billboard) {
    case Billboard::Fixed: return quaternion_yxz(entity_y, entity_x, 0.0f);
    case Billboard::Horizontal: return quaternion_yxz(entity_y, camera_x, 0.0f);
    case Billboard::Vertical: return quaternion_yxz(camera_y, entity_x, 0.0f);
    case Billboard::Center: break;
    }
    return quaternion_yxz(camera_y, camera_x, 0.0f);
}

// `Mth.clampedLerp`.
float clamped_lerp(float delta, float from, float to) {
    if (delta < 0.0f) return from;
    if (delta > 1.0f) return to;
    return from + delta * (to - from);
}

// `AbstractBoatModel.animatePaddle`: the stroke each paddle is part way
// through, the right one mirrored.
void boat_paddles(const ObjectState& state, const Model& model, Pose& pose) {
    const std::array<std::pair<std::string_view, float>, 2> paddles = {
        {{"left_paddle", state.rowing_left}, {"right_paddle", state.rowing_right}}};
    for (size_t side = 0; side < paddles.size(); ++side) {
        const int32_t part = model.find(paddles[side].first);
        if (part < 0) continue;
        const float time = paddles[side].second;
        pose[part].x_rot = clamped_lerp((jmath::sin(-time) + 1.0f) / 2.0f, -kPi / 3.0f,
                                        -kPi / 12.0f);
        pose[part].y_rot = clamped_lerp((jmath::sin(-time + 1.0f) + 1.0f) / 2.0f, -kPi / 4.0f,
                                        kPi / 4.0f);
        if (side == 1) pose[part].y_rot = kPi - pose[part].y_rot;
    }
}

void build_item(const ObjectType& row, const ObjectState& state, const ObjectAssets& assets,
                Placement place, std::vector<ObjectPiece>& out) {
    const int32_t item = item_of(row, state, assets);
    if (item < 0 || assets.hand == nullptr || assets.items == nullptr) return;
    const auto raw = assets.hand->item_bounds(item);
    if (!raw) return;
    const bool flat = (*raw)[5] - (*raw)[2] <= 0.0626f;
    const gpu::HandRenderer::Display display =
        ground_display(assets.items->name(item), flat);
    const std::array<float, 6> box = transformed_bounds(*raw, display);
    const float lift = -box[1] + 0.0625f;
    const float bob = jmath::sin(state.age_in_ticks / 10.0f + state.bob_offset) * 0.1f + 0.1f;
    place.translate(0.0f, bob + lift, 0.0f);
    place.rotate(quaternion_y(item_spin(state.age_in_ticks, state.bob_offset)));
    item_cluster(state, item, display, box, place, out);
}

void build_thrown_item(const ObjectType& row, const ObjectState& state,
                       const ObjectAssets& assets, Placement place,
                       std::vector<ObjectPiece>& out) {
    const int32_t item = item_of(row, state, assets);
    if (item < 0 || assets.hand == nullptr || assets.items == nullptr) return;
    const auto raw = assets.hand->item_bounds(item);
    if (!raw) return;
    place.scale(row.scale, row.scale, row.scale);
    place.rotate(camera_orientation(state));
    const bool flat = (*raw)[5] - (*raw)[2] <= 0.0626f;
    apply_display(place, ground_display(assets.items->name(item), flat), false);
    ObjectPiece piece;
    piece.what = ObjectPiece::What::Item;
    piece.item = item;
    piece.root = placed(place);
    piece.sky_light = state.sky_light;
    piece.block_light = row.full_bright ? 15 : state.block_light;
    out.push_back(std::move(piece));
}

void build_arrow(const ObjectType& row, const ObjectState& state, const ObjectAssets& assets,
                 Placement place, std::vector<ObjectPiece>& out) {
    place.rotate(quaternion_y(to_radians(state.y_rot - 90.0f)));
    place.rotate(quaternion_z(to_radians(state.x_rot)));
    out.push_back(model_piece(row, state, assets, place));
}

// `ThrownTridentRenderer.submit`.
void build_trident(const ObjectType& row, const ObjectState& state, const ObjectAssets& assets,
                   Placement place, std::vector<ObjectPiece>& out) {
    place.rotate(quaternion_y(to_radians(state.y_rot - 90.0f)));
    place.rotate(quaternion_z(to_radians(state.x_rot + 90.0f)));
    out.push_back(model_piece(row, state, assets, place));
}

// `WindChargeRenderer.submit`.
void build_wind_charge(const ObjectType& row, const ObjectState& state,
                       const ObjectAssets& assets, Placement place,
                       std::vector<ObjectPiece>& out) {
    ObjectPiece piece = model_piece(row, state, assets, place);
    piece.sky_light = 15;
    piece.block_light = 15;
    out.push_back(std::move(piece));
}

void build_boat(const ObjectType& row, const ObjectState& state, const ObjectAssets& assets,
                Placement place, std::vector<ObjectPiece>& out) {
    place.translate(0.0f, 0.375f, 0.0f);
    place.rotate(quaternion_y(to_radians(180.0f - state.y_rot)));
    if (state.hurt_time > 0.0f) {
        place.rotate(quaternion_x(to_radians(jmath::sin(state.hurt_time) * state.hurt_time *
                                             state.damage_time / 10.0f *
                                             static_cast<float>(state.hurt_dir))));
    }
    if (!state.under_water && !mth_equal(state.bubble_angle, 0.0f)) {
        // `Quaternionf.setAngleAxis` about (1, 0, 1), which is not a unit axis
        // and is not normalised there either.
        const float half = to_radians(state.bubble_angle) * 0.5f;
        const float s = std::sin(half);
        place.rotate({s * 1.0f, 0.0f, s * 1.0f, std::cos(half)});
    }
    place.scale(-1.0f, -1.0f, 1.0f);
    place.rotate(quaternion_y(to_radians(90.0f)));
    ObjectPiece piece = model_piece(row, state, assets, place);
    if (piece.model != nullptr) boat_paddles(state, *piece.model, piece.pose);
    out.push_back(std::move(piece));
}

void build_minecart(const ObjectType& row, const ObjectState& state, const ObjectAssets& assets,
                    Placement place, std::vector<ObjectPiece>& out) {
    // `AbstractMinecartRenderer.extractRenderState`: the jitter that keeps two
    // carts on one rail from flickering through each other.
    const int64_t id = static_cast<int64_t>(state.entity_id) * 493286711L;
    const int64_t seed = id * id * 4392167121L + id * 98761L;
    const auto jitter = [&](int shift) {
        return ((static_cast<float>((seed >> shift) & 7L) + 0.5f) / 8.0f - 0.5f) * 0.004f;
    };
    place.translate(jitter(16), jitter(20), jitter(24));
    place.rotate(quaternion_y(to_radians(state.y_rot)));
    place.rotate(quaternion_z(to_radians(-state.x_rot)));
    place.translate(0.0f, 0.375f, 0.0f);
    if (state.hurt_time > 0.0f) {
        place.rotate(quaternion_x(to_radians(jmath::sin(state.hurt_time) * state.hurt_time *
                                             state.damage_time / 10.0f *
                                             static_cast<float>(state.hurt_dir))));
    }
    if (state.has_display_block && assets.block_name != nullptr && assets.items != nullptr &&
        assets.hand != nullptr) {
        const std::string_view block = assets.block_name(state.display_block);
        const int32_t item = block.empty() ? -1 : assets.items->find(block);
        if (item >= 0 && assets.hand->item_bounds(item)) {
            Placement inside = place;
            inside.scale(0.75f, 0.75f, 0.75f);
            inside.translate(-0.5f, static_cast<float>(state.display_offset - 8) / 16.0f, 0.5f);
            inside.rotate(quaternion_y(to_radians(90.0f)));
            ObjectPiece piece;
            piece.what = ObjectPiece::What::Item;
            piece.item = item;
            piece.root = placed(inside);
            piece.sky_light = state.sky_light;
            piece.block_light = state.block_light;
            out.push_back(std::move(piece));
        }
    }
    place.scale(-1.0f, -1.0f, 1.0f);
    out.push_back(model_piece(row, state, assets, place));
}

void build_leash_knot(const ObjectType& row, const ObjectState& state,
                      const ObjectAssets& assets, Placement place,
                      std::vector<ObjectPiece>& out) {
    place.scale(-1.0f, -1.0f, 1.0f);
    out.push_back(model_piece(row, state, assets, place));
}

// `ExperienceOrbRenderer.submit`: one billboarded quad out of a sixteen pixel
// cell of a sixty-four pixel sheet, pulsing in green and blue.
void build_experience_orb(const ObjectType& row, const ObjectState& state, Placement place,
                          std::vector<ObjectPiece>& out) {
    const int icon = state.orb_icon;
    const float u0 = static_cast<float>(icon % 4 * 16 + 0) / 64.0f;
    const float u1 = static_cast<float>(icon % 4 * 16 + 16) / 64.0f;
    const float v0 = static_cast<float>(icon / 4 * 16 + 0) / 64.0f;
    const float v1 = static_cast<float>(icon / 4 * 16 + 16) / 64.0f;
    const float phase = state.age_in_ticks / 2.0f;
    const auto red = static_cast<uint32_t>((jmath::sin(phase + 0.0f) + 1.0f) * 0.5f * 255.0f);
    const auto blue = static_cast<uint32_t>(
        (jmath::sin(phase + static_cast<float>(M_PI * 4.0 / 3.0)) + 1.0f) * 0.1f * 255.0f);
    const uint32_t colour = 0x80000000u | (red << 16) | (0xFFu << 8) | blue;
    place.translate(0.0f, 0.1f, 0.0f);
    place.rotate(camera_orientation(state));
    place.scale(0.3f, 0.3f, 0.3f);
    ObjectPiece piece;
    piece.what = ObjectPiece::What::Quads;
    piece.texture = row.texture;
    piece.root = placed(place);
    piece.render = Type::Translucent;
    piece.sky_light = state.sky_light;
    // `ExperienceOrbRenderer.getBlockLightLevel`: seven brighter than it is.
    piece.block_light = static_cast<uint8_t>(std::clamp(state.block_light + 7, 0, 15));
    piece.corners = {{{{-0.5f, -0.25f, 0.0f}, {u0, v1}, colour},
                      {{0.5f, -0.25f, 0.0f}, {u1, v1}, colour},
                      {{0.5f, 0.75f, 0.0f}, {u1, v0}, colour},
                      {{-0.5f, 0.75f, 0.0f}, {u0, v0}, colour}}};
    out.push_back(std::move(piece));
}

// `FishingHookRenderer.submit`: the bobber, then the sixteen segments of line
// back to the rod's tip.
void build_fishing_bobber(const ObjectType& row, const ObjectState& state, Placement place,
                          std::vector<ObjectPiece>& out) {
    Placement bobber = place;
    bobber.scale(0.5f, 0.5f, 0.5f);
    bobber.rotate(camera_orientation(state));
    ObjectPiece hook;
    hook.what = ObjectPiece::What::Quads;
    hook.texture = row.texture;
    hook.root = placed(bobber);
    hook.sky_light = state.sky_light;
    hook.block_light = state.block_light;
    hook.corners = {{{{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}, 0xFFFFFFFFu},
                     {{0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}, 0xFFFFFFFFu},
                     {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}, 0xFFFFFFFFu},
                     {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}, 0xFFFFFFFFu}}};
    out.push_back(std::move(hook));

    // `FishingHookRenderer.stringVertex`: the line sags along a quadratic in
    // the fraction of the way back to the rod. Drawn here as thin quads, the
    // way the rest of this renderer's geometry is drawn, because this client
    // has no line pass of its own.
    ObjectPiece line;
    line.what = ObjectPiece::What::Quads;
    line.texture = "$white";
    line.root = placed(place);
    line.sky_light = state.sky_light;
    line.block_light = state.block_light;
    const float ox = state.line_origin[0], oy = state.line_origin[1], oz = state.line_origin[2];
    const auto at = [&](float t) {
        return std::array<float, 3>{ox * t, oy * (t * t + t) * 0.5f + 0.25f, oz * t};
    };
    constexpr int kSegments = 16;
    for (int segment = 0; segment < kSegments; ++segment) {
        const float t0 = static_cast<float>(segment) / kSegments;
        const float t1 = static_cast<float>(segment + 1) / kSegments;
        const auto a = at(t0);
        const auto b = at(t1);
        constexpr float kHalf = 0.0075f;
        line.corners.push_back({{a[0] - kHalf, a[1], a[2]}, {0.0f, 0.0f}, 0xFF000000u});
        line.corners.push_back({{b[0] - kHalf, b[1], b[2]}, {0.0f, 0.0f}, 0xFF000000u});
        line.corners.push_back({{b[0] + kHalf, b[1], b[2]}, {0.0f, 0.0f}, 0xFF000000u});
        line.corners.push_back({{a[0] + kHalf, a[1], a[2]}, {0.0f, 0.0f}, 0xFF000000u});
    }
    out.push_back(std::move(line));
}

// A block state drawn as the item of the same name, which is the block's own
// model for every block whose item model is `minecraft:block/<name>`. The
// held-item bake is the only block geometry this renderer can reach.
int32_t block_as_item(const ObjectAssets& assets, int32_t state) {
    if (assets.block_name == nullptr || assets.items == nullptr || assets.hand == nullptr) {
        return -1;
    }
    const std::string_view block = assets.block_name(state);
    if (block.empty()) return -1;
    const int32_t item = assets.items->find(block);
    if (item < 0 || !assets.hand->item_bounds(item)) return -1;
    return item;
}

// `TntRenderer.submit`: the block, swelling in the last ten ticks of fuse and
// flashing white every five.
void build_tnt(const ObjectState& state, const ObjectAssets& assets, Placement place,
               std::vector<ObjectPiece>& out) {
    const int32_t item = block_as_item(assets, state.block_state);
    if (item < 0) return;
    place.translate(0.0f, 0.5f, 0.0f);
    if (state.fuse_remaining < 10.0f) {
        float swell = 1.0f - state.fuse_remaining / 10.0f;
        swell = std::clamp(swell, 0.0f, 1.0f);
        swell *= swell;
        swell *= swell;
        const float scale = 1.0f + swell * 0.3f;
        place.scale(scale, scale, scale);
    }
    place.rotate(quaternion_y(to_radians(-90.0f)));
    place.translate(-0.5f, -0.5f, 0.5f);
    place.rotate(quaternion_y(to_radians(90.0f)));
    ObjectPiece piece;
    piece.what = ObjectPiece::What::Item;
    piece.item = item;
    piece.root = placed(place);
    piece.sky_light = state.sky_light;
    piece.block_light = state.block_light;
    // `TntMinecartRenderer.submitWhiteSolidBlock`: for every other five ticks
    // of fuse the block is drawn white over itself. The overlay pass that
    // does it is the renderer's white-flash column, which the entity renderer
    // has for models and the item queue has not, so only the plain block is
    // drawn here.
    out.push_back(std::move(piece));
}

// `FallingBlockRenderer.submit`.
void build_falling_block(const ObjectState& state, const ObjectAssets& assets, Placement place,
                         std::vector<ObjectPiece>& out) {
    const int32_t item = block_as_item(assets, state.block_state);
    if (item < 0) return;
    place.translate(-0.5f, 0.0f, -0.5f);
    ObjectPiece piece;
    piece.what = ObjectPiece::What::Item;
    piece.item = item;
    piece.root = placed(place);
    piece.sky_light = state.sky_light;
    piece.block_light = state.block_light;
    out.push_back(std::move(piece));
}

void display_light(const ObjectState& state, ObjectPiece& piece) {
    // `DisplayRenderer.getSkyLightLevel`, `.getBlockLightLevel`: a brightness
    // override replaces both.
    if (state.brightness_override == -1) {
        piece.sky_light = state.sky_light;
        piece.block_light = state.block_light;
        return;
    }
    piece.sky_light = static_cast<uint8_t>((state.brightness_override >> 20) & 0xF);
    piece.block_light = static_cast<uint8_t>((state.brightness_override >> 4) & 0xF);
}

void build_block_display(const ObjectState& state, const ObjectAssets& assets, Placement place,
                         std::vector<ObjectPiece>& out) {
    const int32_t item = block_as_item(assets, state.block_state);
    if (item < 0) return;
    place.rotate(display_orientation(state));
    display_transformation(state, place);
    ObjectPiece piece;
    piece.what = ObjectPiece::What::Item;
    piece.item = item;
    piece.root = placed(place);
    display_light(state, piece);
    out.push_back(std::move(piece));
}

void build_item_display(const ObjectState& state, const ObjectAssets& assets, Placement place,
                        std::vector<ObjectPiece>& out) {
    if (state.stack.count <= 0 || assets.hand == nullptr || assets.items == nullptr) return;
    const int32_t item = state.stack.id;
    const auto raw = assets.hand->item_bounds(item);
    if (!raw) return;
    place.rotate(display_orientation(state));
    display_transformation(state, place);
    place.rotate(quaternion_y(kPi));
    const bool flat = (*raw)[5] - (*raw)[2] <= 0.0626f;
    // `ItemDisplayContext`: the context the server names, of which only the
    // two hands and the ground one have a transform this client has baked.
    const bool left_hand = state.item_display_context == 2 || state.item_display_context == 4;
    const bool third_person = state.item_display_context == 1 || state.item_display_context == 2;
    gpu::HandRenderer::Display display =
        state.item_display_context >= 1 && state.item_display_context <= 4
            ? assets.hand->item_display(item, left_hand, third_person)
            : ground_display(assets.items->name(item), flat);
    if (!display.present) display = ground_display(assets.items->name(item), flat);
    apply_display(place, display, left_hand);
    ObjectPiece piece;
    piece.what = ObjectPiece::What::Item;
    piece.item = item;
    piece.root = placed(place);
    display_light(state, piece);
    out.push_back(std::move(piece));
}

// `DisplayRenderer.TextDisplayRenderer.submitInner`: the background behind
// every line, then one line at a time, nine pixels apart, aligned in the
// widest line's box.
void build_text_display(const ObjectState& state, const ObjectAssets&, Placement place,
                        std::vector<ObjectPiece>& out) {
    if (state.lines.empty()) return;
    place.rotate(display_orientation(state));
    display_transformation(state, place);
    place.rotate(quaternion_y(kPi));
    place.scale(-0.025f, -0.025f, -0.025f);
    constexpr int kLineHeight = 9 + 1;
    // Widths are the default font's, which `ObjectState.lines` was already
    // split by; the caller measured them.
    float width = 0.0f;
    for (const std::string& line : state.lines) {
        width = std::max(width, static_cast<float>(line.size()) * 6.0f);
    }
    const auto height =
        static_cast<float>(static_cast<int>(state.lines.size()) * kLineHeight - 1);
    place.translate(1.0f - width / 2.0f, -height, 0.0f);
    const uint32_t background =
        state.default_background ? 0x40000000u : state.background_colour;
    if ((background >> 24) != 0) {
        ObjectPiece panel;
        panel.what = ObjectPiece::What::Quads;
        panel.texture = "$white";
        panel.root = placed(place);
        panel.render = Type::Translucent;
        display_light(state, panel);
        panel.corners = {{{{-1.0f, -1.0f, 0.0f}, {0.5f, 0.5f}, background},
                          {{-1.0f, height, 0.0f}, {0.5f, 0.5f}, background},
                          {{width, height, 0.0f}, {0.5f, 0.5f}, background},
                          {{width, -1.0f, 0.0f}, {0.5f, 0.5f}, background}}};
        out.push_back(std::move(panel));
    }
    float y = 0.0f;
    for (const std::string& line : state.lines) {
        const auto line_width = static_cast<float>(line.size()) * 6.0f;
        const float x = state.align == TextAlign::Left    ? 0.0f
                        : state.align == TextAlign::Right ? width - line_width
                                                          : width / 2.0f - line_width / 2.0f;
        Placement at = place;
        at.translate(x, y, 0.0f);
        ObjectPiece piece;
        piece.what = ObjectPiece::What::Text;
        piece.text = line;
        piece.text_colour =
            (static_cast<uint32_t>(state.text_opacity) << 24) | 0x00FFFFFFu;
        piece.see_through = state.text_see_through;
        piece.root = placed(at);
        display_light(state, piece);
        out.push_back(std::move(piece));
        y += static_cast<float>(kLineHeight);
    }
}

} // namespace

int rendered_amount(int count) {
    if (count <= 1) return 1;
    if (count <= 16) return 2;
    if (count <= 32) return 3;
    return count <= 48 ? 4 : 5;
}

int32_t item_cluster_seed(const ItemStack& stack) {
    if (stack.count <= 0) return 187;
    int32_t damage = 0;
    if (const proto::Value* worn = stack.components.find("minecraft:damage", 774)) {
        damage = static_cast<int32_t>(worn->as_int().value_or(0));
    }
    return stack.id + damage;
}

float item_spin(float age, float bob_offset) { return age / 20.0f + bob_offset; }

gpu::HandRenderer::Display ground_display(std::string_view item_name, bool flat) {
    gpu::HandRenderer::Display out;
    out.present = true;
    for (const GroundOverride& one : kGroundOverrides) {
        if (one.item != item_name) continue;
        for (int axis = 0; axis < 3; ++axis) out.translation[axis] = one.translation[axis] / 16.0f;
        out.scale[0] = out.scale[1] = out.scale[2] = one.scale;
        out.rotation[2] = one.z_rotation;
        return out;
    }
    // `item/template_bed`, `item/template_banner`: the two templates that put
    // it somewhere of their own and cover a whole family of items.
    if (ends_with(item_name, "_bed")) {
        out.translation[1] = 1.0f / 16.0f;
        out.translation[2] = 2.0f / 16.0f;
        out.scale[0] = out.scale[1] = out.scale[2] = 0.25f;
        return out;
    }
    if (ends_with(item_name, "_banner")) {
        out.translation[1] = 1.0f / 16.0f;
        out.scale[0] = out.scale[1] = out.scale[2] = 0.25f;
        return out;
    }
    if (ends_with(item_name, "_head") || ends_with(item_name, "_skull")) {
        out.translation[1] = 3.0f / 16.0f;
        out.scale[0] = out.scale[1] = out.scale[2] = 0.5f;
        return out;
    }
    // `item/generated` and `block/block`, which between them are the ground
    // transform of every other item in the game.
    out.translation[1] = (flat ? 2.0f : 3.0f) / 16.0f;
    out.scale[0] = out.scale[1] = out.scale[2] = flat ? 0.5f : 0.25f;
    return out;
}

std::span<const ObjectType> object_types() { return kTypes; }

const ObjectType* object_type(std::string_view type) {
    if (type.empty()) return nullptr;
    for (const ObjectType& row : kTypes) {
        if (row.type == type) return &row;
    }
    return nullptr;
}

void build_object(const ObjectType& row, const ObjectState& state, const ObjectAssets& assets,
                  std::vector<ObjectPiece>& out) {
    if (state.invisible) return;
    Placement place;
    place.matrix = translation(state.x, state.y, state.z);
    place.shape = Placement::kAffine | Placement::kTranslation | Placement::kOrthonormal;
    switch (row.kind) {
    case Kind::Item: build_item(row, state, assets, place, out); break;
    case Kind::ThrownItem: build_thrown_item(row, state, assets, place, out); break;
    case Kind::Arrow: build_arrow(row, state, assets, place, out); break;
    case Kind::Trident: build_trident(row, state, assets, place, out); break;
    case Kind::WindCharge: build_wind_charge(row, state, assets, place, out); break;
    case Kind::Boat: build_boat(row, state, assets, place, out); break;
    case Kind::Minecart: build_minecart(row, state, assets, place, out); break;
    case Kind::LeashKnot: build_leash_knot(row, state, assets, place, out); break;
    case Kind::ExperienceOrb: build_experience_orb(row, state, place, out); break;
    case Kind::FishingBobber: build_fishing_bobber(row, state, place, out); break;
    case Kind::Tnt: build_tnt(state, assets, place, out); break;
    case Kind::FallingBlock: build_falling_block(state, assets, place, out); break;
    case Kind::BlockDisplay: build_block_display(state, assets, place, out); break;
    case Kind::ItemDisplay: build_item_display(state, assets, place, out); break;
    case Kind::TextDisplay: build_text_display(state, assets, place, out); break;
    case Kind::None: break;
    }
}

} // namespace mcpp::entity
