#include "mcpp/features.hpp"
#include "mcpp/json.hpp"
#include "mcpp/packet_io.hpp"
#include "mcpp/protocol_versions.hpp"
#include "mcpp/session.hpp"

#include <atomic>
#include <cstdlib>
#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

void print_banner() {
    std::cout << std::format("Plodder Client 0.1.0  ·  doelversie {} (protocol {})\n",
                             mcpp::protocol::kTargetVersionName,
                             mcpp::protocol::kTargetProtocol);
}

void print_versions() {
    std::cout << "\nversies:\n";
    for (const auto& version : mcpp::protocol::all_versions()) {
        std::cout << std::format("  {:<9} protocol {:<4} {}\n", version.name, version.protocol,
                                 version.supported ? "ondersteund" : "gepland");
    }
}

void print_features() {
    auto& registry = mcpp::features::Registry::instance();
    registry.load(mcpp::features::Registry::default_config_path());

    print_banner();
    std::cout << std::format("\nconfig: {}\n", mcpp::features::Registry::default_config_path().string());

    auto current = mcpp::features::Category::Hud;
    bool first = true;
    for (size_t i = 0; i < registry.all().size(); ++i) {
        const auto& feature = registry.all()[i];
        if (first || feature.category != current) {
            std::cout << std::format("\n  {}\n", mcpp::features::category_name(feature.category));
            current = feature.category;
            first = false;
        }
        const bool on = registry.enabled(static_cast<mcpp::features::Id>(i));
        std::cout << std::format("    [{}] {:<24}{}\n", on ? 'x' : ' ', feature.label,
                                 feature.server_may_object ? "  sommige servers verbieden dit" : "");
    }
    std::cout << "\nGeen reach, aim assist, auto-clicker of ESP. Dat blijft zo.\n";
}

struct Address {
    std::string host;
    uint16_t port = 25565;
};

Address parse_address(const std::string& target) {
    Address address{target, 25565};
    if (auto colon = target.rfind(':'); colon != std::string::npos) {
        address.host = target.substr(0, colon);
        address.port = static_cast<uint16_t>(std::stoi(target.substr(colon + 1)));
    }
    return address;
}

int do_join(const std::string& target, const std::string& username, int32_t protocol) {
    const auto address = parse_address(target);

    std::mutex out_mutex;
    auto print = [&](char marker, std::string_view line) {
        std::lock_guard guard(out_mutex);
        std::cout << std::format("{} {}\n", marker, line) << std::flush;
    };

    mcpp::SessionOptions options{.host = address.host, .port = address.port, .username = username,
                                 .protocol = protocol,
                                 .debug_packets = std::getenv("PLODDER_DEBUG") != nullptr};
    mcpp::SessionEvents events{
        .on_status = [&](std::string_view line) { print('-', line); },
        .on_chat = [&](std::string_view line) { print('|', line); },
        .on_disconnect = [&](std::string_view line) { print('x', line); },
    };

    mcpp::Session session(options, events);
    session.connect();

    std::atomic<bool> running{true};
    std::thread reader([&] {
        while (running.load()) {
            try {
                session.pump();
            } catch (const mcpp::TimeoutError&) {
                continue;
            } catch (const std::exception& error) {
                if (running.load()) print('x', error.what());
                running.store(false);
                return;
            }
        }
    });

    print('-', "typ om te chatten, /commando om een commando te sturen, /quit om te stoppen");
    std::string line;
    while (running.load() && std::getline(std::cin, line)) {
        if (line == "/quit") break;
        if (line.empty()) continue;
        try {
            if (line.front() == '/') {
                session.send_command(line.substr(1));
            } else {
                session.send_chat(line);
            }
        } catch (const std::exception& error) {
            print('x', error.what());
        }
    }

    running.store(false);
    session.close();
    reader.join();
    return 0;
}

int do_ping(const std::string& target) {
    const auto address = parse_address(target);
    const std::string& host = address.host;
    const uint16_t port = address.port;

    mcpp::TcpSocket socket;
    socket.connect(host, port);
    mcpp::PacketConnection connection(std::move(socket));

    mcpp::ByteBuffer handshake;
    // -1 = unknown: ViaVersion mirrors back whatever we claim, which would
    // hide the server's real version.
    handshake.write_varint(-1);
    handshake.write_string(host);
    handshake.write_u16(port);
    handshake.write_varint(1);
    connection.send(0x00, handshake);
    connection.send(0x00, mcpp::ByteBuffer{});

    auto packet = connection.receive();
    auto root = mcpp::json::parse(packet.data.read_string(32767));

    std::string version = "?";
    int protocol = -1;
    if (const auto* v = root.find("version")) {
        if (const auto* name = v->find("name")) version = name->string_or("?");
        if (const auto* proto = v->find("protocol")) protocol = static_cast<int>(proto->number_or(-1));
    }

    int online = -1;
    int max = -1;
    if (const auto* players = root.find("players")) {
        if (const auto* o = players->find("online")) online = static_cast<int>(o->number_or(-1));
        if (const auto* m = players->find("max")) max = static_cast<int>(m->number_or(-1));
    }

    std::cout << std::format("{}:{}  {} (protocol {})  {}/{} spelers\n", host, port, version,
                             protocol, online, max);

    if (protocol == mcpp::protocol::kTargetProtocol) {
        std::cout << "past: wij spreken dezelfde versie.\n";
    } else {
        std::cout << std::format("wij spreken {} ({}); werkt alleen als de server ViaVersion heeft.\n",
                                 mcpp::protocol::kTargetProtocol, mcpp::protocol::kTargetVersionName);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";

    try {
        if (command == "features") {
            print_features();
            return 0;
        }
        if (command == "versions") {
            print_banner();
            print_versions();
            return 0;
        }
        if (command == "ping" && argc > 2) {
            return do_ping(argv[2]);
        }
        if (command == "join" && argc > 2) {
            const std::string username = argc > 3 ? argv[3] : "PlodderDev";
            const int32_t protocol = argc > 4 ? std::stoi(argv[4])
                                              : mcpp::protocol::kTargetProtocol;
            return do_join(argv[2], username, protocol);
        }

        print_banner();
        std::cout << std::format(
            "\ngebruik: {} <commando>\n"
            "  features            welke functies aan staan\n"
            "  versions            ondersteunde protocolversies\n"
            "  ping <host:poort>   serverinfo opvragen\n"
            "  join <host:poort> [naam] [protocol]\n"
            "                      inloggen en in de play-state blijven\n"
            "\nrenderen volgt in mijlpaal 3.\n",
            argv[0]);
        return command.empty() ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << std::format("fout: {}\n", error.what());
        return 1;
    }
}
