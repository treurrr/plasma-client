#include "mcpp/entity/scene.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>

namespace mcpp::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadians = kPi / 180.0f;

constexpr uint8_t kSwingMainHand = 0;
constexpr uint8_t kSwingOffHand = 3;
constexpr uint8_t kDeathStatus = 3;
constexpr int32_t kSpectator = 3;

// The player's box: standing, and crouched.
constexpr float kPlayerHeight = 1.8f;
constexpr float kCrouchingHeight = 1.5f;

} // namespace

std::filesystem::path default_registries_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("registries-{}.txt", protocol);
}

std::vector<std::string> load_entity_types(const std::filesystem::path& registries) {
    std::vector<std::string> out;
    std::ifstream input(registries);
    std::string line;
    constexpr std::string_view kPrefix = "minecraft:entity_type\t";
    while (std::getline(input, line)) {
        if (line.starts_with(kPrefix)) out.push_back(line.substr(kPrefix.size()));
    }
    return out;
}

Scene::Scene(std::vector<std::string> types, skins::Cache* skins)
    : types_(std::move(types)), skins_(skins) {
    for (size_t i = 0; i < types_.size(); ++i) {
        if (types_[i] == "minecraft:player" || types_[i] == "player") {
            player_type_ = static_cast<int32_t>(i);
        }
    }
    tracked_.reserve(1024);
    matrices_.resize(256);
}

void Scene::bind(const gpu::EntityRenderer& renderer) {
    wide_model_ = renderer.model("player");
    slim_model_ = renderer.model("player_slim");
    if (const Model* m = renderer.model_data(wide_model_)) wide_parts_ = HumanoidParts::of(*m);
    if (const Model* m = renderer.model_data(slim_model_)) slim_parts_ = HumanoidParts::of(*m);
}

std::string_view Scene::type_name(int32_t type) const {
    if (type < 0 || static_cast<size_t>(type) >= types_.size()) return "";
    return types_[static_cast<size_t>(type)];
}

Scene::Tracked* Scene::find(int32_t id) {
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &tracked_[found->second];
}

const Living* Scene::living(int32_t id) const {
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &tracked_[found->second].living;
}

void Scene::add(const Entity& entity) {
    remove(entity.id);
    Tracked tracked;
    tracked.id = entity.id;
    tracked.type = entity.type;
    tracked.kind = entity.type == player_type_ ? Kind::Player : Kind::Other;
    tracked.uuid = uuid_string(entity.uuid);
    tracked.living.spawn(entity);
    index_[entity.id] = tracked_.size();
    tracked_.push_back(std::move(tracked));
}

void Scene::moved(const EntityMove& move) {
    Tracked* tracked = find(move.id);
    if (tracked == nullptr) return;
    Living& l = tracked->living;
    // `ClientPacketListener.handleEntityPositionSync`: a far jump snaps, the rest eases.
    if (move.absolute && move.has_position) {
        const double dx = l.x - move.x;
        const double dy = l.y - move.y;
        const double dz = l.z - move.z;
        if (dx * dx + dy * dy + dz * dz > 4096.0) {
            l.snap_to(move.x, move.y, move.z, move.yaw, move.pitch);
            l.steps = 0;
            l.has_previous = false;
            return;
        }
    }
    const double position[3] = {move.x, move.y, move.z};
    l.move_to(move.has_position ? position : nullptr, move.has_rotation ? &move.yaw : nullptr,
              move.has_rotation ? &move.pitch : nullptr);
}

// `ClientPacketListener.handleRotateMob`: the head eases over three ticks.
void Scene::head(int32_t id, float yaw) {
    if (Tracked* tracked = find(id)) tracked->living.lerp_head_to(yaw, 3);
}

void Scene::remove(int32_t id) {
    const auto found = index_.find(id);
    if (found == index_.end()) return;
    const size_t at = found->second;
    index_.erase(found);
    if (at + 1 != tracked_.size()) {
        tracked_[at] = std::move(tracked_.back());
        index_[tracked_[at].id] = at;
    }
    tracked_.pop_back();
}

void Scene::clear() {
    tracked_.clear();
    index_.clear();
}

// `ClientPacketListener.handleAnimate`: main or off hand swing.
void Scene::animate(int32_t id, uint8_t action) {
    Tracked* tracked = find(id);
    if (tracked == nullptr) return;
    if (action == kSwingMainHand) tracked->living.swing(false);
    if (action == kSwingOffHand) tracked->living.swing(true);
}

// `ClientPacketListener.handleHurtAnimation`: the hurt flash.
void Scene::hurt(int32_t id) {
    if (Tracked* tracked = find(id)) tracked->living.hurt();
}

// `ClientPacketListener.handleDamageEvent`: the damage reaction.
void Scene::damaged(int32_t id) {
    if (Tracked* tracked = find(id)) tracked->living.damaged();
}

void Scene::event(int32_t id, uint8_t status) {
    Tracked* tracked = find(id);
    if (tracked == nullptr) return;
    if (status == kDeathStatus && tracked->kind != Kind::Player) tracked->living.dead = true;
}

void Scene::set_crouching(int32_t id, bool crouching) {
    if (Tracked* tracked = find(id)) tracked->crouching = crouching;
}

void Scene::player_info(const PlayerInfo& info) {
    PlayerLine& line = players_[info.uuid];
    line.name = info.name;
    line.spectator = info.game_mode == kSpectator;
    const auto fallback = skins::default_skin(info.uuid);
    line.default_texture = fallback.texture;
    line.slim = fallback.slim;
    if (const auto* textures = info.property("textures")) {
        if (const auto decoded = skins::decode_property(textures->value)) {
            if (decoded->skin_url != line.skin.url) {
                line.skin = {};
                line.skin.url = decoded->skin_url;
            }
            line.skin.slim = decoded->slim;
        }
    }
}

void Scene::player_gone(std::string_view uuid) {
    const auto found = players_.find(uuid);
    if (found == players_.end()) return;
    players_.erase(found);
}

int32_t Scene::skin_layer(gpu::EntityRenderer& renderer, Skin& skin) {
    if (skin.layer >= 0 || skin.failed || skin.url.empty() || skins_ == nullptr) return skin.layer;
    if (const auto shared = layers_.find(skin.url); shared != layers_.end()) {
        skin.layer = shared->second.first;
        ++shared->second.second;
        return skin.layer;
    }
    if (const Texture* texture = skins_->get(skin.url)) {
        skin.layer = renderer.upload_skin(*texture);
        if (skin.layer >= 0) layers_[skin.url] = {skin.layer, 1};
        else skin.failed = true;
    } else if (skins_->failed(skin.url)) {
        skin.failed = true;
    }
    return skin.layer;
}

void Scene::tick() {
    for (Tracked& tracked : tracked_) {
        if (tracked.kind == Kind::Player) tracked.living.tick_remote_player();
        else tracked.living.tick_mob();
    }
}

void Scene::draw(gpu::EntityRenderer& renderer, const View& view, const LightAt& light) {
    const auto started = std::chrono::steady_clock::now();
    stats_ = {};
    stats_.tracked = tracked_.size();
    for (Tracked& tracked : tracked_) {
        if (tracked.kind == Kind::Player) draw_player(renderer, tracked, view, light);
    }
    stats_.pose_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              started)
                         .count();
}

void Scene::draw_player(gpu::EntityRenderer& renderer, Tracked& tracked, const View& view,
                        const LightAt& light) {
    const float p = view.partial;
    const Living& l = tracked.living;
    const auto line = players_.find(tracked.uuid);
    if (line == players_.end()) return;
    PlayerLine& info = line->second;

    const int32_t layer = skin_layer(renderer, info.skin);
    const bool slim = layer >= 0 ? info.skin.slim : info.slim;
    const int32_t model_index = slim ? slim_model_ : wide_model_;
    const Model* model = renderer.model_data(model_index);
    const HumanoidParts& parts = slim ? slim_parts_ : wide_parts_;
    if (model == nullptr || !parts.complete()) return;
    uint32_t texture = 0;
    if (layer >= 0) {
        texture = gpu::EntityRenderer::kSkinBit | static_cast<uint32_t>(layer);
    } else {
        const int32_t baked = renderer.texture(info.default_texture);
        if (baked < 0) return;
        texture = static_cast<uint32_t>(baked);
    }

    HumanoidState state;
    const float head = l.head_at(p);
    const float body = l.body_at(p);
    state.y_rot = wrap_degrees(head - body);
    state.x_rot = l.x_rot_at(p);
    if (!l.passenger && !l.dead) {
        state.walk_position = l.walk.position_at(p);
        state.walk_speed = l.walk.speed_at(p);
    }
    state.age_in_ticks = static_cast<float>(l.tick_count) + p;
    state.attack_time = l.attack_at(p);
    state.attack_left = l.swinging_off_hand;
    state.crouching = tracked.crouching;
    state.passenger = l.passenger;
    state.spectator = info.spectator;
    if (tracked.pose.parts.size() != model->parts.size()) tracked.pose.reset(*model);
    animate_player(*model, parts, state, tracked.pose);

    const double x = l.x_at(p);
    const double y = l.y_at(p) + (tracked.crouching ? -2.0 / 16.0 : 0.0);
    const double z = l.z_at(p);
    Matrix root = translation(static_cast<float>(x - view.eye[0]),
                              static_cast<float>(y - view.eye[1]),
                              static_cast<float>(z - view.eye[2]));
    // `LivingEntityRenderer.setupRotations`: face the body's yaw, tip over in death.
    root = multiply(root, rotation_y((180.0f - body) * kRadians));
    const float death = l.death_time > 0 ? static_cast<float>(l.death_time) + p : 0.0f;
    if (death > 0.0f) {
        const float fall = std::min(1.0f, std::sqrt((death - 1.0f) / 20.0f * 1.6f));
        root = multiply(root, rotation_z(fall * 90.0f * kRadians));
    }
    // `LivingEntityRenderer.submit`, `AvatarRenderer.scale`: flip, scale, lift.
    root = multiply(root, scaling(-1.0f, -1.0f, 1.0f));
    root = multiply(root, scaling(0.9375f, 0.9375f, 0.9375f));
    root = multiply(root, translation(0.0f, -1.501f, 0.0f));

    if (matrices_.size() < model->parts.size()) matrices_.resize(model->parts.size());
    std::span<Matrix> out(matrices_.data(), model->parts.size());
    pose_matrices(*model, tracked.pose, root, out);

    const auto lit = light(static_cast<int32_t>(std::floor(x)),
                           static_cast<int32_t>(std::floor(l.y_at(p) + (tracked.crouching ? 1.27 : 1.62))),
                           static_cast<int32_t>(std::floor(z)));
    gpu::EntityRenderer::Instance instance;
    instance.model = model_index;
    instance.type = gpu::EntityRenderer::Type::Translucent;
    instance.texture = texture;
    instance.sky_light = lit[0];
    instance.block_light = lit[1];
    instance.red = l.hurt_time > 0 || l.death_time > 0;
    if (renderer.add(instance, out)) {
        ++stats_.drawn;
        stats_.parts += out.size();
    }

    if (view.name_tags && !info.name.empty()) {
        name_tag(renderer, tracked, view, info.name, x, y, z, tracked.crouching, lit);
    }
}

void Scene::name_tag(gpu::EntityRenderer& renderer, const Tracked& tracked, const View& view,
                     std::string_view text, double x, double y, double z, bool discrete,
                     std::array<uint8_t, 2> light) {
    const double height = tracked.crouching ? kCrouchingHeight : kPlayerHeight;
    const double dx = x - view.eye[0];
    const double dy = y - view.eye[1];
    const double dz = z - view.eye[2];
    const double distance = dx * dx + dy * dy + dz * dz;
    if (distance >= 4096.0) return;
    if (discrete && distance >= 1024.0) return;

    float forward[3] = {view.forward[0], view.forward[1], view.forward[2]};
    const float length =
        std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (length <= 0.0f) return;
    for (float& f : forward) f /= length;
    float right[3] = {-forward[2], 0.0f, forward[0]};
    float right_length = std::sqrt(right[0] * right[0] + right[2] * right[2]);
    if (right_length <= 1e-6f) {
        right[0] = 1.0f;
        right_length = 1.0f;
    }
    right[0] /= right_length;
    right[2] /= right_length;
    const float up[3] = {right[1] * forward[2] - right[2] * forward[1],
                         right[2] * forward[0] - right[0] * forward[2],
                         right[0] * forward[1] - right[1] * forward[0]};
    Matrix orientation = identity();
    for (int axis = 0; axis < 3; ++axis) {
        orientation[axis] = right[axis];
        orientation[4 + axis] = up[axis];
        orientation[8 + axis] = -forward[axis];
    }
    Matrix place = translation(static_cast<float>(dx), static_cast<float>(dy + height + 0.5),
                               static_cast<float>(dz));
    place = multiply(place, orientation);
    place = multiply(place, scaling(0.025f, -0.025f, 0.025f));

    // The default text background opacity, a quarter, as the game rounds it.
    constexpr uint32_t kBackground = static_cast<uint32_t>(0.25f * 255.0f) << 24;
    constexpr uint32_t kDim = 0x80FFFFFFu;
    if (!discrete) {
        renderer.add_tag(text, place, true, kDim, kBackground, light[0], light[1]);
        renderer.add_tag(text, place, false, 0xFFFFFFFFu, 0, light[0],
                         std::max<uint8_t>(light[1], 2));
    } else {
        renderer.add_tag(text, place, false, kDim, kBackground, light[0], light[1]);
    }
}

} // namespace mcpp::entity
