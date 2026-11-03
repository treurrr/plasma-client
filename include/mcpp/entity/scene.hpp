#pragma once
#include "mcpp/entity/assets.hpp"
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
    // Our own entity, which first person does not draw.
    int32_t self = -1;
    bool name_tags = true;
};

// Every entity the client knows about, as the client draws it: the server's
// state, eased in, posed and handed to the renderer once a frame.
class Scene {
public:
    Scene(const Assets& assets, std::vector<std::string> types, skins::Cache* skins = nullptr);

    void add(const Entity& entity);
    void move(const Entity& entity);
    void remove(int32_t id);
    void animate(int32_t id, uint8_t action);
    void hurt(int32_t id);
    void event(int32_t id, uint8_t status);
    void clear() { tracked_.clear(); }

    void tick();
    void draw(gpu::EntityRenderer& renderer, const Session& session, const View& view,
              const LightAt& light);

    size_t tracked() const { return tracked_.size(); }
    size_t drawn() const { return drawn_; }
    // How many of the tracked have no model yet, by type: what is still missing.
    const std::map<std::string, size_t>& missing() const { return missing_; }
    const Motion* motion(int32_t id) const;
    std::string_view type_name(int32_t type) const;

private:
    struct Tracked {
        Entity entity;
        Motion motion;
    };

    const Assets& assets_;
    std::vector<std::string> types_;
    skins::Cache* skins_;
    std::map<int32_t, Tracked> tracked_;
    std::map<std::string, size_t> missing_;
    size_t drawn_ = 0;
    std::vector<Vertex> scratch_;
};

} // namespace mcpp::entity
