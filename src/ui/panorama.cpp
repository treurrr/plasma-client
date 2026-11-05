#include "mcpp/ui/panorama.hpp"

#include "mcpp/assets/object_store.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/ui/gui_graphics.hpp"

#include <format>

namespace mcpp::ui {

// `PanoramaRenderer.render`, `CubeMap.render`: the spin advanced by real time, the cube drawn turned by it and tilted, then the overlay.
void PanoramaRenderer::render(GuiGraphics& gfx, int width, int height, bool turn, double speed) {
    if (turn) {
        const float delta = gfx.realtime_delta_ticks;
        const auto step = static_cast<float>(static_cast<double>(delta) * speed);
        spin = wrap(spin + step * 0.1f, 360.0f);
    }
    if (gfx.quads.has_panorama()) {
        const int pw = gfx.pixel_width > 0 ? gfx.pixel_width : width * gfx.scale;
        const int ph = gfx.pixel_height > 0 ? gfx.pixel_height : height * gfx.scale;
        gfx.quads.panorama(0.0f, 0.0f, static_cast<float>(pw), static_cast<float>(ph), -spin,
                           kPitch);
    }
    gfx.blit("gui/title/background/panorama_overlay", 0, 0, 0.0f, 0.0f, width, height, 16, 128,
             16, 128);
}

// `CubeMapTexture.loadContents`: the six faces, in the order the cube's layers take them.
bool load_panorama(gpu::UiRenderer& quads, std::string_view version) {
    const auto store = assets::ObjectStore::for_version(version);
    if (!store) return false;
    static constexpr int kSuffixes[6] = {1, 3, 5, 4, 0, 2};
    std::vector<uint8_t> faces[6];
    uint32_t side = 0;
    for (int i = 0; i < 6; ++i) {
        const auto bytes = store->read(std::format(
            "minecraft/textures/gui/title/background/panorama_{}.png", kSuffixes[i]));
        if (!bytes) return false;
        auto image = read_png(*bytes);
        if (!image || image->width != image->height) return false;
        if (i == 0) side = image->width;
        if (image->width != side) return false;
        faces[i] = std::move(image->rgba);
    }
    quads.set_panorama(faces, side);
    return quads.has_panorama();
}

} // namespace mcpp::ui
