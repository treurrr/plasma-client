#pragma once
#include "mcpp/jmath.hpp"
#include "mcpp/options.hpp"
#include "mcpp/session.hpp"
#include "mcpp/ui/gui_graphics.hpp"
#include "mcpp/ui/text_component.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

// The in-game hud, the game's Gui and the overlays it owns, drawn through
// GuiGraphics in gui units. It knows nothing of the session or the window:
// each frame it is handed a Player and a Frame, and the packets it reacts to
// arrive as calls (titles, chat, boss bars). Everything time-based runs on the
// game's tick, the way the game's own hud does.
namespace mcpp::ui::hud {

struct Item {
    int32_t id = 0;
    int32_t count = 0;
    // Ticks left of the bounce a stack does when it is picked up.
    int32_t pop_time = 0;
    // The durability bar: 0 to 13 pixels and its colour, or -1 for none.
    int32_t bar_width = -1;
    uint32_t bar_rgb = 0;
    bool empty() const { return id <= 0 || count <= 0; }
};

struct Effect {
    int32_t id = 0;   // the mob_effect registry's network id
    int32_t amplifier = 0;
    int32_t duration = 0;   // ticks, -1 for one that never runs out
    bool ambient = false;
    bool icon = true;
};

// The registry name of each effect by network id, its category and colour.
struct EffectInfo {
    const char* name;
    bool beneficial;
    uint32_t colour;
};
const EffectInfo* effect_info(int32_t id);

// What the hud reads of the camera player for one frame.
struct Player {
    // Survival, creative, adventure, spectator.
    int32_t game_mode = 0;
    bool hardcore = false;
    float health = 20.0f;
    float max_health = 20.0f;
    float absorption = 0.0f;
    // The client's own invulnerability countdown, set when health drops.
    int32_t invulnerable_time = 0;
    int32_t food = 20;
    float saturation = 5.0f;
    int32_t air = 300;
    int32_t max_air = 300;
    bool eye_in_water = false;
    bool fully_frozen = false;
    float percent_frozen = 0.0f;
    int32_t armour = 0;
    float experience = 0.0f;
    int32_t level = 0;
    std::vector<Effect> effects;

    std::array<Item, 9> hotbar{};
    Item offhand;
    int32_t selected = 0;
    // The selected stack's name, in its rarity colour, italic when renamed.
    Component selected_name;
    bool left_handed = false;

    // The attack charge, 0 to 1, and the full charge time in ticks.
    float attack_strength = 1.0f;
    float attack_delay = 5.0f;
    // The crosshair is on a living thing in reach, which lights the
    // indicator when the charge is full.
    bool aiming_at_living = false;
    bool first_person = true;
    // A spectator only sees the crosshair over something with a menu.
    bool spectator_crosshair = false;

    // A living vehicle shows its own hearts in place of the food row.
    bool vehicle_hearts = false;
    float vehicle_health = 0.0f;
    float vehicle_max_health = 0.0f;
    bool jumpable_vehicle = false;
    float jump_scale = 0.0f;
    int32_t jump_cooldown = 0;
    int32_t sleep_timer = 0;
    // The camera overlays; the world renderer's block fills these in.
    bool scoping = false;
    bool pumpkin = false;

    bool spectator() const { return game_mode == 3; }
    bool can_hurt() const { return game_mode == 0 || game_mode == 2; }
    bool has_experience() const { return game_mode == 0 || game_mode == 2; }
    bool has_effect(std::string_view name) const;
};

// One line of the player list.
struct TabEntry {
    std::string uuid;
    std::string name;
    Component display_name;
    bool has_display_name = false;
    int32_t game_mode = 0;
    int32_t latency = 0;
    int32_t list_order = 0;
    bool show_hat = true;
    // The face in the list; nothing until skins arrive.
    int32_t skin_layer = -1;
};

// What the hud needs of this frame beyond the player.
struct Frame {
    float partial = 0.0f;
    int64_t millis = 0;
    bool hide_gui = false;
    bool chat_focused = false;
    int mouse_x = -1;
    int mouse_y = -1;
    bool player_list_down = false;
    bool encrypted = false;
    std::string own_name;
    const Scoreboard* scoreboard = nullptr;
    const std::vector<TabEntry>* players = nullptr;
    // A screen is open that lists effects itself, so the corner does not.
    bool screen_shows_effects = false;
};

// Where things go, worked out on their own so they can be checked without
// drawing anything.
struct HeartLayout {
    int rows = 1;
    int row_height = 10;
    int max_hearts = 10;
    int absorption_hearts = 0;
};
HeartLayout heart_layout(float max_health, int health, int display_health, float absorption);
int chat_width(double option);
int chat_height(double option);

class Chat {
public:
    struct Line {
        int32_t added = 0;
        std::vector<Run> runs;
        int32_t tag = 0;
        bool end_of_entry = true;
    };

    void add(const Component& message, int32_t tag, int32_t gui_ticks, const Font& font,
             const Options& options, bool focused);
    void clear();
    void scroll(int lines, const Options& options, bool focused);
    void reset_scroll();
    void rescale(const Font& font, const Options& options, bool focused);
    void render(GuiGraphics& gfx, const Options& options, int32_t gui_ticks, bool focused,
                int mouse_x, int mouse_y) const;
    int lines_per_page(const Options& options, bool focused) const;
    size_t size() const { return trimmed_.size(); }
    const std::deque<Line>& lines() const { return trimmed_; }

    std::deque<std::string> recent;

private:
    struct Message {
        int32_t added = 0;
        Component content;
        int32_t tag = 0;
    };
    void display(const Message& message, const Font& font, const Options& options, bool focused);

    std::deque<Message> all_;
    std::deque<Line> trimmed_;
    int scroll_ = 0;
    bool new_since_scroll_ = false;
};

class Gui {
public:
    Gui();

    void tick(const Player& player, const Options& options);
    void render(GuiGraphics& gfx, const Player& player, const Options& options,
                const Frame& frame);

    void set_overlay_message(Component message, bool animate);
    void set_title(Component title);
    void set_subtitle(Component subtitle);
    void set_times(int fade_in, int stay, int fade_out);
    void clear_titles();
    void reset_title_times();
    void boss_event(const BossEvent& event, int64_t millis);
    void set_tab_list(Component header, Component footer, bool has_header, bool has_footer);
    void reset();

    Chat& chat() { return chat_; }
    int32_t ticks() const { return tick_count_; }

private:
    struct Boss {
        std::string uuid;
        Component name;
        float progress = 0.0f;
        float target = 0.0f;
        int64_t set_time = 0;
        int32_t colour = 0;
        int32_t overlay = 0;
        uint8_t flags = 0;
        float current(int64_t millis) const;
    };
    struct TabHealth {
        int last = 0;
        int displayed = 0;
        int64_t last_update = 0;
        int64_t blink_until = 0;
    };

    void render_camera_overlays(GuiGraphics& gfx, const Player& player, const Options& options);
    void render_crosshair(GuiGraphics& gfx, const Player& player, const Options& options);
    void render_hotbar_and_decorations(GuiGraphics& gfx, const Player& player,
                                       const Options& options, const Frame& frame);
    void render_item_hotbar(GuiGraphics& gfx, const Player& player, const Options& options,
                            const Frame& frame);
    void render_slot(GuiGraphics& gfx, int x, int y, const Item& item, float partial);
    void render_player_health(GuiGraphics& gfx, const Player& player, int64_t millis);
    void render_hearts(GuiGraphics& gfx, const Player& player, int x, int y, int row_height,
                       int regen, float max_health, int health, int display_health,
                       int absorption, bool blink);
    void render_food(GuiGraphics& gfx, const Player& player, int y, int right);
    void render_air(GuiGraphics& gfx, const Player& player, int vehicle_hearts, int y, int right);
    void render_vehicle_health(GuiGraphics& gfx, const Player& player);
    void render_selected_item_name(GuiGraphics& gfx, const Player& player);
    void render_effects(GuiGraphics& gfx, const Player& player, const Frame& frame);
    void render_bosses(GuiGraphics& gfx, int64_t millis);
    void render_sleep(GuiGraphics& gfx, const Player& player);
    void render_sidebar(GuiGraphics& gfx, const Options& options, const Frame& frame);
    void render_overlay_message(GuiGraphics& gfx, const Frame& frame);
    void render_title(GuiGraphics& gfx, const Frame& frame);
    void render_tab_list(GuiGraphics& gfx, const Options& options, const Frame& frame);

    const Options* options_ = nullptr;
    jmath::LegacyRandom random_{0};
    Chat chat_;
    int32_t tick_count_ = 0;

    std::optional<Component> overlay_;
    int32_t overlay_time_ = 0;
    bool animate_overlay_ = false;

    int32_t tool_highlight_timer_ = 0;
    int32_t last_highlight_id_ = 0;
    std::string last_highlight_name_;
    Component last_highlight_;

    std::optional<Component> title_;
    std::optional<Component> subtitle_;
    int32_t title_time_ = 0;
    int32_t title_fade_in_ = 10;
    int32_t title_stay_ = 70;
    int32_t title_fade_out_ = 20;

    int32_t last_health_ = 0;
    int32_t display_health_ = 0;
    int64_t last_health_time_ = 0;
    int64_t health_blink_time_ = 0;

    std::vector<Boss> bosses_;
    std::optional<Component> tab_header_;
    std::optional<Component> tab_footer_;
    bool tab_visible_ = false;
    std::map<std::string, TabHealth> tab_health_;
};

// A score as the place shows it: the objective's format, the score's own
// override, or the place's default style.
Component format_score(const Scoreboard::Score* score, const Scoreboard::Objective& objective,
                       uint32_t default_colour);
// A name with its team's prefix, suffix and colour.
Component team_name(const Scoreboard::Team* team, Component name);

} // namespace mcpp::ui::hud
