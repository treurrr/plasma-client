#include "client/game.hpp"

#include "mcpp/features.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/ui/menu.hpp"

#include <format>
#include <iostream>

namespace client {

void screens_bind(Game& game, mcpp::SessionEvents& events) {
    events.ui.on_chat = [&game](std::string_view line) {
        auto& screens = game.screens;
        std::lock_guard lock(screens.chat_guard);
        screens.chat.emplace_back(line);
        if (screens.chat.size() > 64) screens.chat.erase(screens.chat.begin());
    };
}

void screens_start(Game& game) {
    auto& screens = game.screens;
    auto& window = *game.window;
    screens.hud.emplace(game.device, mcpp::gpu::TextRenderer::default_font_path(),
                        window.colour_format());
    screens.quads.emplace(
        game.device, window.colour_format(),
        mcpp::gpu::UiRenderer::default_items_path(mcpp::protocol::kTargetProtocol),
        mcpp::gpu::UiRenderer::default_gui_path(mcpp::protocol::kTargetProtocol));
}

void screens_input(Game& game) {
    auto& screens = game.screens;
    auto& window = *game.window;
    const auto& input = game.input;

    if (input.quit) {
        screens.inventory_open = false;
        screens.menu_open = false;
        window.capture_cursor(true);
    }
    if (input.toggle_menu) {
        screens.menu_open = !screens.menu_open;
        window.capture_cursor(!screens.menu_open && !screens.inventory_open);
    }
    if (input.start_typing && !screens.menu_open) window.begin_typing();
    if (window.typing()) {
        if (input.submit_text) {
            const auto line = window.typed();
            if (!line.empty()) {
                try {
                    game.session->send_chat(line);
                } catch (const std::exception& error) {
                    std::cerr << std::format("chat failed: {}\n", error.what());
                }
            }
            window.end_typing();
        } else if (input.cancel_text) {
            window.end_typing();
        }
    }
}

void screens_tick(Game& game) {
    auto& screens = game.screens;
    auto& window = *game.window;
    auto& session = *game.session;
    const auto& input = game.input;

    if (input.toggle_inventory && !window.typing()) {
        screens.inventory_open = !screens.inventory_open;
        window.capture_cursor(!screens.inventory_open && !screens.menu_open);
    }
    if (input.toggle_menu && screens.inventory_open) {
        screens.inventory_open = false;
        window.capture_cursor(!screens.menu_open);
    }

    if (!window.typing() && !screens.inventory_open) {
        if (input.pick_slot >= 0) {
            session.send_held_slot(input.pick_slot);
        } else if (input.wheel != 0) {
            const int32_t slots = static_cast<int32_t>(mcpp::Inventory::kHotbarSlots);
            session.send_held_slot((session.inventory().held - input.wheel + slots) % slots);
        }
    }
}

void screens_draw(Game& game) {
    auto& screens = game.screens;
    auto& window = *game.window;
    auto& session = *game.session;
    auto& renderer = *game.renderer;
    auto& hud = *screens.hud;
    auto& quads = *screens.quads;
    const auto& input = game.input;
    const float* eye = game.player.eye;

    mcpp::ui::HudState state;
    state.fps = static_cast<int>(game.fps + 0.5);
    state.gpu_ms = renderer.last_gpu_ms();
    state.cpu_ms = window.last_frame_ms();
    state.ping_ms = session.ping_ms();
    state.cps_left = window.clicks_per_second(false);
    state.cps_right = window.clicks_per_second(true);
    state.x = eye[0];
    state.y = eye[1] - 1.62;
    state.z = eye[2];
    state.yaw = game.player.yaw;
    state.forward = input.forward > 0.0f;
    state.back = input.forward < 0.0f;
    state.left = input.right < 0.0f;
    state.right = input.right > 0.0f;
    state.jump = input.up > 0.0f;
    state.sections = renderer.drawn_sections();
    {
        const auto& inventory = session.inventory();
        for (size_t i = 0; i < mcpp::Inventory::kHotbarSlots; ++i) {
            state.hotbar[i] = inventory.hotbar(i).id;
            state.hotbar_counts[i] = inventory.hotbar(i).count;
        }
        state.held = inventory.held;
        const auto& in_hand = inventory.hotbar(static_cast<size_t>(inventory.held));
        state.held_name = in_hand.count > 0 ? item_display_name(quads.item_name(in_hand.id))
                                            : std::string{};
        const auto& vitals = session.vitals();
        state.health = vitals.health;
        state.food = vitals.food;
        state.experience = vitals.experience;
        state.level = vitals.level;
        state.in_world = true;
        state.survival_bars = session.vitals().survival_bars();
        state.inventory_open = screens.inventory_open;
        state.pointer_x = input.cursor_x;
        state.pointer_y = input.cursor_y;
        for (size_t i = 0; i < mcpp::Inventory::kSlots; ++i) {
            state.slots[i] = inventory.slots[i].id;
            state.slot_counts[i] = inventory.slots[i].count;
            state.slot_names[i] = inventory.slots[i].count > 0
                                      ? item_display_name(quads.item_name(inventory.slots[i].id))
                                      : std::string{};
        }
        state.cursor_item = inventory.carried.id;
        state.cursor_count = inventory.carried.count;
        state.armour = 0;
        for (size_t i = 0; i < mcpp::Inventory::kArmourSlots; ++i) {
            const auto& piece = inventory.slots[mcpp::Inventory::kArmourFirst + i];
            if (piece.count > 0) state.armour += quads.item_armour(piece.id);
        }
    }
    state.entities = session.entity_count();
    state.typing = window.typing();
    state.compose = window.typed();
    {
        std::lock_guard lock(screens.chat_guard);
        state.chat = screens.chat;
    }

    hud.begin();
    quads.begin();
    mcpp::ui::draw_hud(quads, hud, static_cast<float>(window.width()),
                       static_cast<float>(window.height()), state);
    if (screens.menu_open) {
        mcpp::ui::Pointer pointer{input.cursor_x, input.cursor_y, input.click};
        mcpp::ui::MenuState menu;
        menu.server = game.address.host;
        menu.fps = state.fps;
        menu.ping_ms = state.ping_ms;
        menu.x = state.x;
        menu.y = state.y;
        menu.z = state.z;
        menu.yaw = state.yaw;
        menu.filter = screens.menu_filter;
        const auto hit = mcpp::ui::draw_menu(quads, hud, static_cast<float>(window.width()),
                                             static_cast<float>(window.height()), menu,
                                             pointer);
        if (hit.filter_clicked > -2) screens.menu_filter = hit.filter_clicked;
        if (hit.toggled >= 0) {
            auto& registry = mcpp::features::Registry::instance();
            const auto id = static_cast<mcpp::features::Id>(hit.toggled);
            registry.set(id, !registry.wanted(id));
            registry.save(mcpp::features::Registry::default_config_path());
        }
    }
}

void screens_record(Game& game, VkCommandBuffer command, uint32_t width, uint32_t height) {
    game.screens.quads->record(command, width, height);
    game.screens.hud->record(command, width, height);
}

void screens_stop(Game&) {}

} // namespace client
