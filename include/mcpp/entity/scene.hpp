#pragma once
#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/humanoid.hpp"
#include "mcpp/entity/motion.hpp"
#include "mcpp/gpu/entity_renderer.hpp"
#include "mcpp/session.hpp"
#include "mcpp/skins.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
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
    size_t parts = 0;
    double pose_ms = 0.0;
};

// Every entity the client knows about, as the client draws it: the server's
// word eased in tick by tick, posed and handed to the renderer once a frame.
class Scene {
public:
    Scene(std::vector<std::string> types, skins::Cache* skins = nullptr);

    // Model numbers from the renderer, once it has loaded them.
    void bind(const gpu::EntityRenderer& renderer);

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

    // Once A's metadata reaches the entity: the pose and the flags byte.
    void set_crouching(int32_t id, bool crouching);

    void tick();
    void draw(gpu::EntityRenderer& renderer, const View& view, const LightAt& light);

    const SceneStats& stats() const { return stats_; }
    const Living* living(int32_t id) const;
    std::string_view type_name(int32_t type) const;

private:
    enum class Kind : uint8_t { Other, Player };
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
        Pose pose;
        bool crouching = false;
    };
    struct PlayerLine {
        std::string name;
        bool spectator = false;
        bool slim = false;
        std::string default_texture;
        Skin skin;
    };

    Tracked* find(int32_t id);
    void draw_player(gpu::EntityRenderer& renderer, Tracked& tracked, const View& view,
                     const LightAt& light);
    void name_tag(gpu::EntityRenderer& renderer, const Tracked& tracked, const View& view,
                  std::string_view text, double x, double y, double z, bool discrete,
                  std::array<uint8_t, 2> light);
    int32_t skin_layer(gpu::EntityRenderer& renderer, Skin& skin);

    std::vector<std::string> types_;
    int32_t player_type_ = -1;
    skins::Cache* skins_;
    std::vector<Tracked> tracked_;
    std::unordered_map<int32_t, size_t> index_;
    std::map<std::string, PlayerLine, std::less<>> players_;
    // A skin layer per url, shared by everyone wearing that skin.
    std::map<std::string, std::pair<int32_t, int32_t>, std::less<>> layers_;

    int32_t wide_model_ = -1;
    int32_t slim_model_ = -1;
    HumanoidParts wide_parts_;
    HumanoidParts slim_parts_;
    std::vector<Matrix> matrices_;
    SceneStats stats_;
};

} // namespace mcpp::entity
