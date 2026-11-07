#include "client/itemshot.hpp"

#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/ui.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/options.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/ui/container_screen.hpp"
#include "mcpp/ui/items.hpp"
#include "mcpp/ui/layout.hpp"
#include "mcpp/ui/screen.hpp"
#include "mcpp/ui/screens.hpp"
#include "mcpp/world/atlas.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <vector>

namespace client {
namespace {

constexpr int32_t kProtocol = mcpp::protocol::kTargetProtocol;
using Clock = std::chrono::steady_clock;

double millis(Clock::duration taken) {
    return std::chrono::duration<double, std::milli>(taken).count();
}

} // namespace

int itemshot(const std::vector<std::string>& args) {
    const std::filesystem::path out_dir = args.empty() ? "." : args[0];
    std::filesystem::create_directories(out_dir);

    mcpp::world::Atlas atlas;
    if (!atlas.open(mcpp::world::Atlas::default_path(kProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_all.py\n";
        return 1;
    }
    mcpp::ui::ItemTable items;
    items.load(mcpp::ui::ItemTable::default_path(kProtocol));
    mcpp::Translations lang;
    lang.load(mcpp::Translations::default_path());
    mcpp::Options options;

    // Every item the game knows, in one sheet, at the size a slot draws one.
    constexpr uint32_t kCell = 20;
    constexpr uint32_t kColumns = 48;
    const auto count = static_cast<uint32_t>(items.size());
    const uint32_t rows = (count + kColumns - 1) / kColumns;
    const uint32_t width = kColumns * kCell;
    const uint32_t height = rows * kCell;

    mcpp::gpu::Device device;
    device.create({});
    {
        mcpp::gpu::WorldRenderer sheet(device, width, height, atlas);
        mcpp::gpu::UiRenderer quads(device, sheet.colour_format(),
                                    mcpp::gpu::UiRenderer::default_items_path(kProtocol),
                                    mcpp::gpu::UiRenderer::default_gui_path(kProtocol));
        if (!quads.has_icons()) {
            std::cerr << "no item icons; bake them with tools/bake_items.py\n";
            return 1;
        }
        quads.begin();
        quads.fill(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                   0x202020FFu);
        for (uint32_t id = 0; id < count; ++id) {
            const float x = static_cast<float>((id % kColumns) * kCell + 2);
            const float y = static_cast<float>((id / kColumns) * kCell + 2);
            quads.icon(static_cast<int32_t>(id), x, y, 16.0f);
        }
        const auto start = Clock::now();
        const auto pixels = sheet.render({}, [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
            quads.record(command, w, h);
        });
        const auto taken = Clock::now() - start;
        const auto path = out_dir / "items-all.png";
        mcpp::write_png(path.string(), width, height, pixels);

        // Whatever the sheet's own background came out as: a cell that is
        // nothing but that drew no icon.
        const uint8_t background[3] = {pixels[0], pixels[1], pixels[2]};
        std::vector<std::string> blank;
        for (uint32_t id = 0; id < count; ++id) {
            const uint32_t cell_x = (id % kColumns) * kCell + 2;
            const uint32_t cell_y = (id / kColumns) * kCell + 2;
            bool drawn = false;
            for (uint32_t y = 0; y < 16 && !drawn; ++y) {
                for (uint32_t x = 0; x < 16; ++x) {
                    const size_t at = (size_t{cell_y + y} * width + cell_x + x) * 4;
                    if (pixels[at] != background[0] || pixels[at + 1] != background[1]
                        || pixels[at + 2] != background[2]) {
                        drawn = true;
                        break;
                    }
                }
            }
            if (!drawn) blank.emplace_back(items.name(static_cast<int32_t>(id)));
        }
        std::cout << std::format("{}: {} items, {} drawn, {} with no icon\n", path.string(),
                                 count, count - blank.size(), blank.size());
        for (const auto& name : blank) std::cout << std::format("  {}\n", name);
        std::cout << std::format("  {} items in one frame: {:.2f} ms gpu, {:.2f} ms with the "
                                 "readback\n",
                                 count, sheet.last_gpu_ms(), millis(taken));
    }

    // Every creative tab, as the screen draws it.
    mcpp::ui::CreativeTabs tabs;
    if (!tabs.load(mcpp::ui::CreativeTabs::default_path(kProtocol), items)) {
        std::cerr << "no creative table; bake it with tools/bake_creative.py\n";
        return 1;
    }
    constexpr uint32_t kWidth = 951;
    constexpr uint32_t kHeight = 520;
    mcpp::gpu::WorldRenderer screen(device, kWidth, kHeight, atlas);
    mcpp::gpu::UiRenderer quads(device, screen.colour_format(),
                                mcpp::gpu::UiRenderer::default_items_path(kProtocol),
                                mcpp::gpu::UiRenderer::default_gui_path(kProtocol));
    const mcpp::ui::Font font(quads, &lang);
    const int scale = mcpp::ui::gui_scale(static_cast<int>(kWidth), static_cast<int>(kHeight),
                                          options.get_int("guiScale"), false);
    double slowest = 0.0;
    for (size_t index = 0; index < tabs.tabs.size(); ++index) {
        mcpp::ui::ScreenManager manager;
        manager.lang = &lang;
        manager.options = &options;
        manager.font = &font;
        manager.in_world = true;
        manager.resize(mcpp::ui::gui_extent(static_cast<int>(kWidth), scale),
                       mcpp::ui::gui_extent(static_cast<int>(kHeight), scale));
        auto page = std::make_shared<mcpp::ui::CreativeModeInventoryScreen>(&tabs, false);
        manager.set_screen(page);
        manager.apply_pending();
        page->select_tab(index);
        quads.begin();
        mcpp::ui::GuiGraphics gfx(quads, scale, manager.gui_width, manager.gui_height, &lang);
        gfx.pixel_width = static_cast<int>(kWidth);
        gfx.pixel_height = static_cast<int>(kHeight);
        const auto start = Clock::now();
        page->render_with_tooltip(gfx, -100.0, -100.0);
        const double laid_out = millis(Clock::now() - start);
        const auto pixels = screen.render({}, [&](VkCommandBuffer command, uint32_t w,
                                                  uint32_t h) { quads.record(command, w, h); });
        const auto path = out_dir / std::format("creative-{}.png", tabs.tabs[index].key);
        mcpp::write_png(path.string(), kWidth, kHeight, pixels);
        slowest = std::max(slowest, screen.last_gpu_ms());
        std::cout << std::format("- {}  {} items, {:.3f} ms laid out, {:.2f} ms gpu\n",
                                 path.string(), tabs.tabs[index].items.size(), laid_out,
                                 screen.last_gpu_ms());
    }
    std::cout << std::format("  the slowest tab cost {:.2f} ms on the gpu\n", slowest);
    return 0;
}

} // namespace client
