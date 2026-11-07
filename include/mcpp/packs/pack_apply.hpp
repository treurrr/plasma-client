#pragma once
#include "mcpp/packs/mipmap.hpp"
#include "mcpp/packs/resource_pack.hpp"
#include "mcpp/world/atlas.hpp"
#include "mcpp/world/block_table.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Putting a pack on top of the baked tables while the game runs, and taking it
// off again. Everything Plasma draws comes from tables baked out of the
// player's own jar; a pack has to land on those without a re-bake and without
// a restart. The shape of the baked tables is what makes that cheap - one
// array layer per block texture, one per item icon - and docs/PACKS.md says
// why and what is not done.
namespace mcpp::packs {

// One array layer and the whole mip chain to write into it.
struct LayerPatch {
    uint32_t layer = 0;
    std::vector<std::vector<uint8_t>> levels;
};

// What the renderers have to be able to do for a pack to land. The real one
// sits in front of WorldRenderer and UiRenderer; the test uses its own, so
// the whole path is exercised without a gpu.
class PackTarget {
public:
    virtual ~PackTarget() = default;

    virtual uint32_t atlas_tile() const = 0;
    virtual uint32_t atlas_mips() const = 0;
    virtual uint32_t atlas_layers() const = 0;
    // Returns the time the upload took, in milliseconds.
    virtual double patch_atlas(std::span<const LayerPatch> patches) = 0;

    virtual uint32_t item_tile() const = 0;
    virtual int32_t item_layer_of(std::string_view name) const = 0;
    virtual bool set_item_icon(int32_t layer, const Bitmap& icon) = 0;

    virtual std::array<uint32_t, 4> sprite_rect(std::string_view name) const = 0;
    virtual bool set_sprite_pixels(std::string_view name, const Bitmap& pixels) = 0;
};

// What applying or taking off a pack cost and changed.
struct ApplyStats {
    size_t block_textures = 0;
    size_t atlas_layers = 0;
    size_t item_icons = 0;
    size_t gui_sprites = 0;
    size_t skipped = 0;
    double read_ms = 0.0;
    double mip_ms = 0.0;
    double upload_ms = 0.0;
    double total_ms = 0.0;
};

class PackApplier {
public:
    // Opens the baked tables it needs: the block table for the texture names
    // that are also the atlas layers, the atlas for the pixels to put back,
    // and the flat item recipes tools/bake_item_layers.py wrote.
    bool open(int32_t protocol);
    bool is_open() const { return open_; }
    const std::string& error() const { return error_; }

    // Everything a pack replaces, written over the baked tables. The target
    // decides what it can take; whatever it cannot is counted as skipped.
    ApplyStats apply(PackStack& packs, PackTarget& target);
    // The baked look back, without a restart: every layer this applier
    // touched is written again from the baked tables.
    ApplyStats revert(PackTarget& target);
    bool anything_applied() const { return !touched_layers_.empty() || !touched_items_.empty() ||
                                           !touched_sprites_.empty(); }

    // "minecraft:block/stone" to the jar path the bakers read it from.
    static std::string texture_asset_path(std::string_view name);
    // The other way: a pack's path back to the name the block table uses, or
    // nothing when the path is not a texture at all.
    static std::optional<std::string> texture_name_of(std::string_view asset);
    // The name tools/bake_gui.py packed this jar path under, or nothing when
    // it packed no such sprite.
    static std::optional<std::string> gui_sprite_name_of(std::string_view asset);

    size_t block_texture_count() const { return texture_layers_.size(); }
    size_t item_recipe_count() const { return item_recipes_.size(); }

private:
    struct ItemLayer {
        std::string texture;
        std::array<uint8_t, 4> tint{255, 255, 255, 255};
    };
    struct ItemRecipe {
        int32_t id = 0;
        std::string name;
        std::vector<ItemLayer> layers;
    };

    std::optional<Bitmap> read_texture(PackStack& packs, const std::string& name);
    Bitmap baked_layer(uint32_t layer, uint32_t mip) const;
    void patch_texture(PackStack& packs, PackTarget& target, const std::string& name,
                       const std::vector<uint32_t>& layers, std::vector<LayerPatch>& patches,
                       ApplyStats& stats);
    void patch_items(PackStack& packs, PackTarget& target, ApplyStats& stats);
    void patch_sprites(PackStack& packs, PackTarget& target, ApplyStats& stats);

    bool open_ = false;
    std::string error_;
    world::BlockTable blocks_;
    world::Atlas atlas_;
    // A texture's name with the namespace stripped, to the layers that hold
    // it. The block table names a handful of textures twice - once with the
    // namespace and once without - and both are layers of their own, so both
    // have to be written or half of a replaced water surface stays baked.
    std::map<std::string, std::vector<uint32_t>, std::less<>> texture_layers_;
    // The further layers an animated texture's frames live in.
    std::map<uint32_t, std::vector<uint32_t>> animation_layers_;
    std::vector<ItemRecipe> item_recipes_;
    // Which item ids read a given texture, so one replaced picture composes
    // only the icons that use it.
    std::map<std::string, std::vector<size_t>, std::less<>> items_by_texture_;
    // Reset by revert: what has been written over.
    std::vector<uint32_t> touched_layers_;
    std::vector<int32_t> touched_items_;
    std::vector<std::string> touched_sprites_;

    // The baked item and interface pixels, read only once a pack asks for
    // them: together they are twenty-odd megabytes nothing else needs.
    bool load_baked_items();
    bool load_baked_gui();
    std::filesystem::path items_path_;
    std::filesystem::path gui_path_;
    uint32_t baked_item_tile_ = 0;
    uint32_t baked_item_layers_ = 0;
    uint32_t baked_gui_side_ = 0;
    std::vector<uint8_t> baked_item_pixels_;
    std::vector<uint8_t> baked_gui_pixels_;
};

} // namespace mcpp::packs
