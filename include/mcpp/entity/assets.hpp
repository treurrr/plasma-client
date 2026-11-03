#pragma once
#include "mcpp/entity/model.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::entity {

struct Texture {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

// The default font's ascii sheet: sixteen by sixteen cells, and how far each
// character moves the pen, in sheet pixels.
struct Font {
    Texture sheet;
    uint32_t cell_width = 8;
    uint32_t cell_height = 8;
    uint32_t ascent = 7;
    uint8_t advance[256]{};
    float width(std::string_view text) const;
};

// What tools/bake_entities.py wrote: models, textures and the name-tag font.
class Assets {
public:
    bool open(const std::filesystem::path& path);
    // Models from a data file instead of the baked copy, for working on them.
    void use_models(std::string_view text);

    const ModelSet& models() const { return models_; }
    // By path under textures/entity/ without the extension, "player/wide/steve".
    const Texture* texture(std::string_view name) const;
    const Texture* shadow() const { return texture("$shadow"); }
    const Font& font() const { return font_; }
    size_t texture_count() const { return textures_.size(); }
    const std::map<std::string, Texture, std::less<>>& textures() const { return textures_; }

    static std::filesystem::path default_path(int32_t protocol);

private:
    ModelSet models_;
    std::map<std::string, Texture, std::less<>> textures_;
    Font font_;
};

} // namespace mcpp::entity
