#pragma once
#include "mcpp/byte_buffer.hpp"
#include "mcpp/crypto.hpp"
#include "mcpp/tcp_socket.hpp"

namespace mcpp {

// Uncompressed frame: varint(len) + varint(id) + payload.
// Compressed frame:   varint(len) + varint(uncompressed_size) + body,
//                     where uncompressed_size 0 means the body is not deflated.
class PacketConnection {
public:
    struct Packet {
        int32_t id = 0;
        ByteBuffer data;
    };

    PacketConnection() = default;
    explicit PacketConnection(TcpSocket&& socket) : socket_(std::move(socket)) {}

    void send(int32_t id, const ByteBuffer& payload);
    Packet receive();

    // threshold < 0 disables compression again
    void set_compression_threshold(int32_t threshold) { compression_threshold_ = threshold; }
    int32_t compression_threshold() const { return compression_threshold_; }

    // Everything on the wire is AES-CFB8 from this point on, in both
    // directions, keyed with the same sixteen bytes.
    void start_encryption(std::span<const uint8_t> secret);
    bool encrypted() const { return outgoing_.active(); }

    TcpSocket& socket() { return socket_; }
    const TcpSocket& socket() const { return socket_; }

private:
    int32_t read_wire_varint();

    TcpSocket socket_;
    crypto::Stream outgoing_;
    crypto::Stream incoming_;
    int32_t compression_threshold_ = -1;
};

} // namespace mcpp
