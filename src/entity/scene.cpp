#include "mcpp/entity/scene.hpp"

#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>

namespace mcpp::entity {
namespace {

// The game shows a name tag out to this far, in blocks.
constexpr double kTagRange = 64.0;
// How red a hurt entity flashes.
constexpr float kHurtRed = 0.3f;

constexpr uint8_t kDeathStatus = 3;
constexpr uint8_t kSwingMainHand = 0;

void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

void normalise(float v[3]) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length <= 0.0f) return;
    for (int i = 0; i < 3; ++i) v[i] /= length;
}

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

Scene::Scene(const Assets& assets, std::vector<std::string> types, skins::Cache* skins)
    : assets_(assets), types_(std::move(types)), skins_(skins) {}

std::string_view Scene::type_name(int32_t type) const {
    if (type < 0 || static_cast<size_t>(type) >= types_.size()) return "";
    return types_[static_cast<size_t>(type)];
}

void Scene::add(const Entity& entity) {
    Tracked& tracked = tracked_[entity.id];
    tracked.entity = entity;
    tracked.motion = Motion{};
    tracked.motion.spawn(entity);
}

void Scene::move(const Entity& entity) {
    const auto found = tracked_.find(entity.id);
    if (found == tracked_.end()) return;
    found->second.entity = entity;
    found->second.motion.receive(entity);
}

void Scene::remove(int32_t id) { tracked_.erase(id); }

void Scene::animate(int32_t id, uint8_t action) {
    if (action != kSwingMainHand) return;
    if (const auto found = tracked_.find(id); found != tracked_.end()) found->second.motion.swing();
}

void Scene::hurt(int32_t id) {
    if (const auto found = tracked_.find(id); found != tracked_.end()) found->second.motion.hurt();
}

void Scene::event(int32_t id, uint8_t status) {
    if (status != kDeathStatus) return;
    if (const auto found = tracked_.find(id); found != tracked_.end()) {
        found->second.motion.death_ticks = std::max(found->second.motion.death_ticks, 1);
    }
}

void Scene::tick() {
    for (auto& [id, tracked] : tracked_) {
        tracked.motion.tick();
        if (tracked.motion.death_ticks > 0 && tracked.motion.death_ticks < Motion::kDeathTicks) {
            ++tracked.motion.death_ticks;
        }
    }
}

const Motion* Scene::motion(int32_t id) const {
    const auto found = tracked_.find(id);
    return found == tracked_.end() ? nullptr : &found->second.motion;
}

void Scene::draw(gpu::EntityRenderer& renderer, const Session& session, const View& view,
                 const LightAt& light) {
    drawn_ = 0;
    missing_.clear();

    float forward[3] = {view.forward[0], view.forward[1], view.forward[2]};
    normalise(forward);
    const float world_up[3] = {0.0f, 1.0f, 0.0f};
    float right[3];
    cross(forward, world_up, right);
    normalise(right);
    float up[3];
    cross(right, forward, up);

    for (const auto& [id, tracked] : tracked_) {
        if (id == view.self) continue;
        const std::string_view type = type_name(tracked.entity.type);
        const Model* model = nullptr;
        std::string texture;
        std::string name;
        if (type == "minecraft:player") {
            const std::string uuid = uuid_string(tracked.entity.uuid);
            const PlayerInfo* info = session.player(uuid);
            const auto fallback = skins::default_skin(uuid);
            bool slim = fallback.slim;
            texture = fallback.texture;
            if (info != nullptr) {
                name = info->name;
                if (const auto* property = info->property("textures"); property && skins_) {
                    if (const auto textures = skins::decode_property(property->value);
                        textures && !textures->skin_url.empty()) {
                        if (const auto* skin = skins_->get(textures->skin_url)) {
                            const std::string key = "skin:" + textures->skin_url;
                            if (!renderer.has_texture(key)) renderer.upload_texture(key, *skin);
                            texture = key;
                            slim = textures->slim;
                        }
                    }
                }
            }
            model = assets_.models().find(slim ? "player_slim" : "player");
        } else {
            const std::string_view bare =
                type.starts_with("minecraft:") ? type.substr(10) : type;
            model = assets_.models().find(bare);
            if (model != nullptr) texture = model->texture;
        }
        if (model == nullptr) {
            ++missing_[std::string(type)];
            continue;
        }
        if (!renderer.has_texture(texture)) {
            const Texture* pixels = assets_.texture(texture);
            if (pixels == nullptr) {
                ++missing_[std::string(type)];
                continue;
            }
            renderer.upload_texture(texture, *pixels);
        }

        const Motion& motion = tracked.motion;
        const float partial = view.partial;
        const double at[3] = {motion.x(partial), motion.y(partial), motion.z(partial)};
        const double eye_height = model->height * 0.85;
        const auto lit = light ? light(static_cast<int32_t>(std::floor(at[0])),
                                       static_cast<int32_t>(std::floor(at[1] + eye_height)),
                                       static_cast<int32_t>(std::floor(at[2])))
                               : std::array<uint8_t, 2>{15, 0};

        Pose pose(*model);
        pose_humanoid(*model, motion, partial, pose);
        scratch_.clear();
        build_mesh(*model, pose, model_transform(*model, motion, partial, view.eye), scratch_);

        gpu::EntityRenderer::Draw draw;
        draw.texture = texture;
        draw.layer = type == "minecraft:player" ? gpu::EntityRenderer::Layer::CutoutNoCull
                                                : gpu::EntityRenderer::Layer::Cutout;
        draw.sky_light = lit[0];
        draw.block_light = lit[1];
        if (motion.hurt_ticks > 0 || motion.death_ticks > 0) {
            draw.overlay = {1.0f, 0.0f, 0.0f, kHurtRed};
        }
        renderer.add(scratch_, draw);
        ++drawn_;

        if (!view.name_tags || name.empty() || motion.death_ticks > 0) continue;
        const double dx = at[0] - view.eye[0];
        const double dy = at[1] - view.eye[1];
        const double dz = at[2] - view.eye[2];
        if (dx * dx + dy * dy + dz * dz > kTagRange * kTagRange) continue;
        constexpr float kPixel = 0.025f;
        Matrix place = identity();
        for (int i = 0; i < 3; ++i) {
            place[0 + i] = right[i] * kPixel;
            place[4 + i] = -up[i] * kPixel;
            place[8 + i] = -forward[i] * kPixel;
        }
        place[12] = static_cast<float>(dx);
        place[13] = static_cast<float>(dy + model->height + 0.5);
        place[14] = static_cast<float>(dz);
        renderer.add_tag(name, place, true, lit[0], lit[1]);
    }
}

} // namespace mcpp::entity
