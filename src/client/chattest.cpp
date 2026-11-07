#include "client/chattest.hpp"

#include "client/common.hpp"
#include "mcpp/options.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"
#include "mcpp/translations.hpp"
#include "mcpp/ui/chat_screen.hpp"
#include "mcpp/ui/hud/hud.hpp"
#include "mcpp/ui/multiplayer.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
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

void type(mcpp::ui::ScreenManager& manager, std::string_view text) {
    for (const char c : text) {
        manager.character(static_cast<uint32_t>(static_cast<unsigned char>(c)), 0);
    }
}

} // namespace

// The server is the referee: a line typed into the chat screen is a line the
// server hands back to everyone, word for word.
int chattest(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "usage: chattest <host:port> [name]\n";
        return 2;
    }
    const auto address = parse_address(args[0]);
    const std::string username = args.size() > 1 ? args[1] : "PlasmaS";

    mcpp::Translations lang;
    lang.load(mcpp::Translations::default_path());
    mcpp::Options options;
    mcpp::ui::hud::Chat chat;

    std::mutex guard;
    std::vector<std::string> heard;

    mcpp::SessionOptions session_options;
    session_options.host = address.host;
    session_options.port = address.port;
    session_options.username = username;
    mcpp::SessionEvents events;
    events.ui.on_chat = [&](std::string_view line) {
        std::lock_guard lock(guard);
        heard.emplace_back(line);
    };
    mcpp::Session session(session_options, events);
    session.connect();
    pump_for(session, 2.5);

    mcpp::ui::ScreenManager manager;
    manager.lang = &lang;
    manager.options = &options;
    manager.in_world = true;
    manager.username = username;
    manager.host.chat = [&session](const std::string& line) { session.send_chat(line); };
    manager.host.command = [&session](const std::string& line) { session.send_command(line); };
    manager.resize(427, 240);

    auto screen = std::make_shared<mcpp::ui::ChatScreen>("", false);
    screen->chat = &chat;
    screen->commands = &session.commands();
    manager.set_screen(screen);
    manager.apply_pending();

    int failures = 0;
    const auto check = [&failures](bool condition, const std::string& what) {
        std::cout << (condition ? "ok   " : "FAIL ") << what << '\n';
        if (!condition) ++failures;
    };

    check(screen->input != nullptr, "the chat screen has an input line");
    if (screen->input == nullptr) return 1;

    // The command tree the server sent: typing a slash offers the commands
    // this player is allowed, which for someone who is not an operator is the
    // handful the server leaves unrestricted.
    const auto offered = mcpp::ui::command_suggestions(session.commands(), "/", 1);
    check(offered.size() >= 5,
          std::format("the server's command tree offers {} commands", offered.size()));
    std::string listed;
    for (const auto& suggestion : offered) listed += suggestion.text + " ";
    std::cout << "     offered: " << listed << '\n';
    if (!offered.empty()) {
        // Typing the first letter of the first command narrows to it.
        const std::string first = offered.front().text;
        const std::string head = "/" + first.substr(0, 1);
        const auto narrowed =
            mcpp::ui::command_suggestions(session.commands(), head, head.size());
        bool kept = false;
        for (const auto& suggestion : narrowed) {
            if (suggestion.text == first) kept = true;
        }
        check(kept, std::format("typing {} still offers {}", head, first));
        check(narrowed.size() <= offered.size(), "and offers no more than the whole list");
        // The word being completed starts right after the slash.
        if (!narrowed.empty()) {
            check(narrowed.front().start == 1 && narrowed.front().end == head.size(),
                  "the word being completed is the one after the slash");
        }
    }

    const std::string line = std::format("plasma chat test {}", username);
    type(manager, line);
    check(screen->input->value() == line,
          std::format("the line reads '{}'", screen->input->value()));

    // Enter sends it and closes the screen, the way the game does.
    manager.key(mcpp::ui::key::Enter, 0, 1, 0);
    manager.apply_pending();
    check(manager.current() == nullptr, "the screen closes on enter");
    check(chat.recent.size() == 1 && chat.recent.back() == line,
          "the line went into the history");

    pump_for(session, 2.0);
    bool echoed = false;
    {
        std::lock_guard lock(guard);
        for (const auto& said : heard) {
            if (said.find(line) != std::string::npos && said.find(username) != std::string::npos) {
                echoed = true;
            }
        }
        check(echoed, std::format("the server sent the line back ({} lines heard)", heard.size()));
        if (!echoed) {
            for (const auto& said : heard) std::cout << "  heard: " << said << '\n';
        }
    }

    // And a command the server answers: /seed tells us the world's seed.
    auto again = std::make_shared<mcpp::ui::ChatScreen>("", false);
    again->chat = &chat;
    again->commands = &session.commands();
    manager.set_screen(again);
    manager.apply_pending();
    {
        std::lock_guard lock(guard);
        heard.clear();
    }
    type(manager, "/me waves");
    manager.key(mcpp::ui::key::Enter, 0, 1, 0);
    manager.apply_pending();
    pump_for(session, 1.5);
    bool answered = false;
    {
        std::lock_guard lock(guard);
        for (const auto& said : heard) {
            if (said.find("waves") != std::string::npos &&
                said.find(username) != std::string::npos) {
                answered = true;
            }
        }
        check(answered, std::format("the server answered /me ({} lines heard)", heard.size()));
        if (!answered) {
            for (const auto& said : heard) std::cout << "  heard: " << said << '\n';
        }
    }
    check(chat.recent.size() == 2, "the command went into the history too");

    session.close();

    // And the server list's own ping of the same server: the version, the
    // player count and a round trip time the row shows.
    mcpp::ui::ServerList servers;
    mcpp::ui::ServerEntry entry;
    entry.name = "test";
    entry.ip = args[0];
    servers.entries.push_back(entry);
    mcpp::ui::ServerPinger pinger;
    pinger.ping(0, args[0], -1);
    pinger.wait_all();
    pinger.take(servers);
    const auto& pinged = servers.entries[0];
    check(pinged.state == mcpp::ui::ServerEntry::State::Successful,
          std::format("the status ping succeeded (state {})", static_cast<int>(pinged.state)));
    check(pinged.version == mcpp::protocol::kTargetVersionName,
          std::format("the server says it is {}", pinged.version));
    check(pinged.protocol == mcpp::protocol::kTargetProtocol,
          std::format("on protocol {}", pinged.protocol));
    check(pinged.players_max > 0,
          std::format("with {}/{} players", pinged.players_online, pinged.players_max));
    check(pinged.ping >= 0, std::format("in {} ms", pinged.ping));

    if (failures != 0) {
        std::cout << failures << " failures\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}

} // namespace client
