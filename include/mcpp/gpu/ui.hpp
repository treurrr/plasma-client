#pragma once
#include "mcpp/gpu/device.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

namespace mcpp::gpu {

// One rounded rectangle. Everything the interface is built from is one of
// these: cards, pills, bars, dividers, the glow behind the accent.
struct Quad {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float radius = 0.0f;
    float border = 0.0f;
    float glow = 0.0f;
    uint32_t fill_top = 0;
    uint32_t fill_bottom = 0;
    uint32_t border_rgba = 0;
    uint32_t glow_rgba = 0;
    // What to draw in the rectangle: -1 a rounded shape, -2 the sprite at `uv`
    // in the interface atlas, anything else the inventory icon for that item.
    int32_t layer = -1;
    float uv[4]{};
};

// Shapes are signed distance fields in the fragment shader rather than
// geometry, so a corner radius costs nothing and every edge is antialiased at
// whatever resolution the window happens to be.
class UiRenderer {
public:
    // `items` is the baked inventory icons; without them the renderer still
    // draws every shape, it just has no icons to draw.
    UiRenderer(Device& device, VkFormat colour_format,
               const std::filesystem::path& items = {},
               const std::filesystem::path& gui = {});
    ~UiRenderer();

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    void begin();
    void add(const Quad& quad);
    void panel(float x, float y, float width, float height, float radius, uint32_t fill,
               uint32_t border_rgba = 0, float border = 0.0f);
    // Draws item `id` filling the square, tinted by `rgba` (white leaves it be).
    void icon(int32_t id, float x, float y, float size, uint32_t rgba = 0xFFFFFFFFu);
    bool has_icons() const { return item_layers_ > 1; }
    // "diamond_sword", or empty for an id this build has no name for.
    std::string_view item_name(int32_t id) const;
    // Armour points this item is worth, from the game's own item report.
    int32_t item_armour(int32_t id) const;

    // One of the game's own interface sprites, stretched over the rectangle.
    void sprite(std::string_view name, float x, float y, float width, float height,
                uint32_t rgba = 0xFFFFFFFFu);
    // Part of a sprite, cut out rather than squashed: a bar that is half full
    // is the left half of its picture, not the whole picture at half width.
    void sprite_part(std::string_view name, float x, float y, float width, float height,
                     float fraction_x, float fraction_y, uint32_t rgba = 0xFFFFFFFFu);
    // A sprite that grows the way the game grows it: corners as drawn, edges
    // and middle repeated - or the middle stretched where the sprite says so -
    // at `scale` screen pixels per game pixel. A sprite with no slicing of its
    // own is simply stretched.
    void nine_slice(std::string_view name, float x, float y, float width, float height,
                    float scale, uint32_t rgba = 0xFFFFFFFFu);
    // Its natural size in game pixels, or {0, 0} when this build has no such
    // sprite, which is also what a caller should check before leaning on one.
    std::pair<float, float> sprite_size(std::string_view name) const;
    bool has_sprites() const { return !sprites_.empty(); }

    // The game's own font. `scale` is how many screen pixels one font pixel
    // takes, which is what the game calls the gui scale. The shadow is the
    // game's: the same text one pixel down and right, at a quarter brightness.
    float text(std::string_view utf8, float x, float y, float scale, uint32_t rgba,
               bool shadow = true);
    float text_width(std::string_view utf8, float scale) const;
    float line_height(float scale) const { return 9.0f * scale; }
    bool has_font() const { return !glyphs_.empty(); }

    static std::filesystem::path default_items_path(int32_t protocol);
    static std::filesystem::path default_gui_path(int32_t protocol);
    void record(VkCommandBuffer command, uint32_t width, uint32_t height);

private:
    struct Sprite {
        float u, v, du, dv;
        float width, height;
    };

    struct Slicing {
        float left, top, right, bottom;
        bool stretch_inner;
    };
    void region(const Sprite& sprite, float sx, float sy, float sw, float sh, float dx, float dy,
                float dw, float dh, uint32_t rgba);
    void tile(const Sprite& sprite, float sx, float sy, float sw, float sh, float dx, float dy,
              float dw, float dh, float scale, uint32_t rgba);

    struct Glyph {
        Sprite sprite;
        float advance = 0.0f;
        float draw_width = 0.0f;
        float draw_height = 0.0f;
        float ascent = 0.0f;
    };

    void upload_texture(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
                        uint32_t layers, VkImage& image, VkDeviceMemory& memory,
                        VkImageView& view, VkImageViewType view_type);
    void create_atlas(const std::filesystem::path& items, const std::filesystem::path& gui);
    void create_pipeline(VkFormat colour_format);
    void destroy();

    Device& device_;
    bool linearise_ = false;
    uint32_t count_ = 0;
    static constexpr uint32_t kCapacity = 4096;
    Quad* mapped_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    uint32_t item_layers_ = 0;
    std::vector<std::string> item_names_;
    std::vector<uint8_t> item_armour_;
    std::map<std::string, Sprite, std::less<>> sprites_;
    std::unordered_map<uint32_t, Glyph> glyphs_;
    std::map<std::string, Slicing, std::less<>> slicing_;
    VkImage gui_image_ = VK_NULL_HANDLE;
    VkDeviceMemory gui_memory_ = VK_NULL_HANDLE;
    VkImageView gui_view_ = VK_NULL_HANDLE;
    VkImage item_image_ = VK_NULL_HANDLE;
    VkDeviceMemory item_memory_ = VK_NULL_HANDLE;
    VkImageView item_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
};

} // namespace mcpp::gpu
