#pragma once
#include "mcpp/gpu/text.hpp"
#include "mcpp/gpu/ui.hpp"

#include <array>
#include <string>
#include <vector>

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
    // Which category the player is looking at; -1 is all of them.
    int filter = -1;
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
    // Newest last. Only the last few are drawn.
    std::vector<std::string> chat;
    bool typing = false;
    std::string compose;
    // The nine the player can reach, as item ids with their stack sizes, and
    // which of them is in hand. An id of zero is an empty slot.
    std::array<int32_t, 9> hotbar{};
    std::array<int32_t, 9> hotbar_counts{};
    int32_t held = 0;
    std::string held_name;
    // What the bars above the hotbar show. Health is in half hearts and food
    // in half drumsticks, which is how the game counts them.
    float health = 20.0f;
    int32_t food = 20;
    int32_t armour = 0;
    float experience = 0.0f;
    int32_t level = 0;
    bool in_world = false;
    // Creative and spectator show no health, food, armour or experience at
    // all; the hotbar sits on the bottom of the screen on its own.
    bool survival_bars = true;
    // All 46 slots and the stack on the cursor, for when the inventory is open.
    bool inventory_open = false;
    std::array<int32_t, 46> slots{};
    std::array<int32_t, 46> slot_counts{};
    std::array<std::string, 46> slot_names{};
    int32_t cursor_item = 0;
    int32_t cursor_count = 0;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
};

void draw_hud(gpu::UiRenderer& quads, gpu::TextRenderer& text, float width, float height,
              const HudState& state);

// Where the pointer is and whether it was pressed, in window pixels.
struct Pointer {
    float x = 0.0f;
    float y = 0.0f;
    bool clicked = false;
};

// Which card the pointer is over, and which one it just switched. -1 for none.
struct MenuHit {
    int hovered = -1;
    int toggled = -1;
    int filter_clicked = -2;   // -1 means the All pill, -2 means none
};

// Drawn at a fixed 1600x900 design size and scaled to fit, so the proportions
// hold at any resolution instead of the text shrinking against the cards.
MenuHit draw_menu(gpu::UiRenderer& quads, gpu::TextRenderer& text, float width, float height,
                  const MenuState& state, const Pointer& pointer = {});

} // namespace mcpp::ui
