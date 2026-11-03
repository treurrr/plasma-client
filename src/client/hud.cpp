#include "client/game.hpp"

#include "mcpp/keybinds.hpp"
#include "mcpp/options.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/ui/hud/hud.hpp"
#include "mcpp/ui/items.hpp"
#include "mcpp/ui/screen.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>

namespace client {

struct HudArea {
    mcpp::ui::hud::Gui gui;
    mcpp::ui::hud::Player player;
    std::vector<mcpp::ui::hud::TabEntry> players;
    mcpp::ui::ItemTable items;

    // The client's own copy of what LocalPlayer keeps for the hud.
    float last_health = -1.0f;
    bool flash_on_set_health = false;
    int32_t invulnerable_time = 0;
    int32_t attack_ticker = 0;
    int32_t last_held_id = -1;
    std::array<mcpp::ItemStack, 9> last_hotbar{};
    std::array<int32_t, 9> pop{};
    bool hotbar_seen = false;

    std::vector<std::pair<mcpp::ui::Component, int32_t>> pending_chat;
    double cpu_ms_total = 0.0;
    size_t frames = 0;
};

namespace {

int64_t millis_now() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

mcpp::ui::Component item_name(const HudArea& hud, int32_t id) {
    const auto key = hud.items.name_key(id);
    mcpp::ui::Style style;
    style.colour = mcpp::ui::ItemTable::rarity_colour(hud.items.rarity(id));
    mcpp::ui::Component name = mcpp::ui::translatable(std::string(key));
    mcpp::ui::Component out = mcpp::ui::literal("", style);
    out.extra.push_back(std::move(name));
    return out;
}

void fill_player(Game& game, HudArea& hud) {
    const auto& session = *game.session;
    auto& player = hud.player;
    const auto& vitals = session.vitals();
    const auto& inventory = session.inventory();
    const int32_t self = session.entity_id();

    player.game_mode = vitals.game_mode;
    player.hardcore = session.hardcore();
    player.health = vitals.health;
    player.max_health =
        static_cast<float>(session.attribute(self, "minecraft:max_health", 20.0));
    player.invulnerable_time = hud.invulnerable_time;
    player.food = vitals.food;
    player.saturation = vitals.saturation;
    player.experience = vitals.experience;
    player.level = vitals.level;

    int32_t armour = 0;
    for (size_t i = 0; i < mcpp::Inventory::kArmourSlots; ++i) {
        const auto& piece = inventory.slots[mcpp::Inventory::kArmourFirst + i];
        if (piece.count > 0) armour += game.screens.quads->item_armour(piece.id);
    }
    player.armour = static_cast<int32_t>(
        std::floor(session.attribute(self, "minecraft:armor", static_cast<double>(armour))));

    player.effects.clear();
    for (const auto& [id, effect] : session.effects(self)) {
        player.effects.push_back({effect.id, effect.amplifier, effect.duration, effect.ambient,
                                  effect.icon});
    }

    for (size_t i = 0; i < 9; ++i) {
        const auto& stack = inventory.hotbar(i);
        auto& item = player.hotbar[i];
        item.id = stack.count > 0 ? stack.id : 0;
        item.count = stack.count;
        item.pop_time = hud.pop[i];
    }
    const auto& offhand = inventory.slots[mcpp::Inventory::kOffhand];
    player.offhand.id = offhand.count > 0 ? offhand.id : 0;
    player.offhand.count = offhand.count;
    player.selected = std::clamp(inventory.held, 0, 8);
    const auto& held = player.hotbar[static_cast<size_t>(player.selected)];
    player.selected_name = held.empty() ? mcpp::ui::Component{} : item_name(hud, held.id);
    player.left_handed = game.screens.options.get_string("mainHand") == "left";

    const double speed = session.attribute(self, "minecraft:attack_speed", 4.0);
    player.attack_delay = static_cast<float>(1.0 / speed * 20.0);
    player.attack_strength =
        std::clamp(static_cast<float>(hud.attack_ticker) / player.attack_delay, 0.0f, 1.0f);
    player.first_person = true;
}

} // namespace

void hud_bind(Game& game, mcpp::SessionEvents& events) {
    game.hud = std::make_shared<HudArea>();
    auto& ui = events.ui;
    ui.on_chat_component = [&game](const mcpp::nbt::Value& line, int32_t tag) {
        // Lines that come before the window wait for a font to be wrapped by.
        game.hud->pending_chat.emplace_back(mcpp::ui::from_nbt(line), tag);
    };
    ui.on_overlay_message = [&game](const mcpp::nbt::Value& text) {
        game.hud->gui.set_overlay_message(mcpp::ui::from_nbt(text), false);
    };
    ui.on_title = [&game](const mcpp::nbt::Value& text) {
        game.hud->gui.set_title(mcpp::ui::from_nbt(text));
    };
    ui.on_subtitle = [&game](const mcpp::nbt::Value& text) {
        game.hud->gui.set_subtitle(mcpp::ui::from_nbt(text));
    };
    ui.on_title_times = [&game](int32_t in, int32_t stay, int32_t out) {
        game.hud->gui.set_times(in, stay, out);
    };
    ui.on_clear_titles = [&game](bool reset) {
        game.hud->gui.clear_titles();
        if (reset) game.hud->gui.reset_title_times();
    };
    ui.on_boss_event = [&game](const mcpp::BossEvent& event) {
        game.hud->gui.boss_event(event, millis_now());
    };
    ui.on_tab_list = [&game](const mcpp::nbt::Value& header, const mcpp::nbt::Value& footer) {
        const auto present = [](const mcpp::nbt::Value& v) {
            if (v.is_null()) return false;
            const std::string* text = v.as_string();
            return text == nullptr || !text->empty();
        };
        game.hud->gui.set_tab_list(mcpp::ui::from_nbt(header), mcpp::ui::from_nbt(footer),
                                   present(header), present(footer));
    };
    auto& hud = *game.hud;
    hud.items.load(mcpp::ui::ItemTable::default_path(mcpp::protocol::kTargetProtocol));
}

void hud_start(Game&) {}

void hud_tick(Game& game) {
    auto& hud = *game.hud;
    const auto& session = *game.session;
    const auto& inventory = session.inventory();

    // Every frame: what LocalPlayer::hurtTo does when set_health lands.
    const float health = session.vitals().health;
    if (health != hud.last_health) {
        if (hud.flash_on_set_health) {
            const float lost = hud.last_health - health;
            if (lost < 0.0f) hud.invulnerable_time = 10;
            if (lost > 0.0f) hud.invulnerable_time = 20;
        }
        hud.flash_on_set_health = true;
        hud.last_health = health;
    }
    if (game.input.attack_clicks > 0) hud.attack_ticker = 0;

    if (!game.tick_due) return;
    if (hud.invulnerable_time > 0) --hud.invulnerable_time;
    ++hud.attack_ticker;
    const int32_t held = inventory.hotbar(static_cast<size_t>(std::clamp(inventory.held, 0, 8))).id;
    if (held != hud.last_held_id) {
        hud.attack_ticker = 0;
        hud.last_held_id = held;
    }
    for (size_t i = 0; i < 9; ++i) {
        const auto& now = inventory.hotbar(i);
        const auto& before = hud.last_hotbar[i];
        if (hud.pop[i] > 0) --hud.pop[i];
        if (hud.hotbar_seen && now.count > before.count &&
            (now.id == before.id || before.count == 0)) {
            hud.pop[i] = 5;
        }
        hud.last_hotbar[i] = now;
    }
    hud.hotbar_seen = true;
    fill_player(game, hud);
    hud.gui.tick(hud.player, game.screens.options);
}

void hud_render(Game& game) {
    if (!game.hud || !game.screens.quads) return;
    const auto started = std::chrono::steady_clock::now();
    auto& hud = *game.hud;
    auto& window = *game.window;
    auto& session = *game.session;
    fill_player(game, hud);
    if (!hud.pending_chat.empty()) {
        const mcpp::ui::Font font(*game.screens.quads, &game.screens.lang);
        for (const auto& [line, tag] : hud.pending_chat) {
            hud.gui.chat().add(line, tag, hud.gui.ticks(), font, game.screens.options, window.typing());
        }
        hud.pending_chat.clear();
    }

    const int scale = mcpp::ui::gui_scale(static_cast<int>(window.width()),
                                          static_cast<int>(window.height()),
                                          game.screens.options.get_int("guiScale"), false);
    const int gw = mcpp::ui::gui_extent(static_cast<int>(window.width()), scale);
    const int gh = mcpp::ui::gui_extent(static_cast<int>(window.height()), scale);
    mcpp::ui::GuiGraphics gfx(*game.screens.quads, scale, gw, gh, &game.screens.lang);

    hud.players.clear();
    mcpp::ui::hud::TabEntry self;
    self.uuid = session.profile_uuid();
    self.name = session.profile_name();
    self.game_mode = session.vitals().game_mode;
    self.latency = session.ping_ms();
    hud.players.push_back(std::move(self));

    mcpp::ui::hud::Frame frame;
    frame.millis = millis_now();
    frame.chat_focused = window.typing();
    frame.own_name = session.profile_name();
    frame.scoreboard = &session.scoreboard();
    frame.players = &hud.players;
    const auto* list_key = game.screens.keybinds.find("key.playerlist");
    frame.player_list_down = !window.typing() && list_key != nullptr &&
                             list_key->current.type == mcpp::InputKey::Type::Keyboard &&
                             window.key_down(list_key->current.code);
    frame.screen_shows_effects = game.screens.inventory_open;
    hud.gui.render(gfx, hud.player, game.screens.options, frame);

    if (window.typing()) {
        // The chat screen's input line until the screens area draws its own.
        hud.gui.chat().render(gfx, game.screens.options, hud.gui.ticks(), true, -1, -1);
        gfx.fill(2, gh - 14, gw - 2, gh - 2, 0x80000000u);
        gfx.draw_string(window.typed() + "_", 4, gh - 12, 0xFFE0E0E0u, true);
    }

    hud.cpu_ms_total +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    ++hud.frames;
}

void hud_stop(Game& game) {
    if (!game.hud || game.hud->frames == 0) return;
    std::cout << std::format("- hud: {:.4f} ms cpu per frame over {} frames\n",
                             game.hud->cpu_ms_total / static_cast<double>(game.hud->frames),
                             game.hud->frames);
}

} // namespace client
