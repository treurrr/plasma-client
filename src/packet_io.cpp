#include "mcpp/packet_io.hpp"

#include <format>
#include <zlib.h>

namespace mcpp {

namespace {

constexpr size_t kMaxPacketSize = 8 * 1024 * 1024;

std::vector<uint8_t> zlib_compress(std::span<const uint8_t> in) {
    uLongf bound = compressBound(static_cast<uLong>(in.size()));
    std::vector<uint8_t> out(bound);
    int rc = ::compress2(out.data(), &bound, in.data(), static_cast<uLong>(in.size()), 6);
    if (rc != Z_OK) throw ProtocolError(std::format("zlib compress failed: {}", rc));
    out.resize(bound);
    return out;
}

std::vector<uint8_t> zlib_decompress(std::span<const uint8_t> in, size_t expected) {
    std::vector<uint8_t> out(expected);
    uLongf size = static_cast<uLongf>(expected);
    int rc = ::uncompress(out.data(), &size, in.data(), static_cast<uLong>(in.size()));
    if (rc != Z_OK) throw ProtocolError(std::format("zlib decompress failed: {}", rc));
    if (size != expected) {
        throw ProtocolError(std::format("decompressed {} bytes, server promised {}", size, expected));
    }
    return out;
}

} // namespace

int32_t PacketConnection::read_wire_varint() {
    // Cannot go through ByteBuffer: we don't know yet how many bytes to read.
    uint32_t result = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        uint8_t byte = 0;
        socket_.recv_exact(&byte, 1);
        incoming_.apply(&byte, 1);
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return static_cast<int32_t>(result);
    }
    throw ProtocolError("frame varint longer than 5 bytes");
}

void PacketConnection::send(int32_t id, const ByteBuffer& payload) {
    ByteBuffer body;
    body.write_varint(id);
    body.write_bytes(payload.data());

    ByteBuffer frame;
    if (compression_threshold_ < 0) {
        frame.write_varint(static_cast<int32_t>(body.data().size()));
        frame.write_bytes(body.data());
    } else if (body.data().size() >= static_cast<size_t>(compression_threshold_)) {
        auto compressed = zlib_compress(body.data());
        const auto uncompressed_size = static_cast<int32_t>(body.data().size());
        frame.write_varint(static_cast<int32_t>(ByteBuffer::varint_size(uncompressed_size) +
                                                compressed.size()));
        frame.write_varint(uncompressed_size);
        frame.write_bytes(compressed);
    } else {
        frame.write_varint(static_cast<int32_t>(1 + body.data().size()));
        frame.write_varint(0);
        frame.write_bytes(body.data());
    }

    if (outgoing_.active()) {
        auto bytes = frame.data();
        std::vector<uint8_t> sealed(bytes.begin(), bytes.end());
        outgoing_.apply(sealed.data(), sealed.size());
        socket_.send_all(sealed);
    } else {
        socket_.send_all(frame.data());
    }
}

PacketConnection::Packet PacketConnection::receive() {
    int32_t length = read_wire_varint();
    if (length <= 0 || static_cast<size_t>(length) > kMaxPacketSize) {
        throw ProtocolError(std::format("bogus packet length {}", length));
    }

    std::vector<uint8_t> raw(static_cast<size_t>(length));
    socket_.recv_exact(raw.data(), raw.size());
    incoming_.apply(raw.data(), raw.size());

    ByteBuffer buf(std::move(raw));
    if (compression_threshold_ >= 0) {
        int32_t uncompressed_size = buf.read_varint();
        if (uncompressed_size != 0) {
            if (static_cast<size_t>(uncompressed_size) > kMaxPacketSize) {
                throw ProtocolError(std::format("bogus uncompressed size {}", uncompressed_size));
            }
            auto rest = buf.read_bytes(buf.remaining());
            buf = ByteBuffer(zlib_decompress(rest, static_cast<size_t>(uncompressed_size)));
        }
    }

    Packet packet;
    packet.id = buf.read_varint();
    packet.data = ByteBuffer(buf.read_bytes(buf.remaining()));
    return packet;
}

void PacketConnection::start_encryption(std::span<const uint8_t> secret) {
    outgoing_.start(secret, true);
    incoming_.start(secret, false);
}

} // namespace mcpp
