#include "client/game.hpp"

#include "mcpp/features.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/ui/container_screen.hpp"
#include "mcpp/ui/menu.hpp"
#include "mcpp/ui/screens.hpp"
#include "mcpp/ui/text_component.hpp"

#include <GLFW/glfw3.h>

#include <format>
#include <iostream>

namespace client {
namespace {

int gui_scale_of(const Game& game) {
    const auto& window = *game.window;
    return mcpp::ui::gui_scale(static_cast<int>(window.width()), static_cast<int>(window.height()),
                               game.screens.options.get_int("guiScale"), false);
}

// Copies what the server last said into the menu the screens click in. The
// session already holds the client's prediction of every click it sent.
void sync_menu(Game& game) {
    auto& screens = game.screens;
    const auto& session = *game.session;
    const auto& inventory = session.inventory();
    for (size_t i = 0; i < mcpp::Inventory::kSlots; ++i) screens.menu.slots[i] = inventory.slots[i];
    screens.menu.carried = inventory.carried;
    screens.menu.state_id = session.container_state();
    screens.menu.creative = session.vitals().game_mode == 1;
    screens.world.game_mode = session.vitals().game_mode;
    auto& container = screens.container;
    if (container && container->container_id == session.open_container()) {
        const auto& held = session.container_slots();
        for (size_t i = 0; i < container->size() && i < held.size(); ++i) container->slots[i] = held[i];
        container->carried = inventory.carried;
        container->creative = screens.menu.creative;
        screens.world.menu = &*container;
    } else {
        container.reset();
        screens.world.menu = &screens.menu;
    }
}

// `ClientPacketListener.refreshRecipeBook`: regroups the book and tells an open book screen.
void refresh_recipe_book(Game& game) {
    auto& screens = game.screens;
    screens.world.recipe_book.rebuild_collections();
    if (auto* screen = dynamic_cast<mcpp::ui::AbstractRecipeBookScreen*>(screens.manager.current())) {
        screen->recipe_book.recipes_updated();
    }
}

void open_screen(Game& game, std::shared_ptr<mcpp::ui::Screen> screen) {
    auto& screens = game.screens;
    screens.manager.set_screen(std::move(screen));
    screens.manager.apply_pending();
    screens.opened = std::chrono::steady_clock::now();
}

bool bound(const Game& game, std::string_view name, int key) {
    const auto* bind = game.screens.keybinds.find(name);
    return bind != nullptr && bind->current.type == mcpp::InputKey::Type::Keyboard &&
           bind->current.code == key;
}

} // namespace

void screens_bind(Game& game, mcpp::SessionEvents& events) {
    events.ui.on_chat = [&game](std::string_view line) {
        auto& screens = game.screens;
        std::lock_guard lock(screens.chat_guard);
        screens.chat.emplace_back(line);
        if (screens.chat.size() > 64) screens.chat.erase(screens.chat.begin());
    };
    events.ui.on_container_close = [&game](int32_t) {
        if (dynamic_cast<mcpp::ui::AbstractContainerScreen*>(game.screens.manager.current())) {
            game.screens.manager.close();
        }
    };
    // `ClientPacketListener.handleOpenScreen`, `MenuScreens.create`: the crafting table's screen.
    events.ui.on_open_screen = [&game](int32_t container, int32_t type, const mcpp::nbt::Value& title) {
        auto& screens = game.screens;
        const auto* menu = game.session->registries().at("minecraft:menu", static_cast<size_t>(type));
        if (menu == nullptr || menu->name != "minecraft:crafting" || !screens.quads) return;
        screens.container.emplace(mcpp::ui::ContainerMenu::crafting_table(&screens.items));
        screens.container->container_id = container;
        screens.world.menu = &*screens.container;
        open_screen(game, std::make_shared<mcpp::ui::CraftingScreen>(mcpp::ui::from_nbt(title)));
    };
    // `ClientPacketListener.handlePlaceRecipe`: the ghost of a recipe the player lacks items for.
    events.ui.on_place_ghost_recipe = [&game](int32_t container, const mcpp::proto::Value& display) {
        auto& screens = game.screens;
        if (screens.world.menu == nullptr || screens.world.menu->container_id != container) return;
        if (auto* screen = dynamic_cast<mcpp::ui::AbstractRecipeBookScreen*>(screens.manager.current())) {
            screen->recipe_book.fill_ghost_recipe(
                mcpp::ui::read_recipe_display(display, game.session->tags()));
        }
    };
    events.ui.on_recipe_book_add = [&game](const mcpp::proto::Value& added) {
        game.screens.world.recipe_book.add_batch(added, game.session->registries(),
                                                 game.session->tags());
        refresh_recipe_book(game);
    };
    // `ClientPacketListener.handleRecipeBookRemove`: recipes taken away.
    events.ui.on_recipe_book_remove = [&game](const std::vector<int32_t>& gone) {
        for (const int32_t id : gone) game.screens.world.recipe_book.remove(id);
        refresh_recipe_book(game);
    };
    // `ClientPacketListener.handleRecipeBookSettings`: the crafting book's open and filter switches.
    events.ui.on_recipe_book_settings = [&game](const mcpp::proto::Value& settings) {
        auto& book = game.screens.world.recipe_book;
        const auto flag = [&](std::string_view name) {
            const auto* value = settings.field(name);
            return value != nullptr && value->as_bool().value_or(false);
        };
        book.open = flag("crafting_open");
        book.filtering = flag("crafting_filtering");
        refresh_recipe_book(game);
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

    screens.lang.load(mcpp::Translations::default_path());
    if (!screens.options.load(mcpp::Options::default_path())) {
        if (const auto vanilla = mcpp::Options::vanilla_path()) screens.options.load(*vanilla);
    }
    screens.keybinds.load(screens.options);
    screens.items.load(mcpp::ui::ItemTable::default_path(mcpp::protocol::kTargetProtocol));

    auto& manager = screens.manager;
    manager.lang = &screens.lang;
    manager.options = &screens.options;
    manager.keybinds = &screens.keybinds;
    manager.in_world = true;
    manager.world = &screens.world;
    manager.key_name = [&game](std::string_view name) {
        const auto* bind = game.screens.keybinds.find(name);
        return bind != nullptr ? bind->current.name() : std::string(name);
    };

    auto& world = screens.world;
    world.menu = &screens.menu;
    world.items = &screens.items;
    world.click = [&game](int slot, int button, mcpp::ui::ClickType type,
                          const std::vector<std::pair<int16_t, mcpp::ItemStack>>& changed) {
        auto& menu = *game.screens.world.menu;
        auto& session = *game.session;
        session.send_container_click(menu.container_id, static_cast<int16_t>(slot),
                                     static_cast<int8_t>(button), static_cast<int32_t>(type),
                                     changed, menu.carried);
        if (menu.container_id != 0) {
            session.predict_container(menu.slots, menu.carried);
            return;
        }
        std::array<mcpp::ItemStack, mcpp::Inventory::kSlots> slots{};
        for (size_t i = 0; i < slots.size() && i < menu.slots.size(); ++i) slots[i] = menu.slots[i];
        session.predict_inventory(slots, menu.carried);
    };
    world.close_container = [&game](int container) {
        game.session->send_container_close(container);
    };
    world.recipe_book_changed = [&game](bool open, bool filtering) {
        game.session->send_recipe_book_settings(0, open, filtering);
    };
    world.place_recipe = [&game](int32_t container, int32_t recipe, bool use_max) {
        game.session->send_place_recipe(container, recipe, use_max);
    };
    world.recipe_seen = [&game](int32_t recipe) { game.session->send_recipe_seen(recipe); };
    world.creative_set = [&game](int16_t slot, const mcpp::ItemStack& stack) {
        game.session->send_creative_slot(slot, stack);
    };
    world.commit = [&game]() {
        auto& menu = game.screens.menu;
        std::array<mcpp::ItemStack, mcpp::Inventory::kSlots> slots{};
        for (size_t i = 0; i < slots.size() && i < menu.slots.size(); ++i) slots[i] = menu.slots[i];
        game.session->predict_inventory(slots, menu.carried);
    };
    manager.platform.get_clipboard = [&window]() { return window.clipboard(); };
    manager.platform.set_clipboard = [&window](const std::string& text) {
        window.set_clipboard(text);
    };
}

void screens_input(Game& game) {
    auto& screens = game.screens;
    auto& window = *game.window;
    auto& manager = screens.manager;
    auto& input = game.input;

    const int scale = gui_scale_of(game);
    const double pixels = window.content_scale();
    manager.resize(mcpp::ui::gui_extent(static_cast<int>(window.width()), scale),
                   mcpp::ui::gui_extent(static_cast<int>(window.height()), scale));
    sync_menu(game);

    const double now = std::chrono::duration<double>(game.now.time_since_epoch()).count();
    for (const auto& event : window.take_events()) {
        using Type = mcpp::gpu::InputEvent::Type;
        const double gx = event.x * pixels / scale;
        const double gy = event.y * pixels / scale;
        if (event.type == Type::Key) {
            if (event.key == GLFW_KEY_LEFT_SHIFT || event.key == GLFW_KEY_RIGHT_SHIFT) {
                manager.shift_down = event.action != GLFW_RELEASE;
            }
            if (event.key == GLFW_KEY_LEFT_CONTROL || event.key == GLFW_KEY_RIGHT_CONTROL) {
                manager.control_down = event.action != GLFW_RELEASE;
            }
        }
        if (manager.current() == nullptr) {
            if (event.type == Type::Key && event.action == GLFW_PRESS && !window.typing() &&
                !screens.menu_open && bound(game, "key.inventory", event.key)) {
                if (game.session->vitals().game_mode == 1) {
                    open_screen(game, std::make_shared<mcpp::ui::CreativeModeInventoryScreen>(
                                          &mcpp::ui::creative_tabs(), false));
                } else {
                    open_screen(game, std::make_shared<mcpp::ui::InventoryScreen>());
                }
            }
            continue;
        }
        switch (event.type) {
        case Type::Key: manager.key(event.key, event.scancode, event.action, event.mods); break;
        case Type::Char: manager.character(static_cast<uint32_t>(event.key), event.mods); break;
        case Type::Button:
            manager.button(event.key, event.action == GLFW_PRESS, event.mods, gx, gy, now);
            break;
        case Type::Scroll: manager.scroll(manager.mouse_x, manager.mouse_y, event.x, event.y); break;
        case Type::Move: manager.move(gx, gy); break;
        }
    }
    manager.apply_pending();

    const bool screen = manager.current() != nullptr;
    screens.inventory_open = screen;
    if (screen) {
        // A screen has the keyboard and the pointer; the game sees nothing.
        input.forward = input.right = input.up = 0.0f;
        input.yaw_delta = input.pitch_delta = 0.0f;
        input.sprint = input.sprint_key = input.jump = input.sneak = false;
        input.attack = input.use = false;
        input.attack_clicks = input.use_clicks = input.pick_item_clicks = 0;
        input.drop = input.drop_stack = input.swap_offhand = false;
        input.toggle_inventory = input.toggle_menu = input.toggle_fly = false;
        input.start_typing = false;
        input.wheel = 0;
        input.pick_slot = -1;
        input.screen_open = true;
    }
    window.capture_cursor(!screen && !screens.menu_open && !window.typing());

    if (!screen) {
        // The Plasma feature menu exists only in the Plasma profile.
        const bool plasma = mcpp::features::Registry::instance().profile() ==
                            mcpp::features::Profile::Plasma;
        if (input.toggle_menu && plasma) screens.menu_open = !screens.menu_open;
        if (input.quit) screens.menu_open = false;
        if (input.start_typing && !screens.menu_open) window.begin_typing();
    }
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

    if (game.tick_due) screens.manager.tick();

    if (!window.typing() && screens.manager.current() == nullptr) {
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
    hud_render(game);

    if (auto* screen = screens.manager.current()) {
        const int scale = gui_scale_of(game);
        mcpp::ui::GuiGraphics gfx(quads, scale, screens.manager.gui_width,
                                  screens.manager.gui_height, &screens.lang);
        gfx.key_name = screens.manager.key_name;
        gfx.seconds =
            std::chrono::duration<double>(game.now - screens.opened).count();
        screen->render_with_tooltip(gfx, screens.manager.mouse_x, screens.manager.mouse_y);
    }

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

void screens_stop(Game& game) {
    auto& screens = game.screens;
    screens.keybinds.store(screens.options);
    screens.options.save(mcpp::Options::default_path());
}

} // namespace client
