// The server as referee for digging: join as PlasmaB2, build a stone platform
// far from everyone, and break stone through the same game mode the window
// uses, by hand and with a pickaxe, in survival and in creative, one game tick
// at a time in real time. Counted: the ticks each break took against the
// oracle's count, and every block the server's acknowledgement put back.
#include "client/common.hpp"
#include "client/game_mode.hpp"
#include "client/player_rules.hpp"

#include "mcpp/protocol_versions.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <format>
#include <iostream>
#include <map>
#include <thread>

namespace client {

int digtest2(const std::string& target) {
    namespace w = mcpp::world;
    const auto address = parse_address(target);
    std::map<std::pair<int32_t, int32_t>, w::Chunk> world;
    w::Body body;
    bool placed = false;
    BlockPrediction prediction;
    size_t rollbacks = 0;
    size_t acks = 0;

    const auto at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        const auto found = world.find({x >> 4, z >> 4});
        if (found == world.end()) return -1;
        const int32_t index = found->second.index_of(y);
        if (index < 0 || index >= static_cast<int32_t>(found->second.sections.size()) * 16) return 0;
        return found->second.block_state(x & 15, index, z & 15);
    };
    const auto put = [&](int32_t x, int32_t y, int32_t z, int32_t state) {
        const auto found = world.find({x >> 4, z >> 4});
        if (found == world.end()) return false;
        return found->second.set_block_state(x & 15, found->second.index_of(y), z & 15, state);
    };

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    std::string username = "PlasmaB2";
    options.profile = signed_in_profile(username);
    options.username = username;
    mcpp::SessionEvents events;
    std::optional<mcpp::Session> session;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.world.on_chunk = [&](const w::Chunk& chunk) { world.insert_or_assign({chunk.x, chunk.z}, chunk); };
    events.world.on_block_change = [&](int32_t x, int32_t y, int32_t z, int32_t state) {
        const int32_t pos[3] = {x, y, z};
        if (!prediction.update(pos, state)) put(x, y, z, state);
    };
    events.player.on_block_changed_ack = [&](int32_t sequence) {
        ++acks;
        for (const auto& [pos, verified] : prediction.end_up_to(sequence)) {
            if (std::max(0, at(pos[0], pos[1], pos[2])) != verified.state) {
                ++rollbacks;
                std::cout << std::format("    the server put {} {} {} back to {}\n", pos[0], pos[1], pos[2],
                                         w::BlockShapes::game().name(verified.state));
                put(pos[0], pos[1], pos[2], verified.state);
            }
        }
    };
    events.player.on_teleport = [&](const mcpp::SessionEvents::Player::Teleport& t) {
        apply_teleport(body, t);
        placed = true;
        session->accept_teleport(t.id, body.x, body.y, body.z, body.yaw, body.pitch);
    };
    events.player.on_motion = [&](double x, double y, double z) {
        body.vx = x;
        body.vy = y;
        body.vz = z;
    };
    session.emplace(options, events);
    session->connect();
    const auto pump = [&] {
        while (session->has_data() || session->in_bundle()) {
            if (!session->pump()) return false;
        }
        return true;
    };
    const w::BlockAt block_at = at;
    const auto& shapes = w::BlockShapes::game();
    w::BlockTags tags;
    auto next = std::chrono::steady_clock::now();
    std::function<void()> each_tick;
    const auto tick = [&] {
        if (each_tick) each_tick();
        const w::Stats stats = player_stats(*session);
        w::tick(body, w::Keys{}, stats, w::World{block_at, shapes, &tags, session->dimension().min_y});
        send_tick(*session, body);
        next += std::chrono::milliseconds(50);
        std::this_thread::sleep_until(next);
        return pump();
    };
    const auto idle = [&](int ticks) {
        for (int i = 0; i < ticks; ++i) {
            if (!tick()) return false;
        }
        return true;
    };
    for (int i = 0; i < 400 && !(placed && world.size() > 20); ++i) {
        try {
            session->pump();
        } catch (const mcpp::TimeoutError&) {
        }
    }
    if (!placed) {
        std::cerr << "the server never placed us\n";
        return 1;
    }
    player_tags(*session, shapes, tags);
    next = std::chrono::steady_clock::now();
    // Through the server's console, which needs no operator: every @s is us.
    const char* console_path = std::getenv("DIGTEST_CONSOLE");
    const std::string console = console_path != nullptr
                                    ? console_path
                                    : std::string(std::getenv("HOME")) + "/plasma-testserver/console.in";
    const auto cmd = [&](std::string text) {
        for (size_t at_self = text.find("@s"); at_self != std::string::npos; at_self = text.find("@s")) {
            text.replace(at_self, 2, username);
        }
        std::ofstream(console) << text << '\n';
        idle(5);
    };

    constexpr int32_t kX = 15000, kY = 100, kZ = 15000;
    // A closed, lit stone room, so nothing wanders in while we dig.
    cmd(std::format("forceload add {} {} {} {}", kX - 8, kZ - 8, kX + 7, kZ + 7));
    cmd("gamemode creative @s");
    cmd(std::format("fill {} {} {} {} {} {} stone hollow", kX - 3, kY, kZ - 3, kX + 3, kY + 5, kZ + 3));
    cmd(std::format("setblock {} {} {} light[level=15]", kX, kY + 4, kZ));
    cmd(std::format("tp @s {} {} {} 0 30", kX + 0.5, kY + 1, kZ + 0.5));
    idle(60);
    cmd("effect clear @s");
    cmd("clear @s");
    session->send_held_slot(0);
    idle(20);

    const ToolTable& tools = ToolTable::game();
    GameModeLevel level;
    level.state_at = [&](const int32_t* p) { return std::max(0, at(p[0], p[1], p[2])); };
    level.set_block = [&](const int32_t* p, int32_t state) { return put(p[0], p[1], p[2], state); };
    level.break_effect = [](const int32_t*, int32_t) {};
    level.progress = [](const int32_t*, int32_t) {};
    level.hit_sound = [](const int32_t*, int32_t) {};
    level.held = [&]() -> const mcpp::ItemStack& {
        const auto& inventory = session->inventory();
        return inventory.hotbar(static_cast<size_t>(inventory.held));
    };
    level.destroy_progress = [&](int32_t state) {
        const auto& stack = level.held();
        const auto tool = tool_on_block(tools.find(stack.count > 0 ? stack.id : -1), state, shapes, session->tags());
        const auto& info = shapes.state(state);
        return destroy_progress(info.hardness, (info.flags & w::kNeedsTool) != 0, dig_context(*session, body, tool));
    };
    level.player_position = [&](double* out) {
        out[0] = body.x;
        out[1] = body.y;
        out[2] = body.z;
    };
    MultiPlayerGameMode mode(*session, prediction, level);
    const int32_t stone = shapes.find("minecraft:stone");
    const int32_t target_pos[3] = {kX, kY + 1, kZ + 2};
    const int32_t north = 2;

    // One survival break from the first press to the block gone: the ticks of
    // progress it took, which the oracle says for the same block and item.
    bool failed = false;
    const auto survival = [&](const std::string& name, const std::string& item, int expected) {
        cmd(std::format("setblock {} {} {} stone", target_pos[0], target_pos[1], target_pos[2]));
        cmd(item.empty() ? std::string("clear @s") : std::format("item replace entity @s hotbar.0 with {}", item));
        for (int i = 0; i < 100 && at(target_pos[0], target_pos[1], target_pos[2]) != stone; ++i) idle(1);
        if (at(target_pos[0], target_pos[1], target_pos[2]) != stone) {
            std::cout << std::format("- {:<28} the stone never arrived\n", name);
            failed = true;
            return;
        }
        idle(5);
        const size_t rolled = rollbacks;
        // `MultiPlayerGameMode.continueDestroyBlock` first waits out what is
        // left of the delay after the last break, as the game does.
        const int delay = mode.delay();
        expected += delay;
        int steps = 0;
        bool broke = false;
        each_tick = [&] {
            if (broke) return;
            if (steps == 0 && !mode.destroying()) mode.start_destroy_block(target_pos, north);
            if (mode.continue_destroy_block(target_pos, north)) {
                ++steps;
                session->send_swing();
            }
            broke = std::max(0, at(target_pos[0], target_pos[1], target_pos[2])) != stone;
        };
        for (int i = 0; i < 400 && !broke; ++i) idle(1);
        each_tick = nullptr;
        idle(20);
        const bool gone = std::max(0, at(target_pos[0], target_pos[1], target_pos[2])) != stone;
        const bool ok = broke && gone && steps == expected && rollbacks == rolled;
        failed = failed || !ok;
        std::cout << std::format("- {:<28} {} ticks (oracle {} + {} of delay), {}, {}\n", name, steps,
                                 expected - delay, delay,
                                 gone ? "the server kept it broken" : "the block is back",
                                 rollbacks == rolled ? "no rollback" : "ROLLED BACK");
    };
    cmd("gamemode survival @s");
    survival("stone by hand", "", 151);
    survival("stone with an iron pickaxe", "minecraft:iron_pickaxe", 8);
    survival("stone with a wooden pickaxe", "minecraft:wooden_pickaxe", 23);

    // The control: a dig stopped well before the game would, which the
    // server must refuse and put the block back.
    {
        cmd(std::format("setblock {} {} {} stone", target_pos[0], target_pos[1], target_pos[2]));
        cmd("item replace entity @s hotbar.0 with minecraft:iron_pickaxe");
        idle(10);
        const size_t rolled = rollbacks;
        mode.start_destroy_block(target_pos, north);
        idle(2);
        prediction.start(session->sequence() + 1);
        const int32_t pos[3] = {target_pos[0], target_pos[1], target_pos[2]};
        double where[3] = {body.x, body.y, body.z};
        if (put(pos[0], pos[1], pos[2], 0)) prediction.retain(pos, stone, where);
        session->send_dig_action(2, pos[0], pos[1], pos[2], north);
        prediction.stop();
        idle(40);
        const bool refused = rollbacks > rolled;
        failed = failed || !refused;
        std::cout << std::format("- {:<28} {}\n", "stopped after 3 ticks",
                                 refused ? "refused and put back, as it should" : "ACCEPTED: the referee is blind");
        mode.stop_destroy_block();
        idle(30);
    }

    // Creative: every block at once, then five ticks of nothing.
    {
        cmd("gamemode creative @s");
        cmd("clear @s");
        cmd(std::format("fill {} {} {} {} {} {} stone", kX - 1, kY + 1, kZ + 2, kX + 1, kY + 1, kZ + 2));
        for (int i = 0; i < 100 && at(kX + 1, kY + 1, kZ + 2) != stone; ++i) idle(1);
        idle(5);
        const size_t rolled = rollbacks;
        std::vector<int> breaks;
        int t = 0;
        const int32_t row[3][3] = {{kX - 1, kY + 1, kZ + 2}, {kX, kY + 1, kZ + 2}, {kX + 1, kY + 1, kZ + 2}};
        size_t which = 0;
        each_tick = [&] {
            ++t;
            if (which >= 3) return;
            const int32_t* p = row[which];
            const bool start = t == 1;
            if (start) mode.start_destroy_block(p, north);
            else mode.continue_destroy_block(p, north);
            if (std::max(0, at(p[0], p[1], p[2])) == 0) {
                breaks.push_back(t);
                ++which;
            }
        };
        idle(30);
        each_tick = nullptr;
        idle(20);
        const bool spaced = breaks.size() == 3 && breaks[1] - breaks[0] == 6 && breaks[2] - breaks[1] == 6;
        failed = failed || !spaced || rollbacks != rolled;
        std::cout << std::format("- {:<28} broke at ticks {}, {}\n", "creative, three in a row",
                                 breaks.size() == 3 ? std::format("{} {} {}", breaks[0], breaks[1], breaks[2]) : "?",
                                 rollbacks == rolled ? "no rollback" : "ROLLED BACK");
    }

    cmd("clear @s");
    cmd(std::format("fill {} {} {} {} {} {} air", kX - 3, kY, kZ - 3, kX + 3, kY + 5, kZ + 3));
    cmd(std::format("forceload remove {} {} {} {}", kX - 8, kZ - 8, kX + 7, kZ + 7));
    session->close();
    std::cout << std::format("- {} acknowledgements, {} rollbacks\n", acks, rollbacks);
    return failed ? 1 : 0;
}

} // namespace client
