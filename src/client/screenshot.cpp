#include "client/screenshot.hpp"

#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/ui.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/keybinds.hpp"
#include "mcpp/options.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/ui/panorama.hpp"
#include "mcpp/ui/pause_screen.hpp"
#include "mcpp/ui/screens.hpp"
#include "mcpp/ui/world.hpp"
#include "mcpp/world/atlas.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>

namespace client {
namespace {

// A recipe book with something in every tab, so the book's layout - the
// tabs, the grid, the page count - can be checked before the server's
// recipe packets are read. Every recipe is shown uncraftable, as it is with
// an empty inventory.
void demo_book(mcpp::ui::RecipeBookView& book, const mcpp::ui::ItemTable& items) {
    const char* names[4][6] = {
        {"iron_axe", "golden_sword", "bow", "shield", "iron_helmet", "fishing_rod"},
        {"bricks", "stone_bricks", "oak_planks", "glass", "sandstone", "polished_andesite"},
        {"bundle", "bucket", "paper", "book", "bread", "cake"},
        {"redstone_torch", "piston", "lever", "hopper", "observer", "repeater"},
    };
    for (size_t c = 0; c < 4; ++c) {
        for (int round = 0; round < 7; ++round) {
            for (const char* name : names[c]) {
                const int32_t id = items.find(name);
                if (id > 0) book.categories[c].push_back({id, 1, false, round % 3 == 0});
            }
        }
    }
    book.open = true;
}

} // namespace

void draw_pause_over(mcpp::gpu::UiRenderer& quads, mcpp::gpu::WorldRenderer& renderer,
                     uint32_t width, uint32_t height) {
    static mcpp::Translations lang;
    lang.load(mcpp::Translations::default_path());
    static mcpp::Options options;
    const mcpp::ui::Font font(quads, &lang);
    const int scale = mcpp::ui::gui_scale(static_cast<int>(width), static_cast<int>(height),
                                          options.get_int("guiScale"), false);
    mcpp::ui::ScreenManager manager;
    manager.lang = &lang;
    manager.options = &options;
    manager.font = &font;
    manager.in_world = true;
    manager.resize(mcpp::ui::gui_extent(static_cast<int>(width), scale),
                   mcpp::ui::gui_extent(static_cast<int>(height), scale));
    manager.set_screen(std::make_shared<mcpp::ui::PauseScreen>(true));
    manager.apply_pending();
    mcpp::ui::GuiGraphics gfx(quads, scale, manager.gui_width, manager.gui_height, &lang);
    gfx.pixel_width = static_cast<int>(width);
    gfx.pixel_height = static_cast<int>(height);
    manager.current()->render_with_tooltip(gfx, -100.0, -100.0);
    renderer.set_backdrop(
        [&quads](VkCommandBuffer command, uint32_t w, uint32_t h) {
            quads.record_under_blur(command, w, h);
        },
        options.get_int("menuBackgroundBlurriness"));
}

int screenshot(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "usage: screenshot <screen|all> [out-dir] [width height] [--at x y] "
                     "[--closed-book] [--scale n] [--spin degrees] [--millis ms]\n";
        for (const auto& known : mcpp::ui::screen_names()) std::cerr << "  " << known.name << '\n';
        return 2;
    }
    std::string which = args[0];
    std::filesystem::path out_dir = ".";
    uint32_t width = 951;
    uint32_t height = 520;
    double at_x = -1.0;
    double at_y = -1.0;
    bool book = true;
    std::vector<int> scales = {2, 3};
    float spin = 0.0f;
    int64_t millis = 0;
    std::vector<std::string> plain;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--at" && i + 2 < args.size()) {
            at_x = std::stod(args[i + 1]);
            at_y = std::stod(args[i + 2]);
            i += 2;
        } else if (args[i] == "--scale" && i + 1 < args.size()) {
            scales = {std::stoi(args[++i])};
        } else if (args[i] == "--spin" && i + 1 < args.size()) {
            spin = std::stof(args[++i]);
        } else if (args[i] == "--millis" && i + 1 < args.size()) {
            millis = std::stoll(args[++i]);
        } else if (args[i] == "--closed-book") {
            book = false;
        } else {
            plain.push_back(args[i]);
        }
    }
    if (!plain.empty()) out_dir = plain[0];
    if (plain.size() >= 3) {
        width = static_cast<uint32_t>(std::stoul(plain[1]));
        height = static_cast<uint32_t>(std::stoul(plain[2]));
    }
    std::filesystem::create_directories(out_dir);

    mcpp::world::Atlas atlas;
    if (!atlas.open(mcpp::world::Atlas::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_all.py\n";
        return 1;
    }
    mcpp::gpu::Device device;
    mcpp::gpu::Device::Options gpu_options;
    device.create(gpu_options);
    mcpp::gpu::WorldRenderer renderer(device, width, height, atlas);
    mcpp::gpu::UiRenderer quads(
        device, renderer.colour_format(),
        mcpp::gpu::UiRenderer::default_items_path(mcpp::protocol::kTargetProtocol),
        mcpp::gpu::UiRenderer::default_gui_path(mcpp::protocol::kTargetProtocol));

    mcpp::Translations lang;
    lang.load(mcpp::Translations::default_path());
    const mcpp::ui::Font font(quads, &lang);
    const bool panorama = mcpp::ui::load_panorama(quads, mcpp::protocol::kTargetVersionName);
    mcpp::Options options;
    mcpp::Keybinds keybinds;
    keybinds.load(options);
    mcpp::ui::ItemTable items;
    items.load(mcpp::ui::ItemTable::default_path(mcpp::protocol::kTargetProtocol));

    auto menu = mcpp::ui::ContainerMenu::player_inventory(&items);
    mcpp::ui::World world;
    world.menu = &menu;
    world.items = &items;
    if (book) demo_book(world.recipe_book, items);

    std::vector<mcpp::ui::ScreenName> todo;
    for (const auto& known : mcpp::ui::screen_names()) {
        if (which == "all" || which == known.name) todo.push_back(known);
    }
    if (todo.empty()) {
        std::cerr << std::format("no screen called {}\n", which);
        return 2;
    }

    for (const auto& entry : todo) {
        for (const int scale : scales) {
            mcpp::ui::ScreenManager manager;
            manager.lang = &lang;
            manager.font = &font;
            manager.username = "PlasmaT";
            manager.panorama.spin = spin;
            manager.options = &options;
            manager.keybinds = &keybinds;
            manager.in_world = entry.in_world;
            manager.world = entry.in_world ? &world : nullptr;
            const int gw = mcpp::ui::gui_extent(static_cast<int>(width), scale);
            const int gh = mcpp::ui::gui_extent(static_cast<int>(height), scale);
            manager.resize(gw, gh);
            manager.set_screen(mcpp::ui::make_screen(entry.name));
            manager.apply_pending();
            const double mx = at_x >= 0.0 ? at_x : -100.0;
            const double my = at_y >= 0.0 ? at_y : -100.0;
            manager.move(mx, my);

            const auto build = [&] {
                quads.begin();
                mcpp::ui::GuiGraphics gfx(quads, scale, gw, gh, &lang);
                gfx.millis = millis;
                gfx.pixel_width = static_cast<int>(width);
                gfx.pixel_height = static_cast<int>(height);
                // No world here: over a world, the panorama stands in for one,
                // so what the blur and the darkening do can be seen.
                if (entry.in_world && panorama) {
                    quads.panorama(0.0f, 0.0f, static_cast<float>(width),
                                   static_cast<float>(height), -spin,
                                   mcpp::ui::PanoramaRenderer::kPitch);
                }
                manager.current()->render_with_tooltip(gfx, mx, my);
            };
            constexpr int kRuns = 200;
            const auto before = std::chrono::steady_clock::now();
            for (int i = 0; i < kRuns; ++i) build();
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - before).count() / kRuns;

            const auto overlay = [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
                quads.record(command, w, h);
            };
            if (quads.blur_marked()) {
                renderer.set_backdrop(
                    [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
                        quads.record_under_blur(command, w, h);
                    },
                    options.get_int("menuBackgroundBlurriness"));
            } else {
                renderer.set_backdrop({}, 0);
            }
            mcpp::gpu::Camera camera{};
            camera.eye[1] = 10000.0f;
            camera.target[0] = 1.0f;
            camera.target[1] = 10000.0f;
            const auto pixels = renderer.render(camera, overlay);
            const auto path = out_dir / std::format("{}-s{}.png", entry.name, scale);
            mcpp::write_png(path, width, height, pixels);
            std::cout << std::format("{}  {:.3f} ms cpu to build the frame's interface\n",
                                     path.string(), ms);
        }
    }
    device.save_pipeline_cache();
    return 0;
}

} // namespace client
