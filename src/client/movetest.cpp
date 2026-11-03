// The server as referee for movement: join, build a course far from
// everyone, run each movement through the same tick and packets as the
// window, in real time, and count every place the server put us back.
#include "client/common.hpp"
#include "client/player_rules.hpp"

#include "mcpp/protocol_versions.hpp"

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace client {
namespace {

namespace w = mcpp::world;

struct Course {
    int32_t x = -7000;
    int32_t y = 150;
    int32_t z = 7000;
};

struct Scenario {
    std::string name;
    double start[3];
    float yaw = 0.0f;
    int ticks = 40;
    std::function<w::Keys(int)> keys;
};

} // namespace

int movetest(const std::string& target) {
    const auto address = parse_address(target);
    std::map<std::pair<int32_t, int32_t>, w::Chunk> world;
    std::vector<std::string> chat;
    w::Body body;
    bool placed = false;
    size_t corrections = 0;
    bool counting = false;

    mcpp::SessionOptions options;
    options.host = address.host;
    options.port = address.port;
    std::string username = "PlasmaE";
    options.profile = signed_in_profile(username);
    options.username = username;

    mcpp::SessionEvents events;
    std::optional<mcpp::Session> session;
    events.on_status = [](std::string_view line) { std::cout << "- " << line << '\n'; };
    events.world.on_chunk = [&](const w::Chunk& chunk) {
        world.insert_or_assign({chunk.x, chunk.z}, chunk);
    };
    events.world.on_block_change = [&](int32_t x, int32_t y, int32_t z, int32_t state) {
        const auto found = world.find({x >> 4, z >> 4});
        if (found == world.end()) return;
        found->second.set_block_state(x & 15, found->second.index_of(y), z & 15, state);
    };
    events.ui.on_chat = [&](std::string_view line) {
        chat.emplace_back(line);
        if (std::getenv("MOVETEST_CHAT") != nullptr) std::cout << "  chat: " << line << '\n';
    };
    events.player.on_teleport = [&](const mcpp::SessionEvents::Player::Teleport& t) {
        apply_teleport(body, t);
        placed = true;
        if (counting) {
            ++corrections;
            std::cout << std::format("    corrected to {:.4f} {:.4f} {:.4f}\n", body.x, body.y,
                                     body.z);
        }
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
    const auto at = [&](int32_t x, int32_t y, int32_t z) -> int32_t {
        const auto found = world.find({x >> 4, z >> 4});
        if (found == world.end()) return -1;
        const int32_t index = found->second.index_of(y);
        if (index < 0 || index >= static_cast<int32_t>(found->second.sections.size()) * 16) return 0;
        return found->second.block_state(x & 15, index, z & 15);
    };
    const w::BlockAt block_at = at;
    const auto& shapes = w::BlockShapes::game();
    w::BlockTags tags;

    // One game tick in real time: move, send, and read what came back.
    auto next = std::chrono::steady_clock::now();
    const auto tick = [&](const w::Keys& keys) {
        const w::Stats stats = player_stats(*session);
        const w::World level{block_at, shapes, &tags, session->dimension().min_y};
        w::tick(body, keys, stats, level);
        send_tick(*session, body);
        next += std::chrono::milliseconds(50);
        std::this_thread::sleep_until(next);
        return pump();
    };
    // Standing still; sneaking stays held where a scenario ended on it, or
    // the body slides the last few hundredths off the edge it stopped at.
    bool hold_shift = false;
    const auto idle = [&](int ticks) {
        w::Keys keys;
        keys.shift = hold_shift;
        for (int i = 0; i < ticks; ++i) {
            if (!tick(keys)) return false;
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

    const Course c;
    // The server kicks a player for more than about one command a second.
    const auto cmd = [&](const std::string& text) {
        session->send_command(text);
        idle(21);
    };
    cmd("gamemode creative");
    cmd(std::format("tp @s {} {} {}", c.x + 0.5, c.y + 2, c.z + 0.5));
    // The course needs its chunks loaded on the server before any fill.
    idle(120);
    // The course: a stone floor one below the lanes, open air above.
    cmd(std::format("fill {} {} {} {} {} {} stone", c.x - 2, c.y - 1, c.z - 2, c.x + 34, c.y - 1, c.z + 40));
    cmd(std::format("fill {} {} {} {} {} {} air", c.x - 2, c.y, c.z - 2, c.x + 34, c.y + 12, c.z + 40));
    cmd(std::format("fill {} {} {} {} {} {} stone", c.x - 2, c.y - 6, c.z - 2, c.x + 34, c.y - 2, c.z + 40));
    // Lane 9 ends at z+10 over a drop; lane 12 is packed ice.
    cmd(std::format("fill {} {} {} {} {} {} air", c.x + 8, c.y - 6, c.z + 11, c.x + 10, c.y - 1, c.z + 40));
    cmd(std::format("fill {} {} {} {} {} {} packed_ice", c.x + 11, c.y - 1, c.z, c.x + 13, c.y - 1, c.z + 40));
    // Lane 15: slabs, then stairs up.
    cmd(std::format("fill {} {} {} {} {} {} oak_slab[type=bottom]", c.x + 15, c.y, c.z + 3, c.x + 15, c.y, c.z + 8));
    cmd(std::format("setblock {} {} {} oak_stairs[facing=south]", c.x + 15, c.y, c.z + 9));
    cmd(std::format("fill {} {} {} {} {} {} stone", c.x + 15, c.y, c.z + 10, c.x + 15, c.y, c.z + 30));
    // Lane 18: a ladder up a wall.
    cmd(std::format("fill {} {} {} {} {} {} stone", c.x + 17, c.y, c.z + 4, c.x + 19, c.y + 10, c.z + 4));
    cmd(std::format("fill {} {} {} {} {} {} ladder[facing=north]", c.x + 18, c.y, c.z + 3, c.x + 18, c.y + 10, c.z + 3));
    // Lanes 21-23: a pool three deep.
    cmd(std::format("fill {} {} {} {} {} {} water", c.x + 21, c.y - 4, c.z, c.x + 23, c.y - 1, c.z + 30));
    // Lane 26: soul sand, a cobweb, honey, slime.
    cmd(std::format("fill {} {} {} {} {} {} soul_sand", c.x + 26, c.y - 1, c.z + 2, c.x + 26, c.y - 1, c.z + 5));
    cmd(std::format("setblock {} {} {} cobweb", c.x + 26, c.y, c.z + 8));
    cmd(std::format("fill {} {} {} {} {} {} honey_block", c.x + 26, c.y - 1, c.z + 11, c.x + 26, c.y - 1, c.z + 13));
    // Lane 29: slime to land on from high up.
    cmd(std::format("setblock {} {} {} slime_block", c.x + 29, c.y - 1, c.z + 2));
    // Monsters are not part of the test; a spider's knockback is.
    cmd("effect give @s resistance infinite 4 true");
    cmd("gamemode survival");
    idle(40);

    const auto lane = [&](int dx, double dy, double dz) {
        return std::array<double, 3>{c.x + dx + 0.5, c.y + dy, c.z + dz};
    };
    std::vector<Scenario> scenarios;
    const auto add = [&](std::string name, std::array<double, 3> start, float yaw, int ticks,
                         std::function<w::Keys(int)> keys) {
        Scenario s;
        s.name = std::move(name);
        std::copy(start.begin(), start.end(), s.start);
        s.yaw = yaw;
        s.ticks = ticks;
        s.keys = std::move(keys);
        scenarios.push_back(std::move(s));
    };
    add("walk", lane(0, 0, 0.5), 0.0f, 60, [](int i) {
        w::Keys k;
        k.forward = i < 40;
        return k;
    });
    add("sprint", lane(3, 0, 0.5), 0.0f, 60, [](int i) {
        w::Keys k;
        k.forward = i < 40;
        k.sprint = i < 40;
        return k;
    });
    add("sprint-jump", lane(6, 0, 0.5), 0.0f, 60, [](int i) {
        w::Keys k;
        k.forward = i < 40;
        k.sprint = i < 40;
        k.jump = i < 40;
        return k;
    });
    add("sneak to the edge", lane(9, 0, 6.5), 0.0f, 120, [](int) {
        w::Keys k;
        k.forward = true;
        k.shift = true;
        return k;
    });
    add("ice", lane(12, 0, 0.5), 0.0f, 80, [](int i) {
        w::Keys k;
        k.forward = i < 30;
        return k;
    });
    add("slabs and stairs", lane(15, 0, 0.5), 0.0f, 80, [](int i) {
        w::Keys k;
        k.forward = i < 60;
        k.jump = i > 30 && i < 60;
        return k;
    });
    add("ladder", lane(18, 0, 0.5), 0.0f, 80, [](int i) {
        w::Keys k;
        k.forward = i < 60;
        return k;
    });
    add("swim", lane(22, -3, 0.5), 0.0f, 80, [](int i) {
        w::Keys k;
        k.forward = i < 50;
        k.sprint = i < 50;
        return k;
    });
    add("soul sand, cobweb, honey", lane(26, 0, 0.5), 0.0f, 120, [](int i) {
        w::Keys k;
        k.forward = i < 100;
        return k;
    });
    add("slime landing", lane(29, 6, 2.5), 0.0f, 80, [](int) { return w::Keys{}; });

    size_t total = 0;
    for (const auto& s : scenarios) {
        cmd(std::format("tp @s {} {} {} {} 0", s.start[0], s.start[1], s.start[2], s.yaw));
        idle(30);
        corrections = 0;
        counting = true;
        for (int i = 0; i < s.ticks; ++i) {
            if (!tick(s.keys(i))) break;
        }
        hold_shift = s.keys(s.ticks - 1).shift;
        idle(50);
        counting = false;
        chat.clear();
        const double ours[3] = {body.x, body.y, body.z};
        session->send_command("data get entity @s Pos");
        idle(21);
        std::string server = "?";
        static const std::regex kPos(R"(\[(-?[0-9.E-]+)d, (-?[0-9.E-]+)d, (-?[0-9.E-]+)d\])");
        for (const auto& line : chat) {
            std::smatch m;
            if (std::regex_search(line, m, kPos)) {
                const double sx = std::stod(m[1]);
                const double sy = std::stod(m[2]);
                const double sz = std::stod(m[3]);
                server = std::format("server differs by {:.2e}",
                                     std::max({std::abs(sx - ours[0]), std::abs(sy - ours[1]),
                                               std::abs(sz - ours[2])}));
            }
        }
        hold_shift = false;
        total += corrections;
        std::cout << std::format("- {:<26} {} corrections, ended at {:.4f} {:.4f} {:.4f}, {}\n",
                                 s.name, corrections, ours[0] - c.x, ours[1] - c.y, ours[2] - c.z,
                                 server);
    }
    cmd("effect clear @s");
    cmd("gamemode creative");
    session->close();
    std::cout << std::format("- {} corrections in all\n", total);
    return total == 0 ? 0 : 1;
}

} // namespace client
