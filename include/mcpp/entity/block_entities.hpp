#pragma once
#include "mcpp/entity/assets.hpp"
#include "mcpp/entity/model.hpp"
#include "mcpp/entity/scene.hpp"
#include "mcpp/gpu/entity_renderer.hpp"
#include "mcpp/nbt.hpp"
#include "mcpp/skins.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcpp::world {
struct Chunk;
}

namespace mcpp::entity {

// `BlockEntityRenderers`: the blocks the game draws with a model of their own,
// on top of whatever the chunk mesh already has for them.
enum class BlockEntityKind : uint8_t {
    None, Chest, Sign, HangingSign, Bed, Banner, Skull, ShulkerBox, Bell, Conduit,
    DecoratedPot, EnchantingTable, Lectern, CopperGolemStatue,
};

// One block state's renderer, as tools/bake_block_entities.py worked it out:
// which layer, which picture, and the few properties the renderer reads.
struct BlockEntityRow {
    BlockEntityKind kind = BlockEntityKind::None;
    int16_t model = -1;
    int16_t texture = -1;
    int8_t facing = -1;    // the Direction's 3d value
    int8_t rotation = -1;  // a sixteenth of a turn, for a free-standing block
    int8_t colour = -1;
    uint8_t variant = 0;
    uint8_t flag = 0;
};

class BlockEntityTable {
public:
    bool open(const std::filesystem::path& path);
    // Throws with the line number on anything it does not understand.
    void parse(std::string_view text);

    const BlockEntityRow* find(int32_t state) const;
    std::string_view layer(int16_t index) const;
    std::string_view texture(int16_t index) const;
    size_t size() const { return rows_.size(); }

    static std::filesystem::path default_path(int32_t protocol);

private:
    int16_t intern(std::vector<std::string>& pool, std::map<std::string, int16_t, std::less<>>& by,
                   std::string_view name);

    std::unordered_map<int32_t, BlockEntityRow> rows_;
    std::vector<std::string> layers_;
    std::vector<std::string> textures_;
    std::map<std::string, int16_t, std::less<>> layer_index_;
    std::map<std::string, int16_t, std::less<>> texture_index_;
};

// One block entity as the client holds it: what the server sent, and the bit
// of animation the client runs on its own.
struct BlockEntity {
    int32_t x = 0, y = 0, z = 0;
    nbt::Value data;
    // `ChestLidController`: how far the lid is open, eased a step a tick while
    // anyone is looking inside.
    float openness = 0.0f;
    float last_openness = 0.0f;
    int32_t viewers = 0;
    int32_t ticks = 0;
};

// Where the camera is, and what the world around a block entity looks like.
using BlockStateAt = std::function<int32_t(int32_t, int32_t, int32_t)>;

struct BlockEntityStats {
    size_t tracked = 0;
    size_t drawn = 0;
    size_t models = 0;
    size_t parts = 0;
    double pose_ms = 0.0;
};

// Every block entity the client knows about, drawn the way the game draws one:
// a model layer out of the entity model table, placed by its own renderer,
// handed to the same instanced renderer every mob goes through.
class BlockEntityScene {
public:
    void load(const Assets& assets, const BlockEntityTable& table,
              skins::Cache* skins = nullptr);

    void put(int32_t x, int32_t y, int32_t z, nbt::Value data);
    // `LevelChunk.setBlockState`: a block that carries a block entity gets one
    // the moment the block is there, whether or not the server had anything to
    // say about it. An empty chest is still a chest with a lid.
    void appear(int32_t x, int32_t y, int32_t z);
    void removed(int32_t x, int32_t y, int32_t z);
    void forget_chunk(int32_t chunk_x, int32_t chunk_z);
    // `ChestBlockEntity.triggerEvent`: how many players have it open.
    void event(int32_t x, int32_t y, int32_t z, uint8_t action, uint8_t parameter);
    void clear();
    void tick();

    void draw(gpu::EntityRenderer& renderer, const View& view, const BlockStateAt& state,
              const LightAt& light, int64_t game_time);

    const BlockEntityStats& stats() const { return stats_; }
    size_t size() const { return entities_.size(); }
    template <typename Visit>
    void each(Visit&& visit) const {
        for (const auto& [key, entity] : entities_) visit(entity);
    }

    // One model of one block entity, already placed: what `draw` hands the
    // renderer, and what a probe compares with the game.
    struct Piece {
        const Model* model = nullptr;
        std::string texture;
        uint32_t colour = 0xFFFFFFFFu;
        gpu::EntityRenderer::Type type = gpu::EntityRenderer::Type::Cutout;
        Matrix root = identity();
        Pose pose;
        // Only this part and what hangs from it, when the renderer draws one.
        std::string_view part;
    };
    // Everything one block entity is drawn with, in the order the game submits
    // it. `models` resolves a layer name; `root` is where the block's own
    // corner sits. Static so a probe can ask without a renderer.
    static void build(const BlockEntityRow& row, const BlockEntityTable& table,
                      const BlockEntity& entity, const ModelSet& models,
                      const Placement& root, float partial, int64_t game_time,
                      std::vector<Piece>& out);

    // The four lines of one of a sign's faces, laid out as the game lays them.
    struct SignLine {
        std::string text;
        Matrix place = identity();
        uint32_t colour = 0xFFFFFFFFu;
        bool glowing = false;
    };
    static void sign_text(const BlockEntityRow& row, const BlockEntity& entity,
                          const Placement& root, const Font& font, std::vector<SignLine>& out);

private:
    struct Key {
        int64_t packed;
        bool operator==(const Key& other) const { return packed == other.packed; }
    };
    struct KeyHash {
        size_t operator()(const Key& key) const {
            return std::hash<int64_t>{}(key.packed);
        }
    };

    static Key key_of(int32_t x, int32_t y, int32_t z);
    // The skin a player head's profile names, uploaded once per url.
    int32_t skin_for(gpu::EntityRenderer& renderer, const nbt::Value& data);

    const BlockEntityTable* table_ = nullptr;
    const ModelSet* models_ = nullptr;
    const Font* font_ = nullptr;
    const Assets* assets_ = nullptr;
    skins::Cache* skins_ = nullptr;
    std::unordered_map<Key, BlockEntity, KeyHash> entities_;
    std::map<std::string, std::pair<int32_t, bool>, std::less<>> skin_layers_;
    std::vector<Piece> pieces_;
    std::vector<SignLine> lines_;
    std::vector<Matrix> matrices_;
    BlockEntityStats stats_;
};

// Every block in a chunk that carries one, given a block entity.
void scan_chunk(const world::Chunk& chunk, const BlockEntityTable& table,
                BlockEntityScene& scene);

} // namespace mcpp::entity
