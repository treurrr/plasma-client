#pragma once
#include <string_view>

namespace mcpp::gpu {
class UiRenderer;
}

namespace mcpp::ui {

class GuiGraphics;

// The turning cube behind the title flow. One lives as long as the client,
// the way the game keeps its own, so the turn carries over from one screen
// to the next.
class PanoramaRenderer {
public:
    static constexpr float kPitch = 10.0f;
    // `PanoramaRenderer.render`: one frame of the panorama, turned on by real
    // time when `spin` is set, `speed` being the panorama speed option.
    void render(GuiGraphics& gfx, int width, int height, bool spin, double speed);
    // `PanoramaRenderer.wrap`.
    static float wrap(float value, float limit) { return value > limit ? value - limit : value; }
    // How far it has turned, in degrees.
    float spin = 0.0f;
};

// The six faces from the player's own copy of the game's assets, where the
// launcher keeps them; false when they are not there.
bool load_panorama(gpu::UiRenderer& quads, std::string_view version);

} // namespace mcpp::ui
