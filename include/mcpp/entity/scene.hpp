#pragma once
#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/equipment.hpp"
#include "mcpp/entity/held_items.hpp"
#include "mcpp/entity/humanoid.hpp"
#include "mcpp/entity/mob_tick.hpp"
#include "mcpp/entity/mobs.hpp"
#include "mcpp/entity/motion.hpp"
#include "mcpp/gpu/entity_renderer.hpp"
#include "mcpp/session.hpp"
#include "mcpp/skins.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mcpp::entity {

// Entity type names in registry order, from the baked registries file, so a
// type number from add_entity becomes "minecraft:zombie".
std::vector<std::string> load_entity_types(const std::filesystem::path& registries);
std::filesystem::path default_registries_path(int32_t protocol);

// Sky and block light where an entity stands, 0 to 15 each.
using LightAt = std::function<std::array<uint8_t, 2>(int32_t, int32_t, int32_t)>;

struct View {
    double eye[3]{};
    float forward[3]{0.0f, 0.0f, 1.0f};
    float partial = 0.0f;
    bool name_tags = true;
};

// What the drawing of one frame cost, for the debug line and the report.
struct SceneStats {
    size_t tracked = 0;
    size_t drawn = 0;
    size_t layers = 0;
    size_t parts = 0;
    double pose_ms = 0.0;
};

// Every entity the client knows about, as the client draws it: the server's
// word eased in tick by tick, posed and handed to the renderer once a frame.
class Scene {
public:
    Scene(std::vector<std::string> types, skins::Cache* skins = nullptr);

    // Model numbers from the renderer, once it has loaded them; the armour
    // textures too, when they have been baked.
    void bind(const gpu::EntityRenderer& renderer);
    void bind(const gpu::EntityRenderer& renderer, const EquipmentAssets* equipment);
    // `ItemInHandLayer`: what a mob holds is item geometry, not a model layer,
    // so it goes to the renderer that owns the item quads. Without one, a mob
    // is drawn with empty hands.
    void hold_items_with(gpu::HandRenderer* hand, const world::Atmosphere* atmosphere);

    void add(const Entity& entity);
    void moved(const EntityMove& move);
    void head(int32_t id, float yaw);
    void remove(int32_t id);
    void animate(int32_t id, uint8_t action);
    void hurt(int32_t id);
    void damaged(int32_t id);
    void event(int32_t id, uint8_t status);
    void player_info(const PlayerInfo& info);
    void player_gone(std::string_view uuid);
    void clear();

    // What set_entity_data last said about this entity, every index merged.
    void metadata(int32_t id, const proto::Metadata& data);
    // What set_equipment last said is worn and held.
    void equipment(int32_t id, const Equipment& worn);
    // set_passengers: a rider sits, and the vehicle's own walk drives nothing.
    void riding(int32_t id, bool passenger);

    void tick();
    void draw(gpu::EntityRenderer& renderer, const View& view, const LightAt& light);

    const SceneStats& stats() const { return stats_; }
    const Living* living(int32_t id) const;
    const MobState* state(int32_t id) const;
    std::string_view type_name(int32_t type) const;

private:
    enum class Kind : uint8_t { Other, Player };

    // One model and texture to draw over a body: the base layer, an overlay,
    // or one armour piece.
    struct Layer {
        int32_t model = -1;
        const MobModel* animator = nullptr;
        int32_t texture = -1;
        Overlay kind = Overlay::Plain;
        gpu::EntityRenderer::Type render = gpu::EntityRenderer::Type::CutoutNoCull;
    };

    // Everything about one entity type that is the same for every entity of
    // it, worked out the first time one is seen.
    struct TypeInfo {
        const MobType* row = nullptr;
        bool ready = false;
        std::array<Layer, 2> base{};   // adult, baby
        std::array<std::vector<Layer>, 2> overlays{};
        // `HumanoidArmorLayer`: feet, legs, chest, head, adult and baby. The
        // texture comes from the item, so only the model is settled here.
        std::array<std::array<Layer, 4>, 2> armour{};
        bool has_armour = false;
        // `ArmedModel.translateToHand`: whose version of it this body uses.
        HandModel hand_model = HandModel::Plain;
        // `EntityType.getDimensions`: the box the type stands in, which the
        // name tag floats over and the light is read at.
        float width = 0.6f;
        float height = 1.8f;
        float eye_height = 1.62f;
    };

    struct Skin {
        std::string url;
        bool slim = false;
        int32_t layer = -1;   // a skin layer once it is in
        bool failed = false;
    };
    struct Tracked {
        int32_t id = 0;
        int32_t type = 0;
        Kind kind = Kind::Other;
        std::string uuid;
        Living living;
        MobState state;
        Pose pose;
        Equipment worn;
        // What set_entity_data said, kept so every tick can read it again.
        proto::Metadata data;
        // The counters this mob's own tick keeps, which no packet carries.
        MobTick own;
        // `Entity.getCustomName`, and whether the server wants it shown.
        std::string custom_name;
        bool show_name = false;
    };
    struct PlayerLine {
        std::string name;
        bool spectator = false;
        bool slim = false;
        std::string default_texture;
        Skin skin;
    };

    Tracked* find(int32_t id);
    TypeInfo& type_info(int32_t type);
    // The model for a layer, built once and shared by every entity using it.
    const MobModel* animator(std::string_view layer);
    Layer layer_for(std::string_view layer_name, std::string_view texture, Overlay kind,
                    gpu::EntityRenderer::Type render);

    void fill_shared_state(Tracked& tracked, const View& view) const;
    uint32_t dyed_colour(const ItemStack& stack) const;
    void draw_one(gpu::EntityRenderer& renderer, Tracked& tracked, const View& view,
                  const LightAt& light);
    void draw_player(gpu::EntityRenderer& renderer, Tracked& tracked, const View& view,
                     const LightAt& light);
    // One model over an already-posed body: an overlay, or an armour piece.
    void submit(gpu::EntityRenderer& renderer, const Layer& layer, const MobState& state,
                const Matrix& root, const gpu::EntityRenderer::Instance& like);
    void submit_armour(gpu::EntityRenderer& renderer, std::span<const Layer> models,
                       const Tracked& tracked, const Matrix& root,
                       const gpu::EntityRenderer::Instance& like);
    void items_in_hand(HandModel kind, const MobModel& animator, Tracked& tracked,
                       const Matrix& root, std::array<uint8_t, 2> light);
    void name_tag(gpu::EntityRenderer& renderer, const Tracked& tracked, const View& view,
                  std::string_view text, double x, double y, double z, bool discrete,
                  std::array<uint8_t, 2> light);
    int32_t skin_layer(gpu::EntityRenderer& renderer, Skin& skin);
    // The root placement every layer of one entity hangs from.
    Matrix root_of(const Tracked& tracked, const MobModel& model, const View& view) const;

    std::vector<std::string> types_;
    int32_t player_type_ = -1;
    skins::Cache* skins_;
    const gpu::EntityRenderer* renderer_ = nullptr;
    const EquipmentAssets* equipment_ = nullptr;
    gpu::HandRenderer* hand_ = nullptr;
    const world::Atmosphere* atmosphere_ = nullptr;
    std::vector<Tracked> tracked_;
    std::unordered_map<int32_t, size_t> index_;
    std::map<std::string, PlayerLine, std::less<>> players_;
    // A skin layer per url, shared by everyone wearing that skin.
    std::map<std::string, std::pair<int32_t, int32_t>, std::less<>> layers_;
    // One model object per layer, and one worked-out row per entity type.
    std::map<std::string, std::unique_ptr<MobModel>, std::less<>> models_;
    std::vector<TypeInfo> type_info_;
    // Width, height and eye height by type name, from the baked table.
    std::map<std::string, std::array<float, 3>, std::less<>> dimensions_;

    int32_t wide_model_ = -1;
    int32_t slim_model_ = -1;
    const Avatar* wide_ = nullptr;
    const Avatar* slim_ = nullptr;
    // The player's own four armour layers, wide and slim.
    std::array<std::array<Layer, 4>, 2> player_armour_{};
    std::vector<Matrix> matrices_;
    // The same model posed again with one arm moved, for the item in it.
    std::vector<Matrix> hand_matrices_;
    // The pose an overlay or an armour piece is animated into: one layer at a
    // time, so one buffer serves them all.
    Pose scratch_;
    SceneStats stats_;
    uint64_t ticks_ = 0;
    int32_t protocol_ = 774;
};

} // namespace mcpp::entity
