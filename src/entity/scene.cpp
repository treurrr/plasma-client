#include "mcpp/entity/scene.hpp"

#include "mcpp/entity/dye.hpp"
#include "mcpp/entity/mob_tick.hpp"

#include <algorithm>
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

// `EquipmentSlot`, in the order `HumanoidArmorLayer.submit` draws the four
// humanoid pieces, and the model layer each one wears.
struct ArmourPiece {
    Equipment::Slot slot;
    std::string_view suffix;
    LayerType type;
};
constexpr std::array<ArmourPiece, 4> kArmour = {{
    {Equipment::Chest, "#chestplate", LayerType::Humanoid},
    {Equipment::Legs, "#leggings", LayerType::HumanoidLeggings},
    {Equipment::Feet, "#boots", LayerType::Humanoid},
    {Equipment::Head, "#helmet", LayerType::Humanoid},
}};

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
    type_info_.resize(types_.size());
}

void Scene::bind(const gpu::EntityRenderer& renderer) { bind(renderer, nullptr); }

void Scene::bind(const gpu::EntityRenderer& renderer, const EquipmentAssets* equipment) {
    renderer_ = &renderer;
    equipment_ = equipment;
    models_.clear();
    for (TypeInfo& info : type_info_) info = {};
    wide_model_ = renderer.model("player");
    slim_model_ = renderer.model("player_slim");
    wide_ = dynamic_cast<const Avatar*>(animator("player"));
    slim_ = dynamic_cast<const Avatar*>(animator("player_slim"));
    for (int slim = 0; slim < 2; ++slim) {
        const std::string_view base = slim == 1 ? "player_slim" : "player";
        for (size_t piece = 0; piece < kArmour.size(); ++piece) {
            player_armour_[static_cast<size_t>(slim)][piece] =
                layer_for(std::string(base) + std::string(kArmour[piece].suffix), {},
                          Overlay::Plain, gpu::EntityRenderer::Type::CutoutNoCull);
        }
    }
}

// One model object per layer: the game builds each `EntityModel` once and
// animates every entity of that layer with it.
const MobModel* Scene::animator(std::string_view layer) {
    if (renderer_ == nullptr) return nullptr;
    if (const auto found = models_.find(layer); found != models_.end()) {
        return found->second.get();
    }
    const int32_t model = renderer_->model(layer);
    const Model* data = renderer_->model_data(model);
    if (data == nullptr) return nullptr;
    auto built = make_mob_model(layer, *data);
    const MobModel* raw = built.get();
    models_.emplace(std::string(layer), std::move(built));
    return raw;
}

Scene::Layer Scene::layer_for(std::string_view layer_name, std::string_view texture, Overlay kind,
                              gpu::EntityRenderer::Type render) {
    Layer out;
    if (renderer_ == nullptr || layer_name.empty()) return out;
    out.model = renderer_->model(layer_name);
    out.animator = animator(layer_name);
    out.texture = texture.empty() ? -1 : renderer_->texture(texture);
    out.kind = kind;
    out.render = render;
    return out;
}

Scene::TypeInfo& Scene::type_info(int32_t type) {
    if (type < 0 || static_cast<size_t>(type) >= type_info_.size()) {
        static TypeInfo none;
        return none;
    }
    TypeInfo& info = type_info_[static_cast<size_t>(type)];
    if (info.ready) return info;
    info.ready = true;
    const MobType* row = mob_type(type_name(type));
    info.row = row;
    if (row == nullptr || renderer_ == nullptr) return info;

    const auto baby_or = [&](std::string_view baby, std::string_view adult) {
        return baby.empty() ? adult : baby;
    };
    info.base[0] = layer_for(row->layer, row->texture, Overlay::Plain, row->render);
    info.base[1] =
        layer_for(baby_or(row->baby_layer, row->layer), row->texture, Overlay::Plain, row->render);
    for (int age = 0; age < 2; ++age) {
        for (const MobOverlay& over : row->overlays) {
            // An empty layer name means the overlay is drawn on the base model.
            std::string_view name = over.layer.empty() ? row->layer : over.layer;
            if (age == 1) {
                if (!over.baby_layer.empty()) name = over.baby_layer;
                else if (over.layer.empty()) name = baby_or(row->baby_layer, row->layer);
            }
            info.overlays[static_cast<size_t>(age)].push_back(
                layer_for(name, over.texture, over.kind, over.render));
        }
    }
    if (row->humanoid_armour) {
        info.has_armour = true;
        for (int age = 0; age < 2; ++age) {
            const std::string_view body =
                age == 1 ? baby_or(row->baby_layer, row->layer) : row->layer;
            for (size_t piece = 0; piece < kArmour.size(); ++piece) {
                info.armour[static_cast<size_t>(age)][piece] =
                    layer_for(std::string(body) + std::string(kArmour[piece].suffix), {},
                              Overlay::Plain, gpu::EntityRenderer::Type::CutoutNoCull);
            }
        }
    }
    return info;
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

const MobState* Scene::state(int32_t id) const {
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &tracked_[found->second].state;
}

void Scene::add(const Entity& entity) {
    remove(entity.id);
    Tracked tracked;
    tracked.id = entity.id;
    tracked.type = entity.type;
    tracked.kind = entity.type == player_type_ ? Kind::Player : Kind::Other;
    tracked.uuid = uuid_string(entity.uuid);
    tracked.living.spawn(entity);
    tracked.state.entity_id = entity.id;
    index_[entity.id] = tracked_.size();
    tracked_.push_back(std::move(tracked));
    if (!entity.metadata.empty()) metadata(entity.id, entity.metadata);
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
    l.on_ground = move.on_ground;
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
    mob_entity_event(type_name(tracked->type), status, tracked->own);
}

void Scene::equipment(int32_t id, const Equipment& worn) {
    if (Tracked* tracked = find(id)) tracked->worn = worn;
}

void Scene::riding(int32_t id, bool passenger) {
    if (Tracked* tracked = find(id)) tracked->living.passenger = passenger;
}

// What `set_entity_data` says, read into the render state: the fields every
// entity shares, then the ones this type's own table names.
void Scene::metadata(int32_t id, const proto::Metadata& data) {
    Tracked* tracked = find(id);
    if (tracked == nullptr) return;
    MobState& s = tracked->state;
    const std::string_view type = type_name(tracked->type);

    const uint8_t flags = data.flags();
    s.crouching = (flags & proto::Metadata::Crouching) != 0;
    s.fall_flying = (flags & proto::Metadata::FallFlying) != 0;
    s.invisible = (flags & proto::Metadata::Invisible) != 0;

    // `Entity.getPose`, numbered the same on both sides.
    s.pose = static_cast<EntityPose>(data.int_at(proto::Metadata::kPose, 0));
    // `LivingEntity.isVisuallySwimming`: the swimming pose, however it was reached.
    s.visually_swimming = s.pose == EntityPose::Swimming;
    if (s.pose == EntityPose::Crouching) s.crouching = true;

    // `LivingEntity.isUsingItem`: the hand-states byte.
    const auto hand_states = static_cast<uint8_t>(data.int_at(proto::Metadata::kLivingFlags, 0));
    s.using_item = (hand_states & 0x01) != 0;
    s.use_item_off_hand = (hand_states & 0x02) != 0;
    s.auto_spin_attack = (hand_states & 0x04) != 0;

    // `LivingEntity.isFullyFrozen`: a hundred and forty ticks in powder snow.
    const int index_frozen = proto::metadata_index(type, "ticks_frozen_powder_snow");
    if (index_frozen >= 0) {
        s.fully_frozen =
            data.int_at(static_cast<uint8_t>(index_frozen), 0) >= 140;
    }

    // `Player.getMainArm` and the skin parts the player chose to show.
    if (tracked->kind == Kind::Player) {
        if (const int arm = proto::metadata_index(type, "main_hand"); arm >= 0) {
            s.main_arm_right = data.int_at(static_cast<uint8_t>(arm), 1) != 0;
        }
        if (const int parts = proto::metadata_index(type, "displayed_skin_parts_bit_mask");
            parts >= 0) {
            // `PlayerModelPart`: cape, jacket, left and right sleeve, left and
            // right trouser leg, hat.
            const auto shown =
                static_cast<uint8_t>(data.int_at(static_cast<uint8_t>(parts), 0x7F));
            s.show_jacket = (shown & 0x02) != 0;
            s.show_left_sleeve = (shown & 0x04) != 0;
            s.show_right_sleeve = (shown & 0x08) != 0;
            s.show_left_pants = (shown & 0x10) != 0;
            s.show_right_pants = (shown & 0x20) != 0;
            s.show_hat = (shown & 0x40) != 0;
        }
    }

    // `LivingEntity.isBaby`, where the type has one.
    if (const int baby = proto::metadata_index(type, "is_baby"); baby >= 0) {
        s.baby = data.bool_at(static_cast<uint8_t>(baby), false);
        tracked->living.baby = s.baby;
    }
    // `Mob.getAgeScale`: a baby is half size, and its shadow with it.
    s.age_scale = s.baby ? 0.5f : 1.0f;

    tracked->data = data;
    tick_mob_counters(type, data, tracked->living.on_ground, tracked->living.walk.speed,
                      tracked->own);
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
    ++ticks_;
    for (Tracked& tracked : tracked_) {
        if (tracked.kind == Kind::Player) tracked.living.tick_remote_player();
        else tracked.living.tick_mob();
        tracked.own.tick_count = static_cast<int64_t>(ticks_);
        tick_mob_counters(type_name(tracked.type), tracked.data, tracked.living.on_ground,
                          tracked.living.walk.speed, tracked.own);
    }
}

void Scene::draw(gpu::EntityRenderer& renderer, const View& view, const LightAt& light) {
    const auto started = std::chrono::steady_clock::now();
    stats_ = {};
    stats_.tracked = tracked_.size();
    for (Tracked& tracked : tracked_) {
        if (tracked.kind == Kind::Player) draw_player(renderer, tracked, view, light);
        else draw_one(renderer, tracked, view, light);
    }
    stats_.pose_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              started)
                         .count();
}

// `LivingEntityRenderer.extractRenderState` for the fields the shared easing
// owns: where the entity is, which way it faces and how far into a swing,
// a hurt or a death it is.
void Scene::fill_shared_state(Tracked& tracked, const View& view) const {
    const float p = view.partial;
    const Living& l = tracked.living;
    MobState& s = tracked.state;
    const float head = l.head_at(p);
    s.body_rot = l.body_at(p);
    s.y_rot = wrap_degrees(head - s.body_rot);
    s.x_rot = l.x_rot_at(p);
    if (!l.passenger && !l.dead) {
        s.walk_position = l.walk.position_at(p);
        s.walk_speed = l.walk.speed_at(p);
    } else {
        s.walk_position = 0.0f;
        s.walk_speed = 0.0f;
    }
    s.age_in_ticks = static_cast<float>(l.tick_count) + p;
    s.attack_time = l.attack_at(p);
    s.attack_arm_left = l.swinging_off_hand == s.main_arm_right;
    s.passenger = l.passenger;
    s.death_time = l.death_time > 0 ? static_cast<float>(l.death_time) + p : 0.0f;
    s.red_overlay = l.hurt_time > 0 || l.death_time > 0;
    s.partial = p;
    read_mob_counters(type_name(tracked.type), tracked.own, p, s);
}

// `LivingEntityRenderer.submit`: the sleep shuffle, the scale, the body's
// turn, the flip onto its feet, the renderer's own scale, and the lift that
// puts the model's feet on the ground.
Matrix Scene::root_of(const Tracked& tracked, const MobModel& model, const View& view) const {
    const float p = view.partial;
    const Living& l = tracked.living;
    const MobState& s = tracked.state;
    const double x = l.x_at(p);
    const double y = l.y_at(p);
    const double z = l.z_at(p);
    Placement place;
    place.matrix = translation(static_cast<float>(x - view.eye[0]),
                              static_cast<float>(y - view.eye[1]),
                              static_cast<float>(z - view.eye[2]));
    place.shape = Placement::kAffine | Placement::kTranslation | Placement::kOrthonormal;
    if (s.has_pose(EntityPose::Sleeping) && s.bed_orientation >= 0) {
        const auto step = bed_step(s.bed_orientation);
        const float back = s.eye_height - 0.1f;
        place.translate(-static_cast<float>(step[0]) * back, 0.0f,
                        -static_cast<float>(step[1]) * back);
    }
    const float scale = s.scale;
    place.scale(scale, scale, scale);
    model.setup_rotations(s, place, s.body_rot, scale);
    place.scale(-1.0f, -1.0f, 1.0f);
    model.scale(s, place);
    place.translate(0.0f, -1.501f, 0.0f);
    return place.matrix;
}

// One model over an already-posed body: the pose is whatever the base model
// left, which is how every layer of the game reuses it.
void Scene::submit(gpu::EntityRenderer& renderer, const Layer& layer, const MobState& state,
                   const Matrix& root, const gpu::EntityRenderer::Instance& like) {
    if (layer.model < 0 || layer.animator == nullptr || layer.texture < 0) return;
    const Model& data = layer.animator->model();
    layer.animator->animate(state, scratch_);
    if (matrices_.size() < data.parts.size()) matrices_.resize(data.parts.size());
    std::span<Matrix> out(matrices_.data(), data.parts.size());
    pose_matrices(data, scratch_, root, out);
    gpu::EntityRenderer::Instance instance = like;
    instance.model = layer.model;
    instance.texture = static_cast<uint32_t>(layer.texture);
    instance.type = layer.render;
    // `EyesLayer`: drawn through the dark at full brightness.
    if (layer.kind == Overlay::Eyes) {
        instance.sky_light = 15;
        instance.block_light = 15;
        instance.white = 0;
        instance.red = false;
    }
    if (renderer.add(instance, out)) {
        ++stats_.layers;
        stats_.parts += out.size();
    }
}

// `HumanoidArmorLayer.submit` and `EquipmentLayerRenderer.renderLayers`: the
// four pieces in the order the game draws them, each as the layers its
// equipment asset names, in the colour the dye or the asset gives them.
void Scene::submit_armour(gpu::EntityRenderer& renderer, std::span<const Layer> models,
                          const Tracked& tracked, const Matrix& root,
                          const gpu::EntityRenderer::Instance& like) {
    if (equipment_ == nullptr || renderer_ == nullptr) return;
    for (size_t piece = 0; piece < kArmour.size(); ++piece) {
        const ItemStack& stack = tracked.worn.slots[kArmour[piece].slot];
        if (stack.count <= 0) continue;
        const auto* equippable = equipment_->equippable(stack.id);
        // `HumanoidArmorLayer.shouldRender`: the item must name an asset, and
        // the slot it names must be the one it is in.
        if (equippable == nullptr || equippable->slot != kArmour[piece].slot) continue;
        const uint32_t dyed = dyed_colour(stack);
        for (const EquipmentLayer& layer : equipment_->layers(equippable->asset,
                                                              kArmour[piece].type)) {
            const uint32_t colour = equipment_layer_colour(layer, dyed);
            if (colour == 0) continue;
            const int32_t texture = renderer_->texture(layer.texture);
            if (texture < 0) continue;
            Layer one = models[piece];
            one.texture = texture;
            gpu::EntityRenderer::Instance instance = like;
            instance.colour = colour;
            submit(renderer, one, tracked.state, root, instance);
        }
    }
}

// `DyedItemColor.getOrDefault`: the dye on a piece of leather armour, or zero.
uint32_t Scene::dyed_colour(const ItemStack& stack) const {
    const proto::Value* dyed = stack.components.find("minecraft:dyed_color", protocol_);
    if (dyed == nullptr) return 0;
    if (const auto rgb = dyed->as_int()) {
        return argb_opaque(static_cast<uint32_t>(*rgb));
    }
    return 0;
}

void Scene::draw_one(gpu::EntityRenderer& renderer, Tracked& tracked, const View& view,
                     const LightAt& light) {
    TypeInfo& info = type_info(tracked.type);
    if (info.row == nullptr) return;
    fill_shared_state(tracked, view);
    const MobState& s = tracked.state;
    const size_t age = s.baby ? 1 : 0;
    const Layer& base = info.base[age];
    if (base.animator == nullptr || base.model < 0 || base.texture < 0) return;

    const Model& data = base.animator->model();
    if (tracked.pose.parts.size() != data.parts.size()) tracked.pose.reset(data);
    base.animator->animate(s, tracked.pose);

    const Matrix root = root_of(tracked, *base.animator, view);
    if (matrices_.size() < data.parts.size()) matrices_.resize(data.parts.size());
    std::span<Matrix> out(matrices_.data(), data.parts.size());
    pose_matrices(data, tracked.pose, root, out);

    const float p = view.partial;
    const double x = tracked.living.x_at(p);
    const double z = tracked.living.z_at(p);
    const auto lit = light(static_cast<int32_t>(std::floor(x)),
                           static_cast<int32_t>(std::floor(tracked.living.y_at(p) + s.eye_height)),
                           static_cast<int32_t>(std::floor(z)));

    gpu::EntityRenderer::Instance instance;
    instance.model = base.model;
    instance.type = base.render;
    instance.texture = static_cast<uint32_t>(base.texture);
    instance.sky_light = lit[0];
    instance.block_light = lit[1];
    instance.red = s.red_overlay;
    // `LivingEntityRenderer.getWhiteOverlayProgress`: the creeper's flash.
    instance.white = static_cast<uint8_t>(
        std::clamp(base.animator->white_overlay(s), 0.0f, 1.0f) * 15.0f);
    instance.colour = base.animator->tint(s);
    // `LivingEntityRenderer.isBodyVisible`: an invisible mob draws nothing but
    // its layers, and not even those unless it glows.
    if (!s.invisible && renderer.add(instance, out)) {
        ++stats_.drawn;
        ++stats_.layers;
        stats_.parts += out.size();
    }

    for (size_t at = 0; at < info.overlays[age].size(); ++at) {
        const MobOverlay& over = info.row->overlays[at];
        gpu::EntityRenderer::Instance tinted = instance;
        if (over.colour != nullptr) {
            tinted.colour = over.colour(s);
            // A layer whose colour works out to nothing is not drawn at all,
            // the way the game skips a collar an animal is not wearing.
            if (tinted.colour == 0) continue;
        }
        submit(renderer, info.overlays[age][at], s, root, tinted);
    }
    // `SheepWoolLayer`: a shorn sheep wears neither of its wool layers.
    if (info.has_armour) {
        submit_armour(renderer, info.armour[age], tracked, root, instance);
    }
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
    const Avatar* avatar = slim ? slim_ : wide_;
    if (avatar == nullptr || model_index < 0) return;
    uint32_t texture = 0;
    if (layer >= 0) {
        texture = gpu::EntityRenderer::kSkinBit | static_cast<uint32_t>(layer);
    } else {
        const int32_t baked = renderer.texture(info.default_texture);
        if (baked < 0) return;
        texture = static_cast<uint32_t>(baked);
    }

    fill_shared_state(tracked, view);
    MobState& s = tracked.state;
    s.spectator = info.spectator;
    const Model& data = avatar->model();
    if (tracked.pose.parts.size() != data.parts.size()) tracked.pose.reset(data);
    avatar->animate(s, tracked.pose);

    // `AvatarRenderer.getRenderOffset`: a crouching player sits two pixels lower.
    Matrix root = root_of(tracked, *avatar, view);
    const double x = l.x_at(p);
    const double y = l.y_at(p) + (s.crouching ? s.scale * -2.0 / 16.0 : 0.0);
    const double z = l.z_at(p);
    if (s.crouching) {
        root = multiply(translation(0.0f, static_cast<float>(s.scale * -2.0 / 16.0), 0.0f), root);
    }

    if (matrices_.size() < data.parts.size()) matrices_.resize(data.parts.size());
    std::span<Matrix> out(matrices_.data(), data.parts.size());
    pose_matrices(data, tracked.pose, root, out);

    const auto lit =
        light(static_cast<int32_t>(std::floor(x)),
              static_cast<int32_t>(std::floor(l.y_at(p) + (s.crouching ? 1.27 : 1.62))),
              static_cast<int32_t>(std::floor(z)));
    gpu::EntityRenderer::Instance instance;
    instance.model = model_index;
    instance.type = gpu::EntityRenderer::Type::Translucent;
    instance.texture = texture;
    instance.sky_light = lit[0];
    instance.block_light = lit[1];
    instance.red = s.red_overlay;
    if (!s.invisible && renderer.add(instance, out)) {
        ++stats_.drawn;
        ++stats_.layers;
        stats_.parts += out.size();
    }
    submit_armour(renderer, player_armour_[slim ? 1 : 0], tracked, root, instance);

    if (view.name_tags && !info.name.empty()) {
        name_tag(renderer, tracked, view, info.name, x, y, z, s.crouching, lit);
    }
}

void Scene::name_tag(gpu::EntityRenderer& renderer, const Tracked& tracked, const View& view,
                     std::string_view text, double x, double y, double z, bool discrete,
                     std::array<uint8_t, 2> light) {
    const double height = discrete ? kCrouchingHeight : kPlayerHeight;
    const double dx = x - view.eye[0];
    const double dy = y - view.eye[1];
    const double dz = z - view.eye[2];
    const double distance = dx * dx + dy * dy + dz * dz;
    if (distance >= 4096.0) return;
    if (discrete && distance >= 1024.0) return;
    (void)tracked;

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
