#pragma once
#include "mcpp/gpu/text.hpp"
#include "mcpp/gpu/ui.hpp"

#include <string>

namespace mcpp::ui {

// What the menu shows that it cannot work out for itself.
struct MenuState {
    std::string server = "singleplayer";
    int ping_ms = 0;
    int fps = 0;
    int cps_left = 0;
    int cps_right = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    std::string version = "1.21.11";
    std::string client_version = "0.1.0";
};

// What the heads-up display shows while playing. Every number here is
// measured by the client, never estimated.
struct HudState {
    int fps = 0;
    double gpu_ms = 0.0;
    double cpu_ms = 0.0;
    int ping_ms = 0;
    int cps_left = 0;
    int cps_right = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    bool forward = false;
    bool left = false;
    bool back = false;
    bool right = false;
    bool jump = false;
    size_t sections = 0;
    size_t entities = 0;
};

void draw_hud(gpu::UiRenderer& quads, gpu::TextRenderer& text, float width, float height,
              const HudState& state);

// Drawn at a fixed 1600x900 design size and scaled to fit, so the proportions
// hold at any resolution instead of the text shrinking against the cards.
void draw_menu(gpu::UiRenderer& quads, gpu::TextRenderer& text, float width, float height,
               const MenuState& state);

} // namespace mcpp::ui
