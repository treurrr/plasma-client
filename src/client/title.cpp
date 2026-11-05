#include "client/game.hpp"

#include "mcpp/protocol_versions.hpp"
#include "mcpp/ui/title_screen.hpp"

#include <GLFW/glfw3.h>

#include <iostream>

namespace client {

bool title_screen(Game& game) {
    mcpp::gpu::Device::Options gpu_options;
    gpu_options.instance_extensions = mcpp::gpu::Window::required_instance_extensions();
    gpu_options.swapchain = true;
    game.device.create(gpu_options);
    if (!game.atlas.open(mcpp::world::Atlas::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_atlas.py\n";
        return false;
    }
    auto& window = game.window.emplace(game.device, 1600, 900, "Plasma Client");
    auto& renderer =
        game.renderer.emplace(game.device, window.width(), window.height(), game.atlas);

    mcpp::gpu::UiRenderer quads(
        game.device, window.colour_format(),
        mcpp::gpu::UiRenderer::default_items_path(mcpp::protocol::kTargetProtocol),
        mcpp::gpu::UiRenderer::default_gui_path(mcpp::protocol::kTargetProtocol));
    mcpp::ui::load_panorama(quads, mcpp::protocol::kTargetVersionName);
    mcpp::Translations lang;
    lang.load(mcpp::Translations::default_path());
    mcpp::Options options;
    if (!options.load(mcpp::Options::default_path())) {
        if (const auto vanilla = mcpp::Options::vanilla_path()) options.load(*vanilla);
    }
    const mcpp::ui::Font font(quads, &lang);

    bool multiplayer = false;
    bool quit = false;
    mcpp::ui::ScreenManager manager;
    manager.lang = &lang;
    manager.options = &options;
    manager.font = &font;
    manager.username = game.options.username;
    manager.host.multiplayer = [&] { multiplayer = true; };
    manager.host.quit = [&] { quit = true; };
    manager.set_screen(std::make_shared<mcpp::ui::TitleScreen>(true));

    mcpp::gpu::Camera camera{};
    camera.eye[1] = 10000.0f;
    camera.target[0] = 1.0f;
    camera.target[1] = 10000.0f;
    window.capture_cursor(false);
    auto previous = std::chrono::steady_clock::now();
    while (window.open() && !multiplayer && !quit) {
        window.poll();
        const int scale = mcpp::ui::gui_scale(static_cast<int>(window.width()),
                                              static_cast<int>(window.height()),
                                              options.get_int("guiScale"), false);
        manager.resize(mcpp::ui::gui_extent(static_cast<int>(window.width()), scale),
                       mcpp::ui::gui_extent(static_cast<int>(window.height()), scale));
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
        const double pixels = window.content_scale();
        for (const auto& event : window.take_events()) {
            using Type = mcpp::gpu::InputEvent::Type;
            const double gx = event.x * pixels / scale;
            const double gy = event.y * pixels / scale;
            switch (event.type) {
            case Type::Key: manager.key(event.key, event.scancode, event.action, event.mods); break;
            case Type::Char: manager.character(static_cast<uint32_t>(event.key), event.mods); break;
            case Type::Button:
                manager.button(event.key, event.action == GLFW_PRESS, event.mods, gx, gy, seconds);
                break;
            case Type::Scroll:
                manager.scroll(manager.mouse_x, manager.mouse_y, event.x, event.y);
                break;
            case Type::Move: manager.move(gx, gy); break;
            }
        }
        manager.apply_pending();
        if (manager.current() == nullptr) break;

        if (window.width() != 0 && window.height() != 0 &&
            (window.width() != renderer.width() || window.height() != renderer.height())) {
            renderer.resize(window.width(), window.height());
        }
        quads.begin();
        mcpp::ui::GuiGraphics gfx(quads, scale, manager.gui_width, manager.gui_height, &lang);
        gfx.millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
        gfx.realtime_delta_ticks =
            std::chrono::duration<float, std::milli>(now - previous).count() / 50.0f;
        previous = now;
        gfx.pixel_width = static_cast<int>(window.width());
        gfx.pixel_height = static_cast<int>(window.height());
        manager.current()->render_with_tooltip(gfx, manager.mouse_x, manager.mouse_y);
        if (quads.blur_marked()) {
            renderer.set_backdrop(
                [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
                    quads.record_under_blur(command, w, h);
                },
                options.get_int("menuBackgroundBlurriness"));
        } else {
            renderer.set_backdrop({}, 0);
        }
        window.present(renderer, camera, [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
            quads.record(command, w, h);
        });
    }
    renderer.set_backdrop({}, 0);
    vkDeviceWaitIdle(game.device.handle());
    return multiplayer && window.open();
}

} // namespace client
