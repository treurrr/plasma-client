#include "client/crafttest.hpp"

#include "client/common.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/ui/container.hpp"
#include "mcpp/ui/container_screen.hpp"
#include "mcpp/ui/world.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <thread>

namespace client {
namespace {

// Where the server last put the player: the test stands there, on the ground,
// and says so every tick, or the server takes it for flying.
struct Standing {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::chrono::steady_clock::time_point next_tick{};
    bool known = false;
};

void pump_for(mcpp::Session& session, Standing& at, double seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until) {
        while (session.has_data() || session.in_bundle()) {
            try {
                if (!session.pump()) break;
            } catch (const mcpp::TimeoutError&) {
                break;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (at.known && now >= at.next_tick) {
            at.next_tick = now + std::chrono::milliseconds(50);
            session.send_position(at.x, at.y, at.z, 0.0f, 0.0f, true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

std::string describe(const mcpp::ItemStack& stack, const mcpp::ui::ItemTable& items) {
    if (stack.id <= 0 || stack.count <= 0) return "-";
    return std::format("{}x{}", stack.count, items.name(stack.id));
}

bool same(const mcpp::ItemStack& a, const mcpp::ItemStack& b) {
    return (a.count <= 0 && b.count <= 0) || (a.id == b.id && a.count == b.count);
}

// What the server's own copy of the player holds, item by item, from the
// console's answer to `data get entity <name> Inventory`.
std::map<std::string, int> server_inventory(const std::filesystem::path& log, size_t from) {
    std::ifstream in(log);
    in.seekg(static_cast<std::streamoff>(from));
    std::string line;
    std::string last;
    while (std::getline(in, line)) {
        if (line.find("has the following entity data") != std::string::npos) last = line;
    }
    std::map<std::string, int> out;
    static const std::regex entry(R"(count:\s*(\d+)[^}]*?id:\s*"minecraft:([a-z_]+)\")");
    for (auto it = std::sregex_iterator(last.begin(), last.end(), entry);
         it != std::sregex_iterator(); ++it) {
        out[(*it)[2].str()] += std::stoi((*it)[1].str());
    }
    return out;
}

} // namespace

// The server is the referee for crafting: planks from the console, sticks
// made in the inventory's 2x2 grid by a click and by a shift-click, and a
// pickaxe at a crafting table. Every click is predicted the game's way; what
// counts is what the server then says the player has.
int crafttest(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "usage: crafttest <host:port> <server console dir> [name] [x y z]\n";
        return 2;
    }
    const auto address = parse_address(args[0]);
    const std::filesystem::path server = args[1];
    const std::string name = args.size() > 2 ? args[2] : "PlasmaK";
    const int bx = args.size() > 5 ? std::stoi(args[3]) : -15000;
    const int by = args.size() > 5 ? std::stoi(args[4]) : 100;
    const int bz = args.size() > 5 ? std::stoi(args[5]) : 15000;
    const auto console = [&](const std::string& command) {
        std::ofstream(server / "console.in") << command << '\n';
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
    int32_t opened_type = -1;
    events.ui.on_open_screen = [&](int32_t, int32_t type, const mcpp::nbt::Value&) {
        opened_type = type;
    };
    Standing standing;
    events.player.on_position = [&](double x, double y, double z, float, float) {
        standing.x = x;
        standing.y = y;
        standing.z = z;
        standing.known = true;
    };
    mcpp::Session session(options, events);
    session.connect();
    pump_for(session, standing, 3.0);

    console(std::format("gamemode survival {}", name));
    console(std::format("tp {} {} {} {}", name, bx + 0.5, by, bz + 0.5));
    pump_for(session, standing, 1.5);
    console(std::format("setblock {} {} {} minecraft:stone", bx, by - 1, bz));
    console(std::format("setblock {} {} {} minecraft:stone", bx + 2, by - 1, bz));
    console(std::format("setblock {} {} {} minecraft:crafting_table", bx + 2, by, bz));
    console(std::format("clear {}", name));
    pump_for(session, standing, 1.0);
    console(std::format("give {} minecraft:oak_planks 16", name));
    pump_for(session, standing, 1.5);

    using mcpp::ui::ClickType;
    int failures = 0;
    size_t steps = 0;
    const auto check = [&](const char* what, bool ok, const std::string& detail) {
        ++steps;
        if (!ok) ++failures;
        std::cout << std::format("{:<52} {} {}\n", what, ok ? "ok     " : "WRONG  ", detail);
    };

    // One click, predicted on `menu`, sent, and compared with what the server
    // sends back. A shift-click on a result crafts as often as it can on the
    // server, while the client - vanilla's too - predicts one craft.
    const auto click = [&](mcpp::ui::ContainerMenu& menu, bool open, const char* what, int slot,
                           int button, ClickType type, bool prediction_exact) {
        if (open) {
            const auto& held = session.container_slots();
            for (size_t i = 0; i < menu.size() && i < held.size(); ++i) menu.slots[i] = held[i];
        } else {
            for (size_t i = 0; i < mcpp::Inventory::kSlots; ++i) {
                menu.slots[i] = session.inventory().slots[i];
            }
        }
        menu.carried = session.inventory().carried;
        const auto before = menu.slots;
        menu.clicked(slot, button, type);
        const auto changed = menu.changed_since(before);
        session.send_container_click(menu.container_id, static_cast<int16_t>(slot),
                                     static_cast<int8_t>(button), static_cast<int32_t>(type),
                                     changed, menu.carried);
        const auto predicted = menu.slots;
        const auto predicted_carried = menu.carried;
        if (open) {
            session.predict_container(predicted, predicted_carried);
        } else {
            std::array<mcpp::ItemStack, mcpp::Inventory::kSlots> all{};
            for (size_t i = 0; i < all.size(); ++i) all[i] = predicted[i];
            session.predict_inventory(all, predicted_carried);
        }
        pump_for(session, standing, 0.4);
        std::string wrong;
        // The result slot is the server's to fill; it is checked on its own.
        for (size_t i = 1; i < predicted.size(); ++i) {
            const auto& b = open ? session.container_slots()[i] : session.inventory().slots[i];
            if (!same(predicted[i], b)) {
                wrong += std::format(" slot {}: predicted {}, server {};", i,
                                     describe(predicted[i], items), describe(b, items));
            }
        }
        if (!same(predicted_carried, session.inventory().carried)) {
            wrong += std::format(" cursor: predicted {}, server {};",
                                 describe(predicted_carried, items),
                                 describe(session.inventory().carried, items));
        }
        if (prediction_exact) {
            check(what, wrong.empty(), wrong);
        } else {
            std::cout << std::format("{:<52} (one craft predicted){}\n", what, wrong);
        }
    };
    const auto expect_slot = [&](const char* what, const mcpp::ItemStack& stack,
                                 std::string_view item, int count) {
        const bool ok = count == 0 ? stack.count <= 0
                                   : stack.count == count && items.name(stack.id) == item;
        check(what, ok, std::format("server has {}", describe(stack, items)));
    };

    // The inventory: hotbar slot 36 holds the 16 planks.
    auto inventory = mcpp::ui::ContainerMenu::player_inventory(&items);
    const auto& inv = session.inventory();
    click(inventory, false, "pick up the planks", 36, 0, ClickType::Pickup, true);
    click(inventory, false, "one plank into the grid's top left", 1, 1, ClickType::Pickup, true);
    click(inventory, false, "one plank into the grid's bottom left", 3, 1, ClickType::Pickup, true);
    click(inventory, false, "the rest back to the hotbar", 36, 0, ClickType::Pickup, true);
    expect_slot("the server shows four sticks as the result", inv.slots[0], "stick", 4);
    click(inventory, false, "take the sticks with a click", 0, 0, ClickType::Pickup, true);
    expect_slot("the cursor holds four sticks", inv.carried, "stick", 4);
    expect_slot("the grid is used up", inv.slots[1], "", 0);
    click(inventory, false, "put the sticks in the hotbar", 37, 0, ClickType::Pickup, true);
    click(inventory, false, "pick up the planks again", 36, 0, ClickType::Pickup, true);
    click(inventory, false, "two planks top left", 1, 1, ClickType::Pickup, true);
    click(inventory, false, "and a second", 1, 1, ClickType::Pickup, true);
    click(inventory, false, "two planks bottom left", 3, 1, ClickType::Pickup, true);
    click(inventory, false, "and a second", 3, 1, ClickType::Pickup, true);
    click(inventory, false, "the rest back", 36, 0, ClickType::Pickup, true);
    expect_slot("four sticks offered again", inv.slots[0], "stick", 4);
    click(inventory, false, "shift-click the result", 0, 0, ClickType::QuickMove, false);
    pump_for(session, standing, 0.4);
    expect_slot("both crafts made: twelve sticks", inv.slots[37], "stick", 12);
    expect_slot("the grid is empty", inv.slots[1], "", 0);
    expect_slot("ten planks left", inv.slots[36], "oak_planks", 10);

    // The crafting table.
    session.send_place(bx + 2, by, bz, 1, 0.5f, 1.0f, 0.5f);
    pump_for(session, standing, 1.0);
    const auto* type = session.registries().at("minecraft:menu", opened_type);
    check("the table opens a crafting menu",
          type != nullptr && type->name == "minecraft:crafting" && session.open_container() != 0,
          std::format("type {}, container {}", type != nullptr ? type->name : "none",
                      session.open_container()));
    auto table = mcpp::ui::ContainerMenu::crafting_table(&items);
    table.container_id = session.open_container();
    // Hotbar slots 0 and 1 are menu slots 37 and 38 here.
    const auto& open = session.container_slots();
    click(table, true, "pick up the planks", 37, 0, ClickType::Pickup, true);
    click(table, true, "a plank top left", 1, 1, ClickType::Pickup, true);
    click(table, true, "a plank top middle", 2, 1, ClickType::Pickup, true);
    click(table, true, "a plank top right", 3, 1, ClickType::Pickup, true);
    click(table, true, "planks back", 37, 0, ClickType::Pickup, true);
    click(table, true, "pick up the sticks", 38, 0, ClickType::Pickup, true);
    click(table, true, "a stick in the middle", 5, 1, ClickType::Pickup, true);
    click(table, true, "a stick at the bottom", 8, 1, ClickType::Pickup, true);
    click(table, true, "sticks back", 38, 0, ClickType::Pickup, true);
    if (open.size() == 46) {
        expect_slot("the server offers a wooden pickaxe", open[0], "wooden_pickaxe", 1);
    } else {
        check("the server sent the table's slots", false, std::format("{} slots", open.size()));
    }
    click(table, true, "take the pickaxe with a click", 0, 0, ClickType::Pickup, true);
    expect_slot("the cursor holds the pickaxe", inv.carried, "wooden_pickaxe", 1);
    click(table, true, "put it in the hotbar", 39, 0, ClickType::Pickup, true);
    session.send_container_close(table.container_id);
    pump_for(session, standing, 1.0);

    const auto log = server / "console.out";
    const auto from = std::filesystem::exists(log) ? std::filesystem::file_size(log) : 0;
    console(std::format("data get entity {} Inventory", name));
    pump_for(session, standing, 1.0);
    auto held = server_inventory(log, from);
    check("the server's player: 1 wooden pickaxe", held["wooden_pickaxe"] == 1,
          std::format("{}", held["wooden_pickaxe"]));
    check("the server's player: 10 sticks", held["stick"] == 10, std::format("{}", held["stick"]));
    check("the server's player: 7 planks", held["oak_planks"] == 7,
          std::format("{}", held["oak_planks"]));
    expect_slot("our inventory: the pickaxe in hotbar 2", inv.slots[38], "wooden_pickaxe", 1);

    console(std::format("clear {}", name));
    console(std::format("setblock {} {} {} minecraft:air", bx + 2, by, bz));
    console(std::format("setblock {} {} {} minecraft:air", bx + 2, by - 1, bz));
    console(std::format("setblock {} {} {} minecraft:air", bx, by - 1, bz));
    pump_for(session, standing, 0.3);
    session.close();
    std::cout << std::format("{} of {} checks agree with the server\n",
                             steps - static_cast<size_t>(failures), steps);
    return failures == 0 ? 0 : 1;
}

// The recipe book against the server: every recipe it unlocks is in the book,
// what two planks can make is drawn craftable and the rest is not, a click on
// the sticks has the server fill the grid, and a click on a recipe the
// inventory lacks the items for brings back its ghost.
int booktest(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "usage: booktest <host:port> <server console dir> [name] [x y z]\n";
        return 2;
    }
    const auto address = parse_address(args[0]);
    const std::filesystem::path server = args[1];
    const std::string name = args.size() > 2 ? args[2] : "PlasmaK";
    const int bx = args.size() > 5 ? std::stoi(args[3]) : -15000;
    const int by = args.size() > 5 ? std::stoi(args[4]) : 100;
    const int bz = args.size() > 5 ? std::stoi(args[5]) : 15000;
    const auto console = [&](const std::string& command) {
        std::ofstream(server / "console.in") << command << '\n';
    };

    mcpp::ui::ItemTable items;
    if (!items.load(mcpp::ui::ItemTable::default_path(mcpp::protocol::kTargetProtocol))) {
        std::cerr << "no item table; bake one with tools/bake_all.py\n";
        return 1;
    }
    auto menu = mcpp::ui::ContainerMenu::player_inventory(&items);
    mcpp::ui::World world;
    world.menu = &menu;
    world.items = &items;
    size_t sent_entries = 0;
    std::vector<int32_t> placed;
    size_t ghosts = 0;
    mcpp::ui::RecipeBookComponent book;

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    options.username = name;
    mcpp::SessionEvents events;
    Standing standing;
    events.player.on_position = [&](double x, double y, double z, float, float) {
        standing.x = x;
        standing.y = y;
        standing.z = z;
        standing.known = true;
    };
    mcpp::Session* live = nullptr;
    events.ui.on_recipe_book_add = [&](const mcpp::proto::Value& added) {
        const auto* replace = added.field("replace");
        if (replace != nullptr && replace->as_bool().value_or(false)) sent_entries = 0;
        if (const auto* entries = added.field("entries"); entries && entries->as_list()) {
            sent_entries += entries->as_list()->size();
        }
        world.recipe_book.add_batch(added, live->registries(), live->tags());
        world.recipe_book.rebuild_collections();
        book.recipes_updated();
    };
    events.ui.on_recipe_book_remove = [&](const std::vector<int32_t>& gone) {
        for (const int32_t id : gone) world.recipe_book.remove(id);
        sent_entries -= gone.size();
        world.recipe_book.rebuild_collections();
        book.recipes_updated();
    };
    events.ui.on_place_ghost_recipe = [&](int32_t, const mcpp::proto::Value& display) {
        book.fill_ghost_recipe(mcpp::ui::read_recipe_display(display, live->tags()));
        ghosts = book.ghost_slots();
    };
    mcpp::Session session(options, events);
    live = &session;
    world.place_recipe = [&](int32_t container, int32_t recipe, bool use_max) {
        placed.push_back(recipe);
        session.send_place_recipe(container, recipe, use_max);
    };
    session.connect();
    pump_for(session, standing, 3.0);
    console(std::format("gamemode survival {}", name));
    console(std::format("tp {} {} {} {}", name, bx + 0.5, by, bz + 0.5));
    pump_for(session, standing, 1.5);
    console(std::format("setblock {} {} {} minecraft:stone", bx, by - 1, bz));
    console(std::format("clear {}", name));
    console(std::format("recipe take {} *", name));
    pump_for(session, standing, 1.0);
    console(std::format("recipe give {} *", name));
    console(std::format("give {} minecraft:oak_planks 2", name));
    pump_for(session, standing, 2.0);

    int failures = 0;
    size_t steps = 0;
    const auto check = [&](const std::string& what, bool ok, const std::string& detail) {
        ++steps;
        if (!ok) ++failures;
        std::cout << std::format("{:<56} {} {}\n", what, ok ? "ok     " : "WRONG  ", detail);
    };
    const auto sync = [&] {
        for (size_t i = 0; i < mcpp::Inventory::kSlots; ++i) menu.slots[i] = session.inventory().slots[i];
        menu.carried = session.inventory().carried;
    };
    sync();
    world.recipe_book.open = true;
    book.init(430, 240, &world, nullptr, false);
    book.tick();

    check("every recipe the server sent is in the book", world.recipe_book.size() == sent_entries,
          std::format("{} in the book, {} sent", world.recipe_book.size(), sent_entries));
    size_t collections = world.recipe_book.collection(0).size();
    check("the crafting tabs hold collections", collections > 100,
          std::format("{} collections", collections));

    // Every page of the search tab, walked with the forward arrow.
    const int32_t stick = items.find("stick");
    const int32_t table = items.find("crafting_table");
    int stick_page = -1;
    int stick_button = -1;
    int table_page = -1;
    int table_button = -1;
    size_t shown = 0;
    size_t craftable = 0;
    const int pages = book.pages();
    for (int page = 0; page < pages; ++page) {
        const auto on_page = book.page();
        for (size_t i = 0; i < on_page.size(); ++i) {
            ++shown;
            if (on_page[i].craftable) ++craftable;
            if (on_page[i].result == stick && stick_page < 0) {
                stick_page = page;
                stick_button = static_cast<int>(i);
                check("the sticks are drawn craftable", on_page[i].craftable, "");
            }
            if (on_page[i].result == table && table_page < 0) {
                table_page = page;
                table_button = static_cast<int>(i);
                check("the crafting table is drawn uncraftable (red)", !on_page[i].craftable, "");
            }
        }
        if (page + 1 < pages) {
            const auto [ax, ay] = book.button_position(0);
            book.mouse_clicked(ax - 11 + 93 + 2, ay - 31 + 137 + 2, 0, false);
        }
    }
    check("the pages show every collection that fits the 2x2 grid", shown > 0 && pages > 1,
          std::format("{} collections on {} pages, {} craftable", shown, pages, craftable));
    check("found the sticks and the crafting table", stick_page >= 0 && table_page >= 0, "");

    const auto to_page = [&](int page) {
        const auto [ax, ay] = book.button_position(0);
        while (book.page_index() > page) book.mouse_clicked(ax - 11 + 38 + 2, ay - 31 + 137 + 2, 0, false);
        while (book.page_index() < page) book.mouse_clicked(ax - 11 + 93 + 2, ay - 31 + 137 + 2, 0, false);
    };
    // The filter shows only what can be made.
    {
        const auto [fx, fy] = book.button_position(0);
        book.mouse_clicked(fx - 11 + 110 + 2, fy - 31 + 12 + 2, 0, false);
        to_page(0);
        size_t filtered = 0;
        bool all_craftable = true;
        for (int page = 0; page < book.pages(); ++page) {
            for (const auto& entry : book.page()) {
                ++filtered;
                all_craftable = all_craftable && entry.craftable;
            }
            if (page + 1 < book.pages()) {
                const auto [ax, ay] = book.button_position(0);
                book.mouse_clicked(ax - 11 + 93 + 2, ay - 31 + 137 + 2, 0, false);
            }
        }
        check("the craftable filter keeps exactly the craftable ones",
              filtered == craftable && all_craftable,
              std::format("{} shown, {} craftable before", filtered, craftable));
        book.mouse_clicked(fx - 11 + 110 + 2, fy - 31 + 12 + 2, 0, false);
    }

    // Back to the stick's page and click it.
    const auto go_to = [&](int page) {
        sync();
        book.tick();
        to_page(page);
    };
    if (stick_page >= 0) {
        go_to(stick_page);
        const auto [sx, sy] = book.button_position(stick_button);
        book.mouse_clicked(sx + 5, sy + 5, 0, false);
        pump_for(session, standing, 1.0);
        const auto& inv = session.inventory();
        check("a click sends place_recipe", placed.size() == 1, std::format("{}", placed.size()));
        const bool grid = inv.slots[1].count == 1 && items.name(inv.slots[1].id) == "oak_planks" &&
                          inv.slots[3].count == 1 && items.name(inv.slots[3].id) == "oak_planks";
        check("the server put the planks in the grid", grid,
              std::format("{} {} / {} {}", describe(inv.slots[1], items), describe(inv.slots[2], items),
                          describe(inv.slots[3], items), describe(inv.slots[4], items)));
        check("and offers four sticks", inv.slots[0].count == 4 && inv.slots[0].id == stick,
              describe(inv.slots[0], items));
    }
    if (table_page >= 0) {
        sync();
        go_to(table_page);
        const auto [tx, ty] = book.button_position(table_button);
        book.mouse_clicked(tx + 5, ty + 5, 0, false);
        pump_for(session, standing, 1.0);
        check("a recipe the inventory cannot make comes back as a ghost", ghosts == 5,
              std::format("{} ghost slots (4 planks and the result)", ghosts));
    }

    console(std::format("recipe take {} *", name));
    console(std::format("clear {}", name));
    console(std::format("setblock {} {} {} minecraft:air", bx, by - 1, bz));
    pump_for(session, standing, 0.5);
    session.close();
    std::cout << std::format("{} of {} checks agree with the server\n",
                             steps - static_cast<size_t>(failures), steps);
    return failures == 0 ? 0 : 1;
}

} // namespace client
