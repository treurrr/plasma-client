#include "mcpp/status.hpp"

#include "mcpp/packet_io.hpp"

#include <chrono>
#include <format>

namespace mcpp::status {
namespace {

constexpr int32_t kIntentStatus = 1;
constexpr std::string_view kPngPrefix = "data:image/png;base64,";


} // namespace

Reply query(const Request& request) {
    TcpSocket socket;
    socket.connect(request.host, request.port, request.timeout_ms);
    PacketConnection connection(std::move(socket));

    ByteBuffer handshake;
    handshake.write_varint(request.protocol);
    handshake.write_string(request.host);
    handshake.write_u16(request.port);
    handshake.write_varint(kIntentStatus);
    connection.send(0x00, handshake);

    connection.send(0x00, ByteBuffer{});

    auto packet = connection.receive();
    if (packet.id != 0x00) {
        throw ProtocolError(std::format("expected status response, got id 0x{:02x}", packet.id));
    }
    Reply reply;
    reply.raw = packet.data.read_string(32767);

    if (request.measure_latency) {
        const auto sent_at = std::chrono::steady_clock::now();
        ByteBuffer ping;
        ping.write_i64(std::chrono::duration_cast<std::chrono::milliseconds>(
                           sent_at.time_since_epoch())
                           .count());
        connection.send(0x01, ping);

        auto pong = connection.receive();
        if (pong.id == 0x01) {
            const auto elapsed = std::chrono::steady_clock::now() - sent_at;
            reply.latency_ms = std::chrono::duration<double, std::milli>(elapsed).count();
        }
    }

    reply.root = json::parse(reply.raw);
    const auto& root = reply.root;
    if (const auto* v = root.find("version")) {
        if (const auto* name = v->find("name")) reply.version = name->string_or("?");
        if (const auto* proto = v->find("protocol")) {
            reply.protocol = static_cast<int32_t>(proto->number_or(-1));
        }
    }
    if (const auto* players = root.find("players")) {
        if (const auto* o = players->find("online")) {
            reply.online = static_cast<int32_t>(o->number_or(-1));
        }
        if (const auto* m = players->find("max")) reply.max = static_cast<int32_t>(m->number_or(-1));
        const auto* sample = players->find("sample");
        if (const auto* list = sample != nullptr ? sample->as_array() : nullptr) {
            for (const auto& player : *list) {
                const auto* name = player.find("name");
                const auto* id = player.find("id");
                reply.sample.emplace_back(name != nullptr ? name->string_or("") : "",
                                          id != nullptr ? id->string_or("") : "");
            }
        }
    }
    if (const auto* icon = root.find("favicon")) {
        const std::string url = icon->string_or("");
        if (url.starts_with(kPngPrefix)) {
            reply.favicon_png = decode_base64(std::string_view(url).substr(kPngPrefix.size()));
        }
    }
    if (const auto* secure = root.find("enforcesSecureChat")) {
        reply.enforces_secure_chat = secure->as_bool().value_or(false);
    }
    if (const auto* description = root.find("description")) {
        reply.motd = json::strip_formatting(json::flatten_component(*description));
    }
    return reply;
}

std::vector<uint8_t> decode_base64(std::string_view text) {
    std::vector<uint8_t> out;
    uint32_t bits = 0;
    int count = 0;
    for (const char c : text) {
        int value = -1;
        if (c >= 'A' && c <= 'Z') value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '+') value = 62;
        else if (c == '/') value = 63;
        else if (c == '=') break;
        else if (c == '\n' || c == '\r') continue;
        if (value < 0) return {};
        bits = bits << 6 | static_cast<uint32_t>(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out.push_back(static_cast<uint8_t>(bits >> count));
        }
    }
    return out;
}

} // namespace mcpp::status
