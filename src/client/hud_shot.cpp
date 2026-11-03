#include "client/hud_shot.hpp"

#include "mcpp/gpu/device.hpp"
#include "mcpp/gpu/ui.hpp"
#include "mcpp/gpu/world_renderer.hpp"
#include "mcpp/image_png.hpp"
#include "mcpp/options.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/ui/hud/hud.hpp"
#include "mcpp/ui/items.hpp"
#include "mcpp/ui/screen.hpp"
#include "mcpp/world/atlas.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>

namespace client {
namespace {

using mcpp::ui::hud::Gui;
using mcpp::ui::hud::Player;

struct Scene {
    const char* name;
    std::function<void(Gui&, Player&, mcpp::ui::hud::Frame&, mcpp::Scoreboard&,
                       std::vector<mcpp::ui::hud::TabEntry>&)>
        setup;
};

mcpp::nbt::Value text(std::string value) {
    return mcpp::nbt::Value(mcpp::nbt::Value::Storage(std::move(value)));
}

mcpp::nbt::Value coloured(std::string value, std::string colour) {
    mcpp::nbt::Compound out;
    out["text"] = text(std::move(value));
    out["color"] = text(std::move(colour));
    return mcpp::nbt::Value(mcpp::nbt::Value::Storage(std::move(out)));
}

} // namespace

int hud_shot(const std::vector<std::string>& args) {
    std::string which = args.empty() ? "all" : args[0];
    std::filesystem::path out_dir = args.size() > 1 ? args[1] : ".";
    uint32_t width = args.size() > 3 ? static_cast<uint32_t>(std::stoul(args[2])) : 854;
    uint32_t height = args.size() > 3 ? static_cast<uint32_t>(std::stoul(args[3])) : 480;
    std::filesystem::create_directories(out_dir);

    mcpp::world::Atlas atlas;
    if (!atlas.open(mcpp::world::Atlas::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no atlas; bake one with tools/bake_all.py\n";
        return 1;
    }
    mcpp::gpu::Device device;
    device.create({});
    mcpp::gpu::WorldRenderer renderer(device, width, height, atlas);
    mcpp::gpu::UiRenderer quads(
        device, renderer.colour_format(),
        mcpp::gpu::UiRenderer::default_items_path(mcpp::protocol::kTargetProtocol),
        mcpp::gpu::UiRenderer::default_gui_path(mcpp::protocol::kTargetProtocol));
    mcpp::Translations lang;
    lang.load(mcpp::Translations::default_path());
    mcpp::Options options;
    mcpp::ui::ItemTable items;
    items.load(mcpp::ui::ItemTable::default_path(mcpp::protocol::kTargetProtocol));
    const auto id = [&](const char* name) { return items.find(name); };

    const auto base = [&](Player& p) {
        const char* names[9] = {"diamond_sword", "bow",        "cobblestone", "golden_apple",
                                "oak_planks",    "water_bucket", "torch",     "cooked_beef",
                                "ender_pearl"};
        const int counts[9] = {1, 1, 64, 3, 32, 1, 17, 12, 16};
        for (size_t i = 0; i < 9; ++i) p.hotbar[i] = {id(names[i]), counts[i]};
        p.selected = 0;
        p.experience = 0.45f;
        p.level = 7;
    };
    const auto named = [&](Player& p) {
        const auto& held = p.hotbar[static_cast<size_t>(p.selected)];
        mcpp::ui::Style style;
        style.colour = mcpp::ui::ItemTable::rarity_colour(items.rarity(held.id));
        p.selected_name = mcpp::ui::literal("", style);
        p.selected_name.extra.push_back(
            mcpp::ui::translatable(std::string(items.name_key(held.id))));
    };

    std::vector<Scene> scenes = {
        {"survival", [&](Gui&, Player& p, auto&, auto&, auto&) { base(p); }},
        {"hurt",
         [&](Gui&, Player& p, auto&, auto&, auto&) {
             base(p);
             p.health = 7.0f;
             p.absorption = 4.0f;
             p.food = 7;
             p.saturation = 0.0f;
             p.armour = 13;
             p.air = 150;
             p.eye_in_water = true;
             p.selected = 3;
             p.offhand = {id("shield"), 1};
         }},
        {"poison",
         [&](Gui&, Player& p, auto&, auto&, auto&) {
             base(p);
             p.health = 11.0f;
             p.effects = {{18, 0, 300, false, true}, {16, 0, 400, false, true}};
         }},
        {"hardcore_wither",
         [&](Gui&, Player& p, auto&, auto&, auto&) {
             base(p);
             p.hardcore = true;
             p.health = 3.0f;
             p.effects = {{19, 1, 100, false, true}};
         }},
        {"max_health",
         [&](Gui&, Player& p, auto&, auto&, auto&) {
             base(p);
             p.max_health = 40.0f;
             p.health = 33.0f;
             p.absorption = 16.0f;
             p.armour = 20;
             p.effects = {{9, 1, 600, false, true}};
         }},
        {"frozen_left",
         [&](Gui&, Player& p, auto&, auto&, auto&) {
             base(p);
             p.fully_frozen = true;
             p.health = 14.0f;
             p.left_handed = true;
             p.offhand = {id("totem_of_undying"), 1};
             p.attack_strength = 0.4f;
         }},
        {"wiki",
         [&](Gui&, Player& p, auto&, auto&, auto&) {
             p.hotbar[7] = {id("diamond_sword"), 1};
             p.selected = 8;
             p.armour = 20;
             p.air = 300;
             p.eye_in_water = true;
             p.experience = 0.05f;
         }},
        {"creative",
         [&](Gui&, Player& p, auto&, auto&, auto&) {
             base(p);
             p.game_mode = 1;
             p.selected = 3;
         }},
        {"effects",
         [&](Gui&, Player& p, auto&, auto&, auto&) {
             base(p);
             p.effects = {{0, 1, 1200, false, true},  {4, 0, 3000, false, true},
                          {10, 0, 150, false, true},  {1, 0, 900, false, true},
                          {14, 0, 2000, false, true}, {2, 0, 2000, true, true}};
         }},
        {"chat",
         [&](Gui& gui, Player& p, auto&, auto&, auto&) {
             base(p);
             const mcpp::ui::Font font(quads, &lang);
             gui.chat().add(mcpp::ui::from_nbt(coloured("PlasmaH joined the game", "yellow")), 1,
                            0, font, options, false);
             gui.chat().add(mcpp::ui::literal("<PlasmaH> hello there, this is a test line"), 2,
                            0, font, options, false);
             gui.chat().add(mcpp::ui::literal(
                                "<PlasmaH> a much longer message that has to wrap onto a second "
                                "line because the chat box is only three hundred and twenty "
                                "pixels wide"),
                            2, 0, font, options, false);
         }},
        {"title",
         [&](Gui& gui, Player& p, auto&, auto&, auto&) {
             base(p);
             gui.set_title(mcpp::ui::from_nbt(coloured("Round 1", "gold")));
             gui.set_subtitle(mcpp::ui::literal("Fight!"));
             gui.set_overlay_message(mcpp::ui::literal("Action bar text"), false);
             for (int i = 0; i < 20; ++i) gui.tick(p, options);
         }},
        {"boss_sidebar",
         [&](Gui& gui, Player& p, auto&, mcpp::Scoreboard& board, auto&) {
             base(p);
             mcpp::BossEvent boss;
             boss.uuid = "a";
             boss.name = coloured("Ender Dragon", "light_purple");
             boss.progress = 0.6f;
             boss.colour = 0;
             gui.boss_event(boss, 0);
             boss.uuid = "b";
             boss.name = text("Wither");
             boss.progress = 0.3f;
             boss.colour = 5;
             boss.overlay = 2;
             gui.boss_event(boss, 0);
             auto& objective = board.objectives["kills"];
             objective.display_name = coloured("Kills", "gold");
             board.display[mcpp::Scoreboard::kSidebar] = "kills";
             board.scores["PlasmaH"]["kills"].value = 12;
             board.scores["Steve"]["kills"].value = 4;
             board.scores["alex"]["kills"].value = 4;
             board.scores["#hidden"]["kills"].value = 99;
         }},
        {"tab",
         [&](Gui& gui, Player& p, mcpp::ui::hud::Frame& frame, auto&, auto& players) {
             base(p);
             frame.player_list_down = true;
             gui.set_tab_list(mcpp::ui::from_nbt(coloured("Plasma test server", "aqua")),
                              mcpp::ui::literal("footer line"), true, true);
             const char* names[] = {"PlasmaH", "Steve", "alex", "Notch"};
             const int32_t pings[] = {12, 180, 450, -1};
             for (int i = 0; i < 4; ++i) {
                 mcpp::ui::hud::TabEntry entry;
                 entry.uuid = names[i];
                 entry.name = names[i];
                 entry.latency = pings[i];
                 players.push_back(entry);
             }
         }},
    };

    for (auto& scene : scenes) {
        if (which != "all" && which != scene.name) continue;
        for (const int scale : {2, 3, 4}) {
            if (scale == 4 && height < 960) continue;
            Gui gui;
            Player player;
            mcpp::ui::hud::Frame frame;
            mcpp::Scoreboard board;
            std::vector<mcpp::ui::hud::TabEntry> players;
            frame.millis = 1000;
            frame.own_name = "PlasmaH";
            frame.scoreboard = &board;
            frame.players = &players;
            scene.setup(gui, player, frame, board, players);
            named(player);
            gui.tick(player, options);
            const int gw = mcpp::ui::gui_extent(static_cast<int>(width), scale);
            const int gh = mcpp::ui::gui_extent(static_cast<int>(height), scale);
            const auto build = [&] {
                quads.begin();
                mcpp::ui::GuiGraphics gfx(quads, scale, gw, gh, &lang);
                gui.render(gfx, player, options, frame);
            };
            constexpr int kRuns = 200;
            const auto before = std::chrono::steady_clock::now();
            for (int i = 0; i < kRuns; ++i) build();
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - before).count() / kRuns;
            mcpp::gpu::Camera camera{};
            camera.eye[1] = 10000.0f;
            camera.target[0] = 1.0f;
            camera.target[1] = 10000.0f;
            const auto pixels = renderer.render(
                camera, [&](VkCommandBuffer command, uint32_t w, uint32_t h) {
                    quads.record(command, w, h);
                });
            const auto path = out_dir / std::format("hud-{}-s{}.png", scene.name, scale);
            mcpp::write_png(path, width, height, pixels);
            std::cout << std::format("{}  {:.4f} ms cpu to build the hud\n", path.string(), ms);
        }
    }
    device.save_pipeline_cache();
    return 0;
}

} // namespace client
