#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace mcpp::world {

// Coordinates are fixed-point: 256 per model unit, 16 model units per block.
constexpr int32_t kUnitsPerModelUnit = 256;
constexpr int32_t kUnitsPerBlock = kUnitsPerModelUnit * 16;

// Which colour a tinted face takes. The model says a face is tinted; the block
// says which tint, and three of them ignore the colour maps entirely.
enum class TintKind : int8_t {
    None = -1, Grass, Foliage, Water, Evergreen, Birch, LilyPad, DryFoliage
};

// A model can switch off directional shading and ambient occlusion, and the
// cross models every plant uses switch off both.
enum QuadFlags : uint8_t {
    kQuadUvlock = 1 << 0,
    kQuadNoShade = 1 << 1,
    kQuadNoAo = 1 << 2,
    // Fractional alpha: blended after everything solid, not cut out.
    kQuadTranslucent = 1 << 3,
    // Widen the element so its corners stay where they were after the turn.
    kQuadRescale = 1 << 4,
};

#pragma pack(push, 1)
struct BakedQuad {
    int16_t from[3];
    int16_t to[3];
    int16_t uv[4];
    uint16_t texture;
    uint8_t face;
    int8_t cullface;    // -1 when the face is never culled
    int8_t tint;        // -1 when untinted, otherwise a TintKind
    uint8_t flags;      // QuadFlags
    uint8_t rotation_x; // quarter turns
    uint8_t rotation_y;
    // The element's own turn, which the blockstate's quarter turns know
    // nothing about: up to two eighths of 45 degrees about one axis.
    int8_t element_eighths;
    uint8_t element_axis;
    int16_t element_origin[3];
};
#pragma pack(pop)
static_assert(sizeof(BakedQuad) == 36, "the baked layout must match the generator");

// Read-only view on a table built by tools/bake_block_table.py. Nothing is
// parsed or copied at startup: the file is mapped and indexed in place.
class BlockTable {
public:
    static constexpr uint8_t kNoModel = 1 << 0;
    static constexpr uint8_t kHasGeometry = 1 << 1;
    static constexpr uint8_t kFullCube = 1 << 2;
    static constexpr uint8_t kOccludes = 1 << 3;
    static constexpr uint8_t kFluid = 1 << 4;

    BlockTable() = default;
    ~BlockTable();

    BlockTable(const BlockTable&) = delete;
    BlockTable& operator=(const BlockTable&) = delete;
    BlockTable(BlockTable&& other) noexcept;
    BlockTable& operator=(BlockTable&& other) noexcept;

    // Throws ProtocolError when the file is present but not a usable table.
    bool open(const std::filesystem::path& path);
    void close();
    bool is_open() const { return base_ != nullptr; }

    int32_t protocol() const { return protocol_; }
    size_t state_count() const { return state_count_; }
    size_t quad_count() const { return quad_count_; }
    size_t mapped_bytes() const { return size_; }

    std::span<const BakedQuad> quads(int32_t state) const;
    std::string_view block_name(int32_t state) const;
    std::string_view texture_name(uint16_t texture) const;
    uint8_t flags(int32_t state) const;
    bool full_cube(int32_t state) const { return (flags(state) & kFullCube) != 0; }
    bool occludes(int32_t state) const { return (flags(state) & kOccludes) != 0; }
    bool fluid(int32_t state) const { return (flags(state) & kFluid) != 0; }

    static std::filesystem::path default_path(int32_t protocol);

private:
    const uint8_t* base_ = nullptr;
    size_t size_ = 0;
    int32_t protocol_ = 0;
    size_t state_count_ = 0;
    size_t quad_count_ = 0;
    size_t texture_count_ = 0;
    size_t block_count_ = 0;

    const uint32_t* quad_offsets_ = nullptr;
    const BakedQuad* quads_ = nullptr;
    const uint32_t* texture_names_ = nullptr;
    const uint32_t* block_names_ = nullptr;
    const uint32_t* state_block_ = nullptr;
    const uint8_t* state_flags_ = nullptr;
    const char* strings_ = nullptr;
    size_t string_bytes_ = 0;
};

} // namespace mcpp::world
