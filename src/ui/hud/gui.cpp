#include "mcpp/ui/hud/hud.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace mcpp::ui::hud {
namespace {

constexpr EffectInfo kEffects[] = {
    {"speed", true, 3402751},           {"slowness", false, 9154528},
    {"haste", true, 14270531},          {"mining_fatigue", false, 4866583},
    {"strength", true, 16762624},       {"instant_health", true, 16262179},
    {"instant_damage", false, 11101546}, {"jump_boost", true, 16646020},
    {"nausea", false, 5578058},         {"regeneration", true, 13458603},
    {"resistance", true, 9520880},      {"fire_resistance", true, 16750848},
    {"water_breathing", true, 10017472}, {"invisibility", true, 16185078},
    {"blindness", false, 2039587},      {"night_vision", true, 12779366},
    {"hunger", false, 5797459},         {"weakness", false, 4738376},
    {"poison", false, 8889187},         {"wither", false, 7561558},
    {"health_boost", true, 16284963},   {"absorption", true, 2445989},
    {"saturation", true, 16262179},     {"glowing", false, 9740385},
    {"levitation", false, 13565951},    {"luck", true, 5882118},
    {"unluck", false, 12624973},        {"slow_falling", true, 15978425},
    {"conduit_power", true, 1950417},   {"dolphins_grace", true, 8954814},
    {"bad_omen", false, 745784},        {"hero_of_the_village", true, 4521796},
    {"darkness", false, 2696993},       {"trial_omen", false, 1484454},
    {"raid_omen", false, 14565464},     {"wind_charged", false, 12438015},
    {"weaving", false, 7891290},        {"oozing", false, 10092451},
    {"infested", false, 9214860},       {"breath_of_the_nautilus", true, 65518},
};

int ceil_int(float value) {
    const int truncated = static_cast<int>(value);
    return value > static_cast<float>(truncated) ? truncated + 1 : truncated;
}

int ceil_int(double value) {
    const int truncated = static_cast<int>(value);
    return value > static_cast<double>(truncated) ? truncated + 1 : truncated;
}

int floor_int(float value) {
    const int truncated = static_cast<int>(value);
    return value < static_cast<float>(truncated) ? truncated - 1 : truncated;
}

int channel(float value) { return floor_int(value * 255.0f); }

uint32_t white(float alpha) { return static_cast<uint32_t>(channel(alpha)) << 24 | 0xFFFFFFu; }
uint32_t white(int alpha) { return static_cast<uint32_t>(alpha) << 24 | 0xFFFFFFu; }
uint32_t black(float alpha) { return static_cast<uint32_t>(channel(alpha)) << 24; }

int lerp_discrete(float delta, int from, int to) {
    return from + floor_int(delta * static_cast<float>(to - from - 1)) + (delta > 0.0f ? 1 : 0);
}

uint32_t hsv_to_argb(float h, float s, float v, int alpha) {
    const int sector = static_cast<int>(h * 6.0f) % 6;
    const float f = h * 6.0f - static_cast<float>(sector);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    switch (sector) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    const auto c = [](float x) {
        return static_cast<uint32_t>(std::clamp(static_cast<int>(x * 255.0f), 0, 255));
    };
    return static_cast<uint32_t>(alpha) << 24 | c(r) << 16 | c(g) << 8 | c(b);
}

// Options::getBackgroundColor(float): the given opacity, or the text
// background's when the player wants backgrounds everywhere.
uint32_t background_colour(const Options& options, float opacity) {
    const float used = options.get_bool("backgroundForChatOnly")
                           ? opacity
                           : static_cast<float>(options.get_double("textBackgroundOpacity"));
    return black(used);
}

uint32_t argb_multiply(uint32_t a, uint32_t b) {
    if (a == 0xFFFFFFFFu) return b;
    if (b == 0xFFFFFFFFu) return a;
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        out |= (((a >> shift) & 0xFFu) * ((b >> shift) & 0xFFu) / 255u) << shift;
    }
    return out;
}

void draw_with_backdrop(GuiGraphics& gfx, const Options& options, const std::vector<Run>& runs,
                        int x, int y, int width, uint32_t argb) {
    const uint32_t background = background_colour(options, 0.0f);
    if (background != 0) {
        gfx.fill(x - 2, y - 2, x + width + 2, y + 9 + 2, argb_multiply(background, argb));
    }
    gfx.draw_string(runs, x, y, argb, true);
}

enum class HeartType { Container, Normal, Poisoned, Withered, Absorbing, Frozen };

std::string_view heart_sprite(HeartType type, bool hardcore, bool half, bool blinking) {
    static constexpr std::string_view kNames[6][8] = {
        {"hud/heart/container", "hud/heart/container_blinking", "hud/heart/container",
         "hud/heart/container_blinking", "hud/heart/container_hardcore",
         "hud/heart/container_hardcore_blinking", "hud/heart/container_hardcore",
         "hud/heart/container_hardcore_blinking"},
        {"hud/heart/full", "hud/heart/full_blinking", "hud/heart/half", "hud/heart/half_blinking",
         "hud/heart/hardcore_full", "hud/heart/hardcore_full_blinking", "hud/heart/hardcore_half",
         "hud/heart/hardcore_half_blinking"},
        {"hud/heart/poisoned_full", "hud/heart/poisoned_full_blinking", "hud/heart/poisoned_half",
         "hud/heart/poisoned_half_blinking", "hud/heart/poisoned_hardcore_full",
         "hud/heart/poisoned_hardcore_full_blinking", "hud/heart/poisoned_hardcore_half",
         "hud/heart/poisoned_hardcore_half_blinking"},
        {"hud/heart/withered_full", "hud/heart/withered_full_blinking", "hud/heart/withered_half",
         "hud/heart/withered_half_blinking", "hud/heart/withered_hardcore_full",
         "hud/heart/withered_hardcore_full_blinking", "hud/heart/withered_hardcore_half",
         "hud/heart/withered_hardcore_half_blinking"},
        {"hud/heart/absorbing_full", "hud/heart/absorbing_full_blinking",
         "hud/heart/absorbing_half", "hud/heart/absorbing_half_blinking",
         "hud/heart/absorbing_hardcore_full", "hud/heart/absorbing_hardcore_full_blinking",
         "hud/heart/absorbing_hardcore_half", "hud/heart/absorbing_hardcore_half_blinking"},
        {"hud/heart/frozen_full", "hud/heart/frozen_full_blinking", "hud/heart/frozen_half",
         "hud/heart/frozen_half_blinking", "hud/heart/frozen_hardcore_full",
         "hud/heart/frozen_hardcore_full_blinking", "hud/heart/frozen_hardcore_half",
         "hud/heart/frozen_hardcore_half_blinking"},
    };
    return kNames[static_cast<int>(type)][(hardcore ? 4 : 0) + (half ? 2 : 0) + (blinking ? 1 : 0)];
}

HeartType heart_type_for(const Player& player) {
    if (player.has_effect("poison")) return HeartType::Poisoned;
    if (player.has_effect("wither")) return HeartType::Withered;
    if (player.fully_frozen) return HeartType::Frozen;
    return HeartType::Normal;
}

int air_bubble(int air, int max_air, int offset) {
    return ceil_int(static_cast<float>((air + offset) * 10) / static_cast<float>(max_air));
}

// The game's order of effects: running out soonest last, beacons apart.
bool effect_before(const Effect& a, const Effect& b) {
    const auto colour = [](const Effect& e) {
        const auto* info = effect_info(e.id);
        return info != nullptr ? static_cast<int64_t>(info->colour) : 0;
    };
    const auto compare = [&](const Effect& x, const Effect& y) -> int {
        if ((x.duration <= 32147 || y.duration <= 32147) && (!x.ambient || !y.ambient)) {
            if (x.ambient != y.ambient) return x.ambient ? 1 : -1;
            const bool xi = x.duration == -1;
            const bool yi = y.duration == -1;
            if (xi != yi) return xi ? 1 : -1;
            if (x.duration != y.duration) return x.duration < y.duration ? -1 : 1;
            const int64_t cx = colour(x);
            const int64_t cy = colour(y);
            return cx == cy ? 0 : (cx < cy ? -1 : 1);
        }
        if (x.ambient != y.ambient) return x.ambient ? 1 : -1;
        const int64_t cx = colour(x);
        const int64_t cy = colour(y);
        return cx == cy ? 0 : (cx < cy ? -1 : 1);
    };
    return compare(a, b) > 0;
}

} // namespace

const EffectInfo* effect_info(int32_t id) {
    if (id < 0 || static_cast<size_t>(id) >= std::size(kEffects)) return nullptr;
    return &kEffects[id];
}

bool Player::has_effect(std::string_view name) const {
    for (const auto& effect : effects) {
        const auto* info = effect_info(effect.id);
        if (info != nullptr && name == info->name) return true;
    }
    return false;
}

HeartLayout heart_layout(float max_health, int health, int display_health, float absorption) {
    HeartLayout layout;
    const float max = std::max(max_health, static_cast<float>(std::max(display_health, health)));
    const int absorbing = ceil_int(absorption);
    layout.rows = ceil_int((max + static_cast<float>(absorbing)) / 2.0f / 10.0f);
    layout.row_height = std::max(10 - (layout.rows - 2), 3);
    layout.max_hearts = ceil_int(static_cast<double>(max) / 2.0);
    layout.absorption_hearts = ceil_int(static_cast<double>(absorbing) / 2.0);
    return layout;
}

Gui::Gui() { reset_title_times(); }

void Gui::reset_title_times() {
    title_fade_in_ = 10;
    title_stay_ = 70;
    title_fade_out_ = 20;
}

void Gui::set_overlay_message(Component message, bool animate) {
    overlay_ = std::move(message);
    overlay_time_ = 60;
    animate_overlay_ = animate;
}

void Gui::set_title(Component title) {
    title_ = std::move(title);
    title_time_ = title_fade_in_ + title_stay_ + title_fade_out_;
}

void Gui::set_subtitle(Component subtitle) { subtitle_ = std::move(subtitle); }

void Gui::set_times(int fade_in, int stay, int fade_out) {
    if (fade_in >= 0) title_fade_in_ = fade_in;
    if (stay >= 0) title_stay_ = stay;
    if (fade_out >= 0) title_fade_out_ = fade_out;
    if (title_time_ > 0) title_time_ = title_fade_in_ + title_stay_ + title_fade_out_;
}

void Gui::clear_titles() {
    title_.reset();
    subtitle_.reset();
    title_time_ = 0;
}

void Gui::set_tab_list(Component header, Component footer, bool has_header, bool has_footer) {
    tab_header_.reset();
    tab_footer_.reset();
    if (has_header) tab_header_ = std::move(header);
    if (has_footer) tab_footer_ = std::move(footer);
}

void Gui::reset() {
    bosses_.clear();
    tab_header_.reset();
    tab_footer_.reset();
    chat_.clear();
    clear_titles();
    reset_title_times();
}

float Gui::Boss::current(int64_t millis) const {
    const float t = std::clamp(static_cast<float>(millis - set_time) / 100.0f, 0.0f, 1.0f);
    return progress + t * (target - progress);
}

void Gui::boss_event(const BossEvent& event, int64_t millis) {
    auto found = std::find_if(bosses_.begin(), bosses_.end(),
                              [&](const Boss& boss) { return boss.uuid == event.uuid; });
    switch (event.op) {
    case BossEvent::Op::Add: {
        Boss boss;
        boss.uuid = event.uuid;
        boss.name = from_nbt(event.name);
        boss.progress = boss.target = event.progress;
        boss.set_time = millis;
        boss.colour = event.colour;
        boss.overlay = event.overlay;
        boss.flags = event.flags;
        if (found != bosses_.end()) {
            *found = std::move(boss);
        } else {
            bosses_.push_back(std::move(boss));
        }
        return;
    }
    case BossEvent::Op::Remove:
        if (found != bosses_.end()) bosses_.erase(found);
        return;
    default:
        break;
    }
    if (found == bosses_.end()) return;
    switch (event.op) {
    case BossEvent::Op::Progress:
        found->progress = found->current(millis);
        found->target = event.progress;
        found->set_time = millis;
        break;
    case BossEvent::Op::Name: found->name = from_nbt(event.name); break;
    case BossEvent::Op::Style:
        found->colour = event.colour;
        found->overlay = event.overlay;
        break;
    case BossEvent::Op::Flags: found->flags = event.flags; break;
    default: break;
    }
}

void Gui::tick(const Player& player, const Options& options) {
    if (overlay_time_ > 0) --overlay_time_;
    if (title_time_ > 0) {
        --title_time_;
        if (title_time_ <= 0) {
            title_.reset();
            subtitle_.reset();
        }
    }
    ++tick_count_;

    const Item& held = player.hotbar[static_cast<size_t>(std::clamp(player.selected, 0, 8))];
    const std::string name = held.empty() ? std::string{} : plain(player.selected_name);
    if (held.empty()) {
        tool_highlight_timer_ = 0;
    } else if (last_highlight_id_ <= 0 || held.id != last_highlight_id_ ||
               name != last_highlight_name_) {
        tool_highlight_timer_ =
            static_cast<int32_t>(40.0 * options.get_double("notificationDisplayTime"));
    } else if (tool_highlight_timer_ > 0) {
        --tool_highlight_timer_;
    }
    last_highlight_id_ = held.empty() ? 0 : held.id;
    last_highlight_name_ = name;
    last_highlight_ = player.selected_name;
}

void Gui::render(GuiGraphics& gfx, const Player& player, const Options& options,
                 const Frame& frame) {
    options_ = &options;
    if (!frame.hide_gui) {
        render_camera_overlays(gfx, player, options);
        render_crosshair(gfx, player, options);
        render_hotbar_and_decorations(gfx, player, options, frame);
        render_effects(gfx, player, frame);
        render_bosses(gfx, frame.millis);
    }
    render_sleep(gfx, player);
    if (!frame.hide_gui) {
        render_sidebar(gfx, options, frame);
        render_overlay_message(gfx, frame);
        render_title(gfx, frame);
        if (!frame.chat_focused) {
            chat_.render(gfx, options, tick_count_, false, frame.mouse_x, frame.mouse_y);
        }
        render_tab_list(gfx, options, frame);
    }
}

void Gui::render_camera_overlays(GuiGraphics& gfx, const Player& player, const Options&) {
    // The vignette, the pumpkin, the scope and the powder snow are whole
    // textures from textures/misc; they draw once the atlas carries them.
    if (player.first_person && player.pumpkin) {
        gfx.blit("misc/pumpkinblur", 0, 0, 0.0f, 0.0f, gfx.width, gfx.height, gfx.width,
                 gfx.height);
    }
    if (player.percent_frozen > 0.0f) {
        gfx.blit("misc/powder_snow_outline", 0, 0, 0.0f, 0.0f, gfx.width, gfx.height, gfx.width,
                 gfx.height, white(player.percent_frozen));
    }
}

void Gui::render_crosshair(GuiGraphics& gfx, const Player& player, const Options& options) {
    if (!player.first_person) return;
    if (player.spectator() && !player.spectator_crosshair) return;
    gfx.blit_sprite("hud/crosshair", (gfx.width - 15) / 2, (gfx.height - 15) / 2, 15, 15);
    if (options.get_int("attackIndicator") != 1) return;
    const float strength = player.attack_strength;
    const bool full = player.aiming_at_living && strength >= 1.0f && player.attack_delay > 5.0f;
    const int y = gfx.height / 2 - 7 + 16;
    const int x = gfx.width / 2 - 8;
    if (full) {
        gfx.blit_sprite("hud/crosshair_attack_indicator_full", x, y, 16, 16);
    } else if (strength < 1.0f) {
        const int filled = static_cast<int>(strength * 17.0f);
        gfx.blit_sprite("hud/crosshair_attack_indicator_background", x, y, 16, 4);
        gfx.blit_sprite("hud/crosshair_attack_indicator_progress", 16, 4, 0, 0, x, y, filled, 4);
    }
}

void Gui::render_hotbar_and_decorations(GuiGraphics& gfx, const Player& player,
                                        const Options& options, const Frame& frame) {
    if (!player.spectator()) render_item_hotbar(gfx, player, options, frame);
    if (player.can_hurt()) render_player_health(gfx, player, frame.millis);
    render_vehicle_health(gfx, player);

    const int left = (gfx.width - 182) / 2;
    const int top = gfx.height - 24 - 5;
    if (player.jumpable_vehicle) {
        gfx.blit_sprite("hud/jump_bar_background", left, top, 182, 5);
        if (player.jump_cooldown > 0) {
            gfx.blit_sprite("hud/jump_bar_cooldown", left, top, 182, 5);
        } else {
            const int filled = lerp_discrete(player.jump_scale, 0, 182);
            if (filled > 0) {
                gfx.blit_sprite("hud/jump_bar_progress", 182, 5, 0, 0, left, top, filled, 5);
            }
        }
    } else if (player.has_experience()) {
        const int filled = static_cast<int>(player.experience * 183.0f);
        gfx.blit_sprite("hud/experience_bar_background", left, top, 182, 5);
        if (filled > 0) {
            gfx.blit_sprite("hud/experience_bar_progress", 182, 5, 0, 0, left, top, filled, 5);
        }
    }
    if (player.has_experience() && player.level > 0) {
        const auto runs = gfx.runs(translatable("gui.experience.level",
                                                {literal(std::to_string(player.level))}));
        const int x = (gfx.width - gfx.font.width(runs)) / 2;
        const int y = gfx.height - 24 - 9 - 2;
        gfx.draw_string(runs, x + 1, y, 0xFF000000u, false);
        gfx.draw_string(runs, x - 1, y, 0xFF000000u, false);
        gfx.draw_string(runs, x, y + 1, 0xFF000000u, false);
        gfx.draw_string(runs, x, y - 1, 0xFF000000u, false);
        gfx.draw_string(runs, x, y, 0xFF80FF20u, false);
    }
    if (!player.spectator()) render_selected_item_name(gfx, player);
    (void)options;
}

void Gui::render_item_hotbar(GuiGraphics& gfx, const Player& player, const Options& options,
                             const Frame& frame) {
    const int centre = gfx.width / 2;
    const bool offhand_left = !player.left_handed;
    gfx.blit_sprite("hud/hotbar", centre - 91, gfx.height - 22, 182, 22);
    gfx.blit_sprite("hud/hotbar_selection", centre - 91 - 1 + player.selected * 20,
                    gfx.height - 22 - 1, 24, 23);
    if (!player.offhand.empty()) {
        if (offhand_left) {
            gfx.blit_sprite("hud/hotbar_offhand_left", centre - 91 - 29, gfx.height - 23, 29, 24);
        } else {
            gfx.blit_sprite("hud/hotbar_offhand_right", centre + 91, gfx.height - 23, 29, 24);
        }
    }
    for (int i = 0; i < 9; ++i) {
        render_slot(gfx, centre - 90 + i * 20 + 2, gfx.height - 16 - 3,
                    player.hotbar[static_cast<size_t>(i)], frame.partial);
    }
    if (!player.offhand.empty()) {
        const int y = gfx.height - 16 - 3;
        render_slot(gfx, offhand_left ? centre - 91 - 26 : centre + 91 + 10, y, player.offhand,
                    frame.partial);
    }
    if (options.get_int("attackIndicator") == 2) {
        const float strength = player.attack_strength;
        if (strength < 1.0f) {
            const int y = gfx.height - 20;
            const int x = offhand_left ? centre + 91 + 6 : centre - 91 - 22;
            const int filled = static_cast<int>(strength * 19.0f);
            gfx.blit_sprite("hud/hotbar_attack_indicator_background", x, y, 18, 18);
            gfx.blit_sprite("hud/hotbar_attack_indicator_progress", 18, 18, 0, 18 - filled, x,
                            y + 18 - filled, 18, filled);
        }
    }
}

void Gui::render_slot(GuiGraphics& gfx, int x, int y, const Item& item, float partial) {
    if (item.empty()) return;
    const float pop = static_cast<float>(item.pop_time) - partial;
    if (pop > 0.0f) {
        // Squashed about the slot's foot: narrower and taller, settling back.
        const float k = 1.0f + pop / 5.0f;
        const float sx = 1.0f / k;
        const float sy = (k + 1.0f) / 2.0f;
        const float ox = static_cast<float>(x + 8);
        const float oy = static_cast<float>(y + 12);
        const float s = static_cast<float>(gfx.scale);
        gpu::Quad quad;
        quad.x = (ox + (static_cast<float>(x) - ox) * sx) * s;
        quad.y = (oy + (static_cast<float>(y) - oy) * sy) * s;
        quad.width = 16.0f * sx * s;
        quad.height = 16.0f * sy * s;
        quad.fill_top = quad.fill_bottom = 0xFFFFFFFFu;
        quad.layer = item.id;
        gfx.quads.add(quad);
    } else {
        gfx.render_item(item.id, x, y);
    }
    gfx.render_item_decorations(item.id, item.count, x, y);
    if (item.bar_width >= 0) gfx.render_item_bar(x, y, item.bar_width, item.bar_rgb);
}

void Gui::render_player_health(GuiGraphics& gfx, const Player& player, int64_t millis) {
    const int health = ceil_int(player.health);
    const bool blink = health_blink_time_ > tick_count_ &&
                       (health_blink_time_ - tick_count_) / 3 % 2 == 1;
    if (health < last_health_ && player.invulnerable_time > 0) {
        last_health_time_ = millis;
        health_blink_time_ = tick_count_ + 20;
    } else if (health > last_health_ && player.invulnerable_time > 0) {
        last_health_time_ = millis;
        health_blink_time_ = tick_count_ + 10;
    }
    if (millis - last_health_time_ > 1000) {
        display_health_ = health;
        last_health_time_ = millis;
    }
    last_health_ = health;
    const int display = display_health_;
    random_.set_seed(static_cast<int64_t>(static_cast<int32_t>(
        static_cast<uint32_t>(tick_count_) * 312871u)));

    const int left = gfx.width / 2 - 91;
    const int right = gfx.width / 2 + 91;
    const int y = gfx.height - 39;
    const float max_health =
        std::max(player.max_health, static_cast<float>(std::max(display, health)));
    const int absorption = ceil_int(player.absorption);
    const int rows = ceil_int((max_health + static_cast<float>(absorption)) / 2.0f / 10.0f);
    const int row_height = std::max(10 - (rows - 2), 3);
    int air_y = y - 10;
    int regen = -1;
    if (player.has_effect("regeneration")) regen = tick_count_ % ceil_int(max_health + 5.0f);

    if (player.armour > 0) {
        const int armour_y = y - (rows - 1) * row_height - 10;
        for (int i = 0; i < 10; ++i) {
            const int x = left + i * 8;
            if (i * 2 + 1 < player.armour) gfx.blit_sprite("hud/armor_full", x, armour_y, 9, 9);
            if (i * 2 + 1 == player.armour) gfx.blit_sprite("hud/armor_half", x, armour_y, 9, 9);
            if (i * 2 + 1 > player.armour) gfx.blit_sprite("hud/armor_empty", x, armour_y, 9, 9);
        }
    }
    render_hearts(gfx, player, left, y, row_height, regen, max_health, health, display, absorption,
                  blink);
    int vehicle_hearts = 0;
    if (player.vehicle_hearts) {
        vehicle_hearts = std::min(static_cast<int>(player.vehicle_max_health + 0.5f) / 2, 30);
    }
    if (vehicle_hearts == 0) {
        render_food(gfx, player, y, right);
        air_y -= 10;
    }
    render_air(gfx, player, vehicle_hearts, air_y, right);
}

void Gui::render_hearts(GuiGraphics& gfx, const Player& player, int x, int y, int row_height,
                        int regen, float max_health, int health, int display_health,
                        int absorption, bool blink) {
    const HeartType type = heart_type_for(player);
    const bool hardcore = player.hardcore;
    const int max_hearts = ceil_int(static_cast<double>(max_health) / 2.0);
    const int absorption_hearts = ceil_int(static_cast<double>(absorption) / 2.0);
    const int max_halves = max_hearts * 2;
    for (int i = max_hearts + absorption_hearts - 1; i >= 0; --i) {
        const int row = i / 10;
        const int column = i % 10;
        const int hx = x + column * 8;
        int hy = y - row * row_height;
        if (health + absorption <= 4) hy += random_.next_int(2);
        if (i < max_hearts && i == regen) hy -= 2;

        gfx.blit_sprite(heart_sprite(HeartType::Container, hardcore, false, blink), hx, hy, 9, 9);
        const int halves = i * 2;
        if (i >= max_hearts) {
            const int absorbed = halves - max_halves;
            if (absorbed < absorption) {
                const bool half = absorbed + 1 == absorption;
                gfx.blit_sprite(heart_sprite(type == HeartType::Withered ? type
                                                                          : HeartType::Absorbing,
                                             hardcore, half, false),
                                hx, hy, 9, 9);
            }
        }
        if (blink && halves < display_health) {
            gfx.blit_sprite(heart_sprite(type, hardcore, halves + 1 == display_health, true), hx,
                            hy, 9, 9);
        }
        if (halves < health) {
            gfx.blit_sprite(heart_sprite(type, hardcore, halves + 1 == health, false), hx, hy, 9,
                            9);
        }
    }
}

void Gui::render_food(GuiGraphics& gfx, const Player& player, int y, int right) {
    const bool hunger = player.has_effect("hunger");
    const std::string_view empty = hunger ? "hud/food_empty_hunger" : "hud/food_empty";
    const std::string_view half = hunger ? "hud/food_half_hunger" : "hud/food_half";
    const std::string_view full = hunger ? "hud/food_full_hunger" : "hud/food_full";
    const int food = player.food;
    for (int i = 0; i < 10; ++i) {
        int fy = y;
        if (player.saturation <= 0.0f && tick_count_ % (food * 3 + 1) == 0) {
            fy += random_.next_int(3) - 1;
        }
        const int x = right - i * 8 - 9;
        gfx.blit_sprite(empty, x, fy, 9, 9);
        if (i * 2 + 1 < food) gfx.blit_sprite(full, x, fy, 9, 9);
        if (i * 2 + 1 == food) gfx.blit_sprite(half, x, fy, 9, 9);
    }
}

void Gui::render_air(GuiGraphics& gfx, const Player& player, int vehicle_hearts, int y,
                     int right) {
    const int max_air = player.max_air;
    const int air = std::clamp(player.air, 0, max_air);
    const bool in_water = player.eye_in_water;
    if (!in_water && air >= max_air) return;
    const int rows = static_cast<int>(std::ceil(vehicle_hearts / 10.0));
    y -= (rows - 1) * 10;
    const int full = air_bubble(air, max_air, -2);
    const int popping = air_bubble(air, max_air, 0);
    const int empty = 10 - air_bubble(air, max_air, air != 0 && in_water ? 1 : 0);
    const bool pops = full != popping;
    for (int i = 1; i <= 10; ++i) {
        const int x = right - (i - 1) * 8 - 9;
        if (i <= full) {
            gfx.blit_sprite("hud/air", x, y, 9, 9);
        } else if (pops && i == popping && in_water) {
            gfx.blit_sprite("hud/air_bursting", x, y, 9, 9);
        } else if (i > 10 - empty) {
            const int shake = empty == 10 && tick_count_ % 2 == 0 ? random_.next_int(2) : 0;
            gfx.blit_sprite("hud/air_empty", x, y + shake, 9, 9);
        }
    }
}

void Gui::render_vehicle_health(GuiGraphics& gfx, const Player& player) {
    if (!player.vehicle_hearts) return;
    int hearts = std::min(static_cast<int>(player.vehicle_max_health + 0.5f) / 2, 30);
    if (hearts == 0) return;
    const int health = static_cast<int>(std::ceil(player.vehicle_health));
    const int right = gfx.width / 2 + 91;
    int y = gfx.height - 39;
    for (int base = 0; hearts > 0; base += 20) {
        const int row = std::min(hearts, 10);
        hearts -= row;
        for (int i = 0; i < row; ++i) {
            const int x = right - i * 8 - 9;
            gfx.blit_sprite("hud/heart/vehicle_container", x, y, 9, 9);
            if (i * 2 + 1 + base < health) gfx.blit_sprite("hud/heart/vehicle_full", x, y, 9, 9);
            if (i * 2 + 1 + base == health) gfx.blit_sprite("hud/heart/vehicle_half", x, y, 9, 9);
        }
        y -= 10;
    }
}

void Gui::render_selected_item_name(GuiGraphics& gfx, const Player& player) {
    if (tool_highlight_timer_ <= 0 || last_highlight_id_ <= 0) return;
    const auto runs = gfx.runs(last_highlight_);
    const int width = gfx.font.width(runs);
    const int x = (gfx.width - width) / 2;
    int y = gfx.height - 59;
    if (!player.can_hurt()) y += 14;
    const int alpha = std::min(static_cast<int>(static_cast<float>(tool_highlight_timer_) *
                                                256.0f / 10.0f),
                               255);
    if (alpha > 0) draw_with_backdrop(gfx, *options_, runs, x, y, width, white(alpha));
}

void Gui::render_effects(GuiGraphics& gfx, const Player& player, const Frame& frame) {
    if (player.effects.empty() || frame.screen_shows_effects) return;
    std::vector<Effect> sorted = player.effects;
    std::stable_sort(sorted.begin(), sorted.end(), effect_before);
    int beneficial = 0;
    int harmful = 0;
    for (const auto& effect : sorted) {
        if (!effect.icon) continue;
        const auto* info = effect_info(effect.id);
        if (info == nullptr) continue;
        int x = gfx.width;
        int y = 1;
        if (info->beneficial) {
            ++beneficial;
            x -= 25 * beneficial;
        } else {
            ++harmful;
            x -= 25 * harmful;
            y += 26;
        }
        float alpha = 1.0f;
        if (effect.ambient) {
            gfx.blit_sprite("hud/effect_background_ambient", x, y, 24, 24);
        } else {
            gfx.blit_sprite("hud/effect_background", x, y, 24, 24);
            if (effect.duration != -1 && effect.duration <= 200) {
                const int duration = effect.duration;
                const int fading = 10 - duration / 20;
                alpha = std::clamp(static_cast<float>(duration) / 10.0f / 5.0f * 0.5f, 0.0f, 0.5f) +
                        jmath::cos(static_cast<float>(duration) * 3.1415927f / 5.0f) *
                            std::clamp(static_cast<float>(fading) / 10.0f * 0.25f, 0.0f, 0.25f);
                alpha = std::clamp(alpha, 0.0f, 1.0f);
            }
        }
        gfx.blit_sprite(std::string("mob_effect/") + info->name, x + 3, y + 3, 18, 18,
                        white(alpha));
    }
}

void Gui::render_bosses(GuiGraphics& gfx, int64_t millis) {
    static constexpr std::string_view kColours[] = {"pink", "blue", "red", "green",
                                                    "yellow", "purple", "white"};
    static constexpr std::string_view kNotches[] = {"6", "10", "12", "20"};
    int y = 12;
    for (const auto& boss : bosses_) {
        const int x = gfx.width / 2 - 91;
        const auto colour = kColours[static_cast<size_t>(std::clamp(boss.colour, 0, 6))];
        const auto bar = [&](int width, std::string_view kind) {
            gfx.blit_sprite(std::format("boss_bar/{}_{}", colour, kind), 182, 5, 0, 0, x, y,
                            width, 5);
            if (boss.overlay > 0 && boss.overlay <= 4) {
                gfx.blit_sprite(std::format("boss_bar/notched_{}_{}",
                                            kNotches[static_cast<size_t>(boss.overlay - 1)], kind),
                                182, 5, 0, 0, x, y, width, 5);
            }
        };
        bar(182, "background");
        const int filled = lerp_discrete(boss.current(millis), 0, 182);
        if (filled > 0) bar(filled, "progress");
        const auto runs = gfx.runs(boss.name);
        gfx.draw_string(runs, gfx.width / 2 - gfx.font.width(runs) / 2, y - 9, 0xFFFFFFFFu, true);
        y += 10 + 9;
        if (y >= gfx.height / 3) break;
    }
}

void Gui::render_sleep(GuiGraphics& gfx, const Player& player) {
    if (player.sleep_timer <= 0) return;
    const float timer = static_cast<float>(player.sleep_timer);
    float amount = timer / 100.0f;
    if (amount > 1.0f) amount = 1.0f - (timer - 100.0f) / 10.0f;
    const uint32_t colour =
        static_cast<uint32_t>(static_cast<int>(220.0f * amount)) << 24 | 0x101020u;
    gfx.fill(0, 0, gfx.width, gfx.height, colour);
}

Component team_name(const Scoreboard::Team* team, Component name) {
    if (team == nullptr) return name;
    Component out = literal("");
    out.extra.push_back(from_nbt(team->prefix));
    out.extra.push_back(std::move(name));
    out.extra.push_back(from_nbt(team->suffix));
    if (team->colour >= 0 && team->colour < 16) {
        out.style.colour = legacy_colour(team->colour);
    } else if (team->colour == 17) {
        out.style.bold = true;
    } else if (team->colour == 18) {
        out.style.strikethrough = true;
    } else if (team->colour == 19) {
        out.style.underlined = true;
    } else if (team->colour == 20) {
        out.style.italic = true;
    } else if (team->colour == 16) {
        out.style.obfuscated = true;
    }
    return out;
}

Component format_score(const Scoreboard::Score* score, const Scoreboard::Objective& objective,
                       uint32_t default_colour) {
    const int32_t value = score != nullptr ? score->value : 0;
    const Scoreboard::NumberFormat* format = &objective.format;
    if (score != nullptr && score->format.kind != Scoreboard::NumberFormat::Kind::Default) {
        format = &score->format;
    }
    using Kind = Scoreboard::NumberFormat::Kind;
    switch (format->kind) {
    case Kind::Blank: return literal("");
    case Kind::Fixed: return from_nbt(format->value);
    case Kind::Styled: {
        Component styled = from_nbt(format->value);
        Component out = literal(std::to_string(value));
        out.style = styled.style;
        return out;
    }
    case Kind::Default: break;
    }
    Style style;
    style.colour = default_colour;
    return literal(std::to_string(value), style);
}

void Gui::render_sidebar(GuiGraphics& gfx, const Options& options, const Frame& frame) {
    if (frame.scoreboard == nullptr) return;
    const Scoreboard& board = *frame.scoreboard;
    const Scoreboard::Objective* objective = nullptr;
    std::string_view objective_name;
    if (const auto* team = board.team_for(frame.own_name);
        team != nullptr && team->colour >= 0 && team->colour < 16) {
        const size_t slot = 3 + static_cast<size_t>(team->colour);
        objective = board.displayed(slot);
        if (objective != nullptr) objective_name = board.display[slot];
    }
    if (objective == nullptr) {
        objective = board.displayed(Scoreboard::kSidebar);
        objective_name = board.display[Scoreboard::kSidebar];
    }
    if (objective == nullptr) return;

    struct Entry {
        std::string_view owner;
        const Scoreboard::Score* score;
        std::vector<Run> name;
        std::vector<Run> value;
        int value_width;
    };
    std::vector<Entry> entries;
    for (const auto& [owner, scores] : board.scores) {
        if (owner.starts_with('#')) continue;
        const auto found = scores.find(objective_name);
        if (found == scores.end()) continue;
        entries.push_back({owner, &found->second, {}, {}, 0});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.score->value != b.score->value) return a.score->value > b.score->value;
        const auto lower = [](std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        };
        return lower(a.owner) < lower(b.owner);
    });
    if (entries.size() > 15) entries.resize(15);
    for (auto& entry : entries) {
        Component owner = entry.score->display_name.is_null()
                              ? literal(std::string(entry.owner))
                              : from_nbt(entry.score->display_name);
        entry.name = gfx.runs(team_name(board.team_for(entry.owner), std::move(owner)));
        entry.value = gfx.runs(format_score(entry.score, *objective, 0xFF5555u));
        entry.value_width = gfx.font.width(entry.value);
    }
    const auto title = gfx.runs(from_nbt(objective->display_name));
    const int title_width = gfx.font.width(title);
    int width = title_width;
    const int spacer = gfx.font.width(": ");
    for (const auto& entry : entries) {
        width = std::max(width, gfx.font.width(entry.name) +
                                    (entry.value_width > 0 ? spacer + entry.value_width : 0));
    }
    const int count = static_cast<int>(entries.size());
    const int bottom = gfx.height / 2 + count * 9 / 3;
    const int left = gfx.width - width - 3;
    const int right = gfx.width - 3 + 2;
    const uint32_t body = background_colour(options, 0.3f);
    const uint32_t head = background_colour(options, 0.4f);
    const int top = bottom - count * 9;
    gfx.fill(left - 2, top - 9 - 1, right, top - 1, head);
    gfx.fill(left - 2, top - 1, right, bottom, body);
    gfx.draw_string(title, left + width / 2 - title_width / 2, top - 9, 0xFFFFFFFFu, false);
    for (int i = 0; i < count; ++i) {
        const auto& entry = entries[static_cast<size_t>(i)];
        const int y = bottom - (count - i) * 9;
        gfx.draw_string(entry.name, left, y, 0xFFFFFFFFu, false);
        gfx.draw_string(entry.value, right - entry.value_width, y, 0xFFFFFFFFu, false);
    }
}

void Gui::render_overlay_message(GuiGraphics& gfx, const Frame& frame) {
    if (!overlay_ || overlay_time_ <= 0) return;
    const float time = static_cast<float>(overlay_time_) - frame.partial;
    const int alpha = std::min(static_cast<int>(time * 255.0f / 20.0f), 255);
    if (alpha <= 0) return;
    const uint32_t colour = animate_overlay_ ? hsv_to_argb(time / 50.0f, 0.7f, 0.6f, alpha)
                                             : white(alpha);
    const auto runs = gfx.runs(*overlay_);
    const int width = gfx.font.width(runs);
    gfx.push_pose();
    gfx.translate(static_cast<float>(gfx.width / 2), static_cast<float>(gfx.height - 68));
    draw_with_backdrop(gfx, *options_, runs, -width / 2, -4, width, colour);
    gfx.pop_pose();
}

void Gui::render_title(GuiGraphics& gfx, const Frame& frame) {
    if (!title_ || title_time_ <= 0) return;
    const float time = static_cast<float>(title_time_) - frame.partial;
    int alpha = 255;
    if (title_time_ > title_fade_out_ + title_stay_) {
        const float in = static_cast<float>(title_fade_in_ + title_stay_ + title_fade_out_) - time;
        alpha = static_cast<int>(in * 255.0f / static_cast<float>(title_fade_in_));
    }
    if (title_time_ <= title_fade_out_) {
        alpha = static_cast<int>(time * 255.0f / static_cast<float>(title_fade_out_));
    }
    alpha = std::clamp(alpha, 0, 255);
    if (alpha <= 0) return;
    const uint32_t colour = white(alpha);
    gfx.push_pose();
    gfx.translate(static_cast<float>(gfx.width / 2), static_cast<float>(gfx.height / 2));
    gfx.push_pose();
    gfx.scale_by(4.0f, 4.0f);
    const auto title = gfx.runs(*title_);
    const int title_width = gfx.font.width(title);
    draw_with_backdrop(gfx, *options_, title, -title_width / 2, -10, title_width, colour);
    gfx.pop_pose();
    if (subtitle_) {
        gfx.push_pose();
        gfx.scale_by(2.0f, 2.0f);
        const auto subtitle = gfx.runs(*subtitle_);
        const int subtitle_width = gfx.font.width(subtitle);
        draw_with_backdrop(gfx, *options_, subtitle, -subtitle_width / 2, 5, subtitle_width,
                           colour);
        gfx.pop_pose();
    }
    gfx.pop_pose();
}

} // namespace mcpp::ui::hud
