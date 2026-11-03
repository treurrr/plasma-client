#include "mcpp/json.hpp"
#include "mcpp/status.hpp"

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

namespace {

constexpr int32_t kProtocolUnknown = -1;

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

void print_status(const mcpp::status::Reply& reply, bool raw) {
    const auto& root = reply.root;
    std::string motd = reply.motd;
    // MOTDs are often two lines.
    for (char& c : motd) {
        if (c == '\n') c = ' ';
    }

    std::cout << std::format("version   : {} (protocol {})\n", reply.version, reply.protocol);
    std::cout << std::format("players   : {}/{}\n", reply.online, reply.max);
    std::cout << std::format("motd      : {}\n", motd);
    std::cout << std::format("latency   : {:.1f} ms\n", reply.latency_ms);
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
    if (raw) std::cout << "\nraw       : " << reply.raw << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parse_args(argc, argv);

    try {
        mcpp::status::Request request;
        request.host = options.host;
        request.port = options.port;
        request.protocol = options.protocol;
        request.timeout_ms = options.timeout_ms;
        print_status(mcpp::status::query(request), options.raw_json);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << std::format("ping failed: {}\n", error.what());
        return 1;
    }
}
