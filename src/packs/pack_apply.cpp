#include "mcpp/packs/pack_apply.hpp"

#include "mcpp/image_png.hpp"
#include "mcpp/json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <format>

namespace mcpp::packs {
namespace {

constexpr char kTexturePrefix[] = "assets/minecraft/textures/";
constexpr char kGuiSpritePrefix[] = "assets/minecraft/textures/gui/sprites/";
constexpr char kGuiPrefix[] = "assets/minecraft/textures/gui/";
constexpr char kContainerPrefix[] = "assets/minecraft/textures/gui/container/";
constexpr char kEffectPrefix[] = "assets/minecraft/textures/mob_effect/";

// The same place `world::Atlas::default_path` looks, for the two tables that
// have no reader of their own here.
std::filesystem::path baked_data_directory() {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data";
}

// A texture name without its namespace: the block table carries both spellings.
std::string strip_namespace(std::string_view name) {
    const size_t colon = name.rfind(':');
    return std::string(colon == std::string_view::npos ? name : name.substr(colon + 1));
}

double ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

struct Stopwatch {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    double* into = nullptr;
    ~Stopwatch() {
        if (into != nullptr) *into += ms_since(start);
    }
};

Bitmap from_image(const Image& image) {
    Bitmap out(image.width, image.height);
    out.rgba = image.rgba;
    return out;
}

uint8_t hex_pair(char high, char low) {
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    return static_cast<uint8_t>(digit(high) * 16 + digit(low));
}

// `tools/bake_items.py Textures.get`: a slot shows an animated texture's first
// frame, so a strip is cut down to its own width.
Bitmap first_frame(const Bitmap& picture) {
    if (picture.height <= picture.width) return picture;
    Bitmap out(picture.width, picture.width);
    std::copy_n(picture.rgba.data(), out.rgba.size(), out.rgba.data());
    return out;
}

} // namespace

std::string PackApplier::texture_asset_path(std::string_view name) {
    const size_t colon = name.rfind(':');
    const std::string_view rest = colon == std::string_view::npos ? name : name.substr(colon + 1);
    return std::format("{}{}.png", kTexturePrefix, rest);
}

std::optional<std::string> PackApplier::texture_name_of(std::string_view asset) {
    if (!asset.starts_with(kTexturePrefix) || !asset.ends_with(".png")) return std::nullopt;
    const size_t prefix = std::string_view(kTexturePrefix).size();
    return std::string(asset.substr(prefix, asset.size() - prefix - 4));
}

// `tools/bake_gui.py main`: the sprite names it packs under.
std::optional<std::string> PackApplier::gui_sprite_name_of(std::string_view asset) {
    if (!asset.ends_with(".png")) return std::nullopt;
    const auto strip = [&](std::string_view prefix) {
        return std::string(asset.substr(prefix.size(), asset.size() - prefix.size() - 4));
    };
    if (asset == "assets/minecraft/textures/misc/unknown_server.png") {
        return std::string("misc/unknown_server");
    }
    if (asset.starts_with(kGuiSpritePrefix)) return strip(kGuiSpritePrefix);
    if (asset.starts_with(kContainerPrefix)) return "container/" + strip(kContainerPrefix);
    if (asset.starts_with(kEffectPrefix)) return "mob_effect/" + strip(kEffectPrefix);
    if (asset.starts_with(kGuiPrefix)) {
        const std::string rest = strip(kGuiPrefix);
        // Only the sheets at the top of gui/ are packed under gui/<name>, plus
        // the title screen's own pictures a folder down.
        static const char* kTitle[] = {"title/minecraft", "title/minceraft", "title/edition",
                                       "title/background/panorama_overlay"};
        if (rest.find('/') == std::string::npos && rest != "demo_background") {
            return "gui/" + rest;
        }
        for (const char* one : kTitle) {
            if (rest == one) return "gui/" + rest;
        }
    }
    return std::nullopt;
}

bool PackApplier::open(int32_t protocol) {
    open_ = false;
    error_.clear();
    if (!blocks_.open(world::BlockTable::default_path(protocol))) {
        error_ = "the block table is missing; bake with tools/bake_all.py";
        return false;
    }
    if (!atlas_.open(world::Atlas::default_path(protocol))) {
        error_ = "the atlas is missing; bake with tools/bake_all.py";
        return false;
    }
    for (uint32_t texture = 0; texture < blocks_.texture_count(); ++texture) {
        texture_layers_[strip_namespace(blocks_.texture_name(texture))].push_back(texture);
    }
    for (const world::Atlas::Animation& animation : atlas_.animations()) {
        std::vector<uint32_t> layers;
        for (const auto& frame : animation.frames) {
            if (std::find(layers.begin(), layers.end(), frame.first) == layers.end()) {
                layers.push_back(frame.first);
            }
        }
        animation_layers_.emplace(animation.texture, std::move(layers));
    }

    const auto data = baked_data_directory();
    items_path_ = data / std::format("items-{}.plit", protocol);
    gui_path_ = data / std::format("gui-{}.plgu", protocol);

    // tools/bake_item_layers.py: the flat icons and the pictures they read.
    const auto layers_path = data / std::format("item_layers-{}.txt", protocol);
    std::ifstream input(layers_path);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::vector<std::string> fields;
        size_t at = 0;
        while (at <= line.size()) {
            const size_t tab = line.find('\t', at);
            fields.push_back(line.substr(at, tab == std::string::npos ? tab : tab - at));
            if (tab == std::string::npos) break;
            at = tab + 1;
        }
        if (fields.size() < 4 || fields[3].size() < 8) continue;
        const int32_t id = std::atoi(fields[0].c_str());
        ItemLayer layer;
        layer.texture = fields[2];
        for (int channel = 0; channel < 4; ++channel) {
            layer.tint[static_cast<size_t>(channel)] =
                hex_pair(fields[3][static_cast<size_t>(channel) * 2],
                         fields[3][static_cast<size_t>(channel) * 2 + 1]);
        }
        if (item_recipes_.empty() || item_recipes_.back().id != id) {
            item_recipes_.push_back(ItemRecipe{id, fields[1], {}});
        }
        item_recipes_.back().layers.push_back(std::move(layer));
    }
    for (size_t index = 0; index < item_recipes_.size(); ++index) {
        for (const ItemLayer& layer : item_recipes_[index].layers) {
            items_by_texture_[layer.texture].push_back(index);
        }
    }
    open_ = true;
    return true;
}

bool PackApplier::load_baked_items() {
    if (!baked_item_pixels_.empty()) return true;
    std::ifstream input(items_path_, std::ios::binary);
    if (!input) return false;
    char magic[4]{};
    uint32_t header[6]{};
    input.read(magic, 4);
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!input || std::memcmp(magic, "PLIT", 4) != 0) return false;
    baked_item_tile_ = header[2];
    baked_item_layers_ = header[3];
    input.seekg(static_cast<std::streamoff>((4 + sizeof(header) + 7) & ~size_t{7}));
    baked_item_pixels_.resize(static_cast<size_t>(baked_item_tile_) * baked_item_tile_ * 4 *
                              baked_item_layers_);
    input.read(reinterpret_cast<char*>(baked_item_pixels_.data()),
               static_cast<std::streamsize>(baked_item_pixels_.size()));
    if (!input) {
        baked_item_pixels_.clear();
        return false;
    }
    return true;
}

bool PackApplier::load_baked_gui() {
    if (!baked_gui_pixels_.empty()) return true;
    std::ifstream input(gui_path_, std::ios::binary);
    if (!input) return false;
    char magic[4]{};
    uint32_t header[7]{};
    input.read(magic, 4);
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!input || std::memcmp(magic, "PLGU", 4) != 0) return false;
    baked_gui_side_ = header[2];
    input.seekg(static_cast<std::streamoff>((4 + sizeof(header) + 7) & ~size_t{7}));
    baked_gui_pixels_.resize(static_cast<size_t>(baked_gui_side_) * baked_gui_side_ * 4);
    input.read(reinterpret_cast<char*>(baked_gui_pixels_.data()),
               static_cast<std::streamsize>(baked_gui_pixels_.size()));
    if (!input) {
        baked_gui_pixels_.clear();
        return false;
    }
    return true;
}

std::optional<Bitmap> PackApplier::read_texture(PackStack& packs, const std::string& name) {
    const auto bytes = packs.read(texture_asset_path(name));
    if (!bytes.has_value()) return std::nullopt;
    const auto image = read_png(*bytes);
    if (!image.has_value() || image->width == 0 || image->height == 0) return std::nullopt;
    return from_image(*image);
}

Bitmap PackApplier::baked_layer(uint32_t layer, uint32_t mip) const {
    const uint32_t side = atlas_.tile_size() >> mip;
    Bitmap out(side, side);
    const size_t stride = static_cast<size_t>(side) * side * 4;
    const size_t offset = atlas_.level_offset(mip) + stride * layer;
    const auto pixels = atlas_.pixels();
    if (offset + stride > pixels.size()) return out;
    std::copy_n(pixels.data() + offset, stride, out.rgba.data());
    return out;
}

// One block texture, as the baker would have put it in: the pack's picture,
// its own .mcmeta strategy, the chain against this atlas's tile size, and the
// animation frames behind it when there is room for them.
void PackApplier::patch_texture(PackStack& packs, PackTarget& target, const std::string& name,
                                const std::vector<uint32_t>& layers,
                                std::vector<LayerPatch>& patches, ApplyStats& stats) {
    std::optional<Bitmap> strip;
    {
        Stopwatch watch{std::chrono::steady_clock::now(), &stats.read_ms};
        strip = read_texture(packs, name);
    }
    if (!strip.has_value()) {
        ++stats.skipped;
        return;
    }

    MipStrategy strategy = MipStrategy::Auto;
    float bias = 0.0f;
    {
        Stopwatch watch{std::chrono::steady_clock::now(), &stats.read_ms};
        if (const auto meta = packs.read(texture_asset_path(name) + ".mcmeta")) {
            try {
                const json::Value root = json::parse(
                    std::string_view(reinterpret_cast<const char*>(meta->data()), meta->size()));
                if (const json::Value* section = root.find("texture"); section != nullptr) {
                    if (const json::Value* kind = section->find("mipmap_strategy")) {
                        strategy = mip_strategy_from_name(kind->string_or("auto"));
                    }
                    if (const json::Value* cutoff = section->find("alpha_cutoff_bias")) {
                        bias = static_cast<float>(cutoff->number_or(0.0));
                    }
                }
            } catch (const std::exception&) {
                // A pack with a broken .mcmeta still has a usable picture.
            }
        }
    }

    const uint32_t tile = target.atlas_tile();
    const uint32_t mips = target.atlas_mips();
    std::vector<std::vector<Bitmap>> per_level;
    {
        Stopwatch watch{std::chrono::steady_clock::now(), &stats.mip_ms};
        per_level = chain_for(to_power_of_two(*strip), strategy, bias, tile, mips);
    }
    if (per_level.empty() || per_level.front().empty()) {
        ++stats.skipped;
        return;
    }

    const auto push = [&](uint32_t layer, size_t frame) {
        LayerPatch patch;
        patch.layer = layer;
        patch.levels.reserve(mips);
        for (uint32_t mip = 0; mip < mips; ++mip) {
            const std::vector<Bitmap>& frames = per_level[mip];
            patch.levels.push_back(frames[std::min(frame, frames.size() - 1)].rgba);
        }
        patches.push_back(std::move(patch));
        touched_layers_.push_back(layer);
        ++stats.atlas_layers;
    };

    for (const uint32_t texture : layers) {
        push(texture, 0);
        // A texture the baked atlas animates keeps its frame layers. A pack
        // with fewer frames repeats its last one; one with more loses the
        // rest, since the atlas has no layers to put them in. docs/PACKS.md
        // says so.
        if (const auto animation = animation_layers_.find(texture);
            animation != animation_layers_.end()) {
            for (size_t frame = 1; frame < animation->second.size(); ++frame) {
                push(animation->second[frame], frame);
            }
        }
    }
    ++stats.block_textures;
}

// A flat icon composed again out of the pack's pictures, exactly the way
// `tools/bake_items.py flat_icon` composed it out of the jar's.
void PackApplier::patch_items(PackStack& packs, PackTarget& target, ApplyStats& stats) {
    const uint32_t tile = target.item_tile();
    if (tile == 0) return;
    std::vector<size_t> wanted;
    for (const auto& entry : items_by_texture_) {
        if (!packs.has(texture_asset_path(entry.first))) continue;
        wanted.insert(wanted.end(), entry.second.begin(), entry.second.end());
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    if (wanted.empty()) return;
    if (!load_baked_items()) {
        ++stats.skipped;
        return;
    }

    for (const size_t index : wanted) {
        const ItemRecipe& recipe = item_recipes_[index];
        const int32_t layer = target.item_layer_of(recipe.name);
        if (layer < 0) {
            ++stats.skipped;
            continue;
        }
        Bitmap icon(tile, tile);
        bool drawn = false;
        for (const ItemLayer& one : recipe.layers) {
            std::optional<Bitmap> picture;
            {
                Stopwatch watch{std::chrono::steady_clock::now(), &stats.read_ms};
                picture = read_texture(packs, one.texture);
                if (!picture.has_value()) {
                    // A layer the pack leaves alone still comes from the jar,
                    // and the jar is not ours to read at run time; the baked
                    // icon's own layer is gone once another one is replaced,
                    // so such an icon is left as it was.
                    picture.reset();
                }
            }
            if (!picture.has_value()) {
                drawn = false;
                break;
            }
            const Bitmap fitted = resample_nearest(first_frame(*picture), tile, tile);
            for (uint32_t y = 0; y < tile; ++y) {
                for (uint32_t x = 0; x < tile; ++x) {
                    const uint8_t* source = fitted.at(x, y);
                    uint8_t* into = icon.at(x, y);
                    const float alpha = static_cast<float>(source[3]) *
                                        static_cast<float>(one.tint[3]) / 255.0f / 255.0f;
                    for (int channel = 0; channel < 3; ++channel) {
                        const float tinted = static_cast<float>(source[channel]) *
                                             static_cast<float>(one.tint[static_cast<size_t>(
                                                 channel)]) / 255.0f;
                        const float mixed = tinted * alpha +
                                            static_cast<float>(into[channel]) * (1.0f - alpha);
                        into[channel] = static_cast<uint8_t>(std::clamp(mixed, 0.0f, 255.0f));
                    }
                    const float mixed = 255.0f * alpha +
                                        static_cast<float>(into[3]) * (1.0f - alpha);
                    into[3] = static_cast<uint8_t>(std::clamp(mixed, 0.0f, 255.0f));
                }
            }
            drawn = true;
        }
        if (!drawn) {
            ++stats.skipped;
            continue;
        }
        Stopwatch watch{std::chrono::steady_clock::now(), &stats.upload_ms};
        if (!target.set_item_icon(layer, icon)) {
            ++stats.skipped;
            continue;
        }
        touched_items_.push_back(layer);
        ++stats.item_icons;
    }
}

// One interface sprite, written back into the rectangle the baker packed it
// into. A pack sprite of another size is resampled to that rectangle: the
// sheet is packed once at bake time and re-packing it would move every uv.
void PackApplier::patch_sprites(PackStack& packs, PackTarget& target, ApplyStats& stats) {
    for (const std::string& asset : packs.list("assets/minecraft/textures/", ".png")) {
        const auto name = gui_sprite_name_of(asset);
        if (!name.has_value()) continue;
        const auto rect = target.sprite_rect(*name);
        if (rect[2] == 0 || rect[3] == 0) continue;
        if (!load_baked_gui()) {
            ++stats.skipped;
            return;
        }
        std::optional<std::vector<uint8_t>> bytes;
        {
            Stopwatch watch{std::chrono::steady_clock::now(), &stats.read_ms};
            bytes = packs.read(asset);
        }
        if (!bytes.has_value()) {
            ++stats.skipped;
            continue;
        }
        const auto image = read_png(*bytes);
        if (!image.has_value() || image->width == 0 || image->height == 0) {
            ++stats.skipped;
            continue;
        }
        const Bitmap fitted = resample_nearest(first_frame(from_image(*image)), rect[2], rect[3]);
        Stopwatch watch{std::chrono::steady_clock::now(), &stats.upload_ms};
        if (!target.set_sprite_pixels(*name, fitted)) {
            ++stats.skipped;
            continue;
        }
        touched_sprites_.push_back(*name);
        ++stats.gui_sprites;
    }
}

ApplyStats PackApplier::apply(PackStack& packs, PackTarget& target) {
    ApplyStats stats;
    const auto start = std::chrono::steady_clock::now();
    if (!open_) {
        stats.total_ms = ms_since(start);
        return stats;
    }

    std::vector<LayerPatch> patches;
    for (const std::string& asset : packs.list("assets/minecraft/textures/", ".png")) {
        const auto name = texture_name_of(asset);
        if (!name.has_value()) continue;
        const auto found = texture_layers_.find(*name);
        if (found == texture_layers_.end()) continue;
        patch_texture(packs, target, *name, found->second, patches, stats);
    }
    if (!patches.empty()) stats.upload_ms += target.patch_atlas(patches);

    patch_items(packs, target, stats);
    patch_sprites(packs, target, stats);
    stats.total_ms = ms_since(start);
    return stats;
}

ApplyStats PackApplier::revert(PackTarget& target) {
    ApplyStats stats;
    const auto start = std::chrono::steady_clock::now();
    const uint32_t mips = target.atlas_mips();

    std::sort(touched_layers_.begin(), touched_layers_.end());
    touched_layers_.erase(std::unique(touched_layers_.begin(), touched_layers_.end()),
                          touched_layers_.end());
    std::vector<LayerPatch> patches;
    patches.reserve(touched_layers_.size());
    for (const uint32_t layer : touched_layers_) {
        LayerPatch patch;
        patch.layer = layer;
        for (uint32_t mip = 0; mip < mips; ++mip) {
            patch.levels.push_back(baked_layer(layer, mip).rgba);
        }
        patches.push_back(std::move(patch));
        ++stats.atlas_layers;
    }
    if (!patches.empty()) stats.upload_ms += target.patch_atlas(patches);
    stats.block_textures = patches.size();
    touched_layers_.clear();

    if (!touched_items_.empty() && load_baked_items()) {
        const size_t stride = static_cast<size_t>(baked_item_tile_) * baked_item_tile_ * 4;
        for (const int32_t layer : touched_items_) {
            Bitmap icon(baked_item_tile_, baked_item_tile_);
            const size_t offset = stride * static_cast<size_t>(layer);
            if (offset + stride > baked_item_pixels_.size()) continue;
            std::copy_n(baked_item_pixels_.data() + offset, stride, icon.rgba.data());
            Stopwatch watch{std::chrono::steady_clock::now(), &stats.upload_ms};
            if (target.set_item_icon(layer, icon)) ++stats.item_icons;
        }
    }
    touched_items_.clear();

    if (!touched_sprites_.empty() && load_baked_gui()) {
        for (const std::string& name : touched_sprites_) {
            const auto rect = target.sprite_rect(name);
            if (rect[2] == 0 || rect[3] == 0) continue;
            Bitmap sprite(rect[2], rect[3]);
            for (uint32_t y = 0; y < rect[3]; ++y) {
                const size_t row = (static_cast<size_t>(rect[1] + y) * baked_gui_side_ + rect[0]) *
                                   4;
                if (row + static_cast<size_t>(rect[2]) * 4 > baked_gui_pixels_.size()) break;
                std::copy_n(baked_gui_pixels_.data() + row, static_cast<size_t>(rect[2]) * 4,
                            sprite.at(0, y));
            }
            Stopwatch watch{std::chrono::steady_clock::now(), &stats.upload_ms};
            if (target.set_sprite_pixels(name, sprite)) ++stats.gui_sprites;
        }
    }
    touched_sprites_.clear();

    stats.total_ms = ms_since(start);
    return stats;
}

} // namespace mcpp::packs
