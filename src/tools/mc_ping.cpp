#include "mcpp/json.hpp"
#include "mcpp/packet_io.hpp"

#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

namespace {

constexpr int32_t kProtocolUnknown = -1;

constexpr int32_t kIntentStatus = 1;

struct Options {
    std::string host = "127.0.0.1";
    uint16_t port = 25565;
    int32_t protocol = kProtocolUnknown;
    bool raw_json = false;
    int timeout_ms = 5000;
};

[[noreturn]] void usage(const char* argv0) {
    std::cerr << std::format(
        "usage: {} <host>[:port] [--protocol N] [--json] [--timeout MS]\n"
        "\n"
        "  <host>[:port]   server to ping (default port 25565)\n"
        "  --protocol N    protocol version to claim (default -1, \"unknown\")\n"
        "  --json          also print the raw status JSON\n"
        "  --timeout MS    socket timeout in milliseconds (default 5000)\n",
        argv0);
    std::exit(2);
}

Options parse_args(int argc, char** argv) {
    if (argc < 2) usage(argv[0]);

    Options options;
    std::string target = argv[1];
    if (target.starts_with("-")) usage(argv[0]);

    // Only split on the last colon so IPv6 literals in brackets survive.
    if (auto colon = target.rfind(':'); colon != std::string::npos && target.back() != ']') {
        options.host = target.substr(0, colon);
        options.port = static_cast<uint16_t>(std::stoi(target.substr(colon + 1)));
    } else {
        options.host = target;
    }
    if (options.host.starts_with("[") && options.host.ends_with("]")) {
        options.host = options.host.substr(1, options.host.size() - 2);
    }

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) usage(argv[0]);
            return argv[++i];
        };
        if (arg == "--protocol") {
            options.protocol = std::stoi(next());
        } else if (arg == "--json") {
            options.raw_json = true;
        } else if (arg == "--timeout") {
            options.timeout_ms = std::stoi(next());
        } else {
            usage(argv[0]);
        }
    }
    return options;
}

void print_status(const mcpp::json::Value& root, bool raw, const std::string& raw_text,
                  double latency_ms) {
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

    std::string motd;
    if (const auto* description = root.find("description")) {
        motd = mcpp::json::strip_formatting(mcpp::json::flatten_component(*description));
    }
    // MOTDs are often two lines.
    for (char& c : motd) {
        if (c == '\n') c = ' ';
    }

    std::cout << std::format("version   : {} (protocol {})\n", version, protocol);
    std::cout << std::format("players   : {}/{}\n", online, max);
    std::cout << std::format("motd      : {}\n", motd);
    std::cout << std::format("latency   : {:.1f} ms\n", latency_ms);
    if (const auto* icon = root.find("favicon")) {
        const auto* data = icon->as_string();
        std::cout << std::format("favicon   : {} bytes of base64\n", data ? data->size() : 0);
    }
    if (const auto* players = root.find("players")) {
        if (const auto* sample = players->find("sample"); sample != nullptr) {
            if (const auto* list = sample->as_array()) {
                std::cout << "sample    :";
                for (const auto& entry : *list) {
                    if (const auto* name = entry.find("name")) {
                        std::cout << ' ' << name->string_or("?");
                    }
                }
                std::cout << '\n';
            }
        }
    }
    if (raw) std::cout << "\nraw       : " << raw_text << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parse_args(argc, argv);

    try {
        mcpp::TcpSocket socket;
        socket.connect(options.host, options.port, options.timeout_ms);
        mcpp::PacketConnection connection(std::move(socket));

        mcpp::ByteBuffer handshake;
        handshake.write_varint(options.protocol);
        handshake.write_string(options.host);
        handshake.write_u16(options.port);
        handshake.write_varint(kIntentStatus);
        connection.send(0x00, handshake);

        connection.send(0x00, mcpp::ByteBuffer{});

        auto packet = connection.receive();
        if (packet.id != 0x00) {
            throw mcpp::ProtocolError(std::format("expected status response, got id 0x{:02x}",
                                                  packet.id));
        }
        const std::string raw_text = packet.data.read_string(32767);

        const auto sent_at = std::chrono::steady_clock::now();
        mcpp::ByteBuffer ping;
        ping.write_i64(std::chrono::duration_cast<std::chrono::milliseconds>(
                           sent_at.time_since_epoch())
                           .count());
        connection.send(0x01, ping);

        double latency_ms = -1.0;
        auto pong = connection.receive();
        if (pong.id == 0x01) {
            const auto elapsed = std::chrono::steady_clock::now() - sent_at;
            latency_ms = std::chrono::duration<double, std::milli>(elapsed).count();
        }

        print_status(mcpp::json::parse(raw_text), options.raw_json, raw_text, latency_ms);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << std::format("ping failed: {}\n", error.what());
        return 1;
    }
}
