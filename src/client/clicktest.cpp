#include "client/screenshot.hpp"

#include "client/common.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/ui/container.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <thread>

namespace client {
namespace {

void pump_for(mcpp::Session& session, double seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until) {
        while (session.has_data() || session.in_bundle()) {
            try {
                if (!session.pump()) break;
            } catch (const mcpp::TimeoutError&) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

std::string describe(const mcpp::ItemStack& stack, const mcpp::ui::ItemTable& items) {
    if (stack.id <= 0 || stack.count <= 0) return "-";
    return std::format("{}x{}", stack.count, items.name(stack.id));
}

} // namespace

// The server is the referee: every click is predicted the game's way, sent with
// the slots it changed, and the inventory the server then sends back must be
// the one predicted. A wrong prediction shows as a slot the server corrects.
int clicktest(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "usage: clicktest <host:port> <server console fifo> [name]\n";
        return 2;
    }
    const auto address = parse_address(args[0]);
    const std::filesystem::path fifo = args[1];
    const std::string name = args.size() > 2 ? args[2] : "PlasmaC";
    const auto console = [&](const std::string& command) {
        std::ofstream(fifo) << command << '\n';
    };

    mcpp::ui::ItemTable items;
    if (!items.load(mcpp::ui::ItemTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no item table; bake one with tools/bake_all.py\n";
        return 1;
    }
    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.username = name;
    mcpp::SessionEvents events;
    mcpp::Session session(options, events);
    session.connect();
    pump_for(session, 3.0);

    console(std::format("spreadplayers 23000 -23000 0 8 false {}", name));
    console(std::format("gamemode survival {}", name));
    console(std::format("clear {}", name));
    pump_for(session, 1.0);
    console(std::format("give {} minecraft:stone 40", name));
    console(std::format("give {} minecraft:oak_planks 20", name));
    console(std::format("give {} minecraft:iron_helmet 1", name));
    console(std::format("give {} minecraft:cobblestone 64", name));
    pump_for(session, 1.5);

    auto menu = mcpp::ui::ContainerMenu::player_inventory(&items);
    using mcpp::ui::ClickType;
    struct Step {
        const char* what;
        int slot;
        int button;
        ClickType type;
    };
    // Hotbar slot 36 holds the stone, 37 the planks, 38 the helmet, 39 the
    // cobblestone, as /give fills them.
    const std::vector<Step> steps = {
        {"pick up the stone", 36, 0, ClickType::Pickup},
        {"put it in the first main slot", 9, 0, ClickType::Pickup},
        {"take half with a right click", 9, 1, ClickType::Pickup},
        {"drop one into an empty slot", 10, 1, ClickType::Pickup},
        {"merge the rest back", 9, 0, ClickType::Pickup},
        {"pick up the planks", 37, 0, ClickType::Pickup},
        {"swap them with the stone", 9, 0, ClickType::Pickup},
        {"put the stone down again", 37, 0, ClickType::Pickup},
        {"shift-click the helmet onto the head", 38, 0, ClickType::QuickMove},
        {"shift-click the helmet back", 5, 0, ClickType::QuickMove},
        {"number key 5 over the planks", 9, 4, ClickType::Swap},
        {"off hand key over the stone", 37, 40, ClickType::Swap},
        {"pick up the cobblestone", 39, 0, ClickType::Pickup},
        {"start a drag split", -999, mcpp::ui::ContainerMenu::quick_craft_mask(0, 0), ClickType::QuickCraft},
        {"drag over slot 20", 20, mcpp::ui::ContainerMenu::quick_craft_mask(1, 0), ClickType::QuickCraft},
        {"drag over slot 21", 21, mcpp::ui::ContainerMenu::quick_craft_mask(1, 0), ClickType::QuickCraft},
        {"drag over slot 22", 22, mcpp::ui::ContainerMenu::quick_craft_mask(1, 0), ClickType::QuickCraft},
        {"end the drag split", -999, mcpp::ui::ContainerMenu::quick_craft_mask(2, 0), ClickType::QuickCraft},
        {"put the remainder down", 39, 0, ClickType::Pickup},
        {"pick up one of the split stacks", 20, 0, ClickType::Pickup},
        {"double click gathers the cobblestone", 20, 0, ClickType::PickupAll},
        {"put it back", 39, 0, ClickType::Pickup},
        {"throw the lone stone with the drop key", 10, 0, ClickType::Throw},
    };

    {
        std::string held;
        for (size_t i = 0; i < mcpp::Inventory::kSlots; ++i) {
            const auto& stack = session.inventory().slots[i];
            if (stack.count > 0) held += std::format(" {}={}", i, describe(stack, items));
        }
        std::cout << "start:" << held << '\n';
    }
    int mismatches = 0;
    for (const auto& step : steps) {
        const auto& inventory = session.inventory();
        for (size_t i = 0; i < mcpp::Inventory::kSlots; ++i) menu.slots[i] = inventory.slots[i];
        menu.carried = inventory.carried;
        const auto before = menu.slots;
        menu.clicked(step.slot, step.button, step.type);
        const auto changed = menu.changed_since(before);
        session.send_container_click(0, static_cast<int16_t>(step.slot),
                                     static_cast<int8_t>(step.button),
                                     static_cast<int32_t>(step.type), changed, menu.carried);
        std::array<mcpp::ItemStack, mcpp::Inventory::kSlots> predicted{};
        for (size_t i = 0; i < predicted.size(); ++i) predicted[i] = menu.slots[i];
        session.predict_inventory(predicted, menu.carried);
        pump_for(session, 0.35);

        const auto& after = session.inventory();
        std::string wrong;
        for (size_t i = 0; i < mcpp::Inventory::kSlots; ++i) {
            const auto& a = predicted[i];
            const auto& b = after.slots[i];
            const bool same = (a.count <= 0 && b.count <= 0) || (a.id == b.id && a.count == b.count);
            if (!same) {
                wrong += std::format(" slot {}: predicted {}, server {};", i, describe(a, items),
                                     describe(b, items));
            }
        }
        const auto& c = menu.carried;
        const auto& d = after.carried;
        if (!((c.count <= 0 && d.count <= 0) || (c.id == d.id && c.count == d.count))) {
            wrong += std::format(" cursor: predicted {}, server {};", describe(c, items),
                                 describe(d, items));
        }
        std::cout << std::format("{:<40} {} changed {}{}\n", step.what,
                                 wrong.empty() ? "agrees " : "DIFFERS", changed.size(), wrong);
        if (!wrong.empty()) ++mismatches;
        if (std::getenv("PLASMA_CLICKTEST_VERBOSE") != nullptr) {
            std::string held;
            for (size_t i = 0; i < mcpp::Inventory::kSlots; ++i) {
                if (after.slots[i].count > 0) held += std::format(" {}={}", i, describe(after.slots[i], items));
            }
            std::cout << "   now:" << held << " cursor=" << describe(after.carried, items) << '\n';
        }
    }
    console(std::format("clear {}", name));
    pump_for(session, 0.3);
    session.close();
    std::cout << std::format("{} of {} clicks predicted exactly as the server played them\n",
                             steps.size() - static_cast<size_t>(mismatches), steps.size());
    return mismatches == 0 ? 0 : 1;
}

} // namespace client
