#include "mcpp/status.hpp"

#include "mcpp/packet_io.hpp"

#include <chrono>
#include <format>

namespace mcpp::status {
namespace {

constexpr int32_t kIntentStatus = 1;

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
    }
    if (const auto* description = root.find("description")) {
        reply.motd = json::strip_formatting(json::flatten_component(*description));
    }
    return reply;
}

} // namespace mcpp::status
