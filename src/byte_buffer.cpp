#include "mcpp/byte_buffer.hpp"

#include <bit>
#include <cstring>
#include <format>

namespace mcpp {

void ByteBuffer::need(size_t n) const {
    if (remaining() < n) {
        throw ProtocolError(std::format("buffer underflow: need {} bytes, {} left", n, remaining()));
    }
}

void ByteBuffer::write_u8(uint8_t v) { data_.push_back(v); }

void ByteBuffer::write_u16(uint16_t v) {
    data_.push_back(static_cast<uint8_t>(v >> 8));
    data_.push_back(static_cast<uint8_t>(v));
}

void ByteBuffer::write_i32(int32_t v) {
    auto u = static_cast<uint32_t>(v);
    for (int shift = 24; shift >= 0; shift -= 8) {
        data_.push_back(static_cast<uint8_t>(u >> shift));
    }
}

void ByteBuffer::write_i64(int64_t v) {
    auto u = static_cast<uint64_t>(v);
    for (int shift = 56; shift >= 0; shift -= 8) {
        data_.push_back(static_cast<uint8_t>(u >> shift));
    }
}

void ByteBuffer::write_f32(float v) { write_i32(static_cast<int32_t>(std::bit_cast<uint32_t>(v))); }
void ByteBuffer::write_f64(double v) { write_i64(static_cast<int64_t>(std::bit_cast<uint64_t>(v))); }

void ByteBuffer::write_varint(int32_t v) {
    auto u = static_cast<uint32_t>(v);
    while (true) {
        if ((u & ~0x7Fu) == 0) {
            data_.push_back(static_cast<uint8_t>(u));
            return;
        }
        data_.push_back(static_cast<uint8_t>((u & 0x7F) | 0x80));
        u >>= 7;
    }
}

void ByteBuffer::write_varlong(int64_t v) {
    auto u = static_cast<uint64_t>(v);
    while (true) {
        if ((u & ~static_cast<uint64_t>(0x7F)) == 0) {
            data_.push_back(static_cast<uint8_t>(u));
            return;
        }
        data_.push_back(static_cast<uint8_t>((u & 0x7F) | 0x80));
        u >>= 7;
    }
}

void ByteBuffer::write_string(std::string_view s) {
    write_varint(static_cast<int32_t>(s.size()));
    data_.insert(data_.end(), s.begin(), s.end());
}

void ByteBuffer::write_bytes(std::span<const uint8_t> b) {
    data_.insert(data_.end(), b.begin(), b.end());
}

uint8_t ByteBuffer::read_u8() {
    need(1);
    return data_[pos_++];
}

uint16_t ByteBuffer::read_u16() {
    need(2);
    uint16_t v = static_cast<uint16_t>(data_[pos_] << 8 | data_[pos_ + 1]);
    pos_ += 2;
    return v;
}

int32_t ByteBuffer::read_i32() {
    need(4);
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | data_[pos_ + static_cast<size_t>(i)];
    pos_ += 4;
    return static_cast<int32_t>(v);
}

int64_t ByteBuffer::read_i64() {
    need(8);
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | data_[pos_ + static_cast<size_t>(i)];
    pos_ += 8;
    return static_cast<int64_t>(v);
}

float ByteBuffer::read_f32() { return std::bit_cast<float>(static_cast<uint32_t>(read_i32())); }
double ByteBuffer::read_f64() { return std::bit_cast<double>(static_cast<uint64_t>(read_i64())); }

int32_t ByteBuffer::read_varint() {
    uint32_t result = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        uint8_t byte = read_u8();
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return static_cast<int32_t>(result);
    }
    throw ProtocolError("varint longer than 5 bytes");
}

int64_t ByteBuffer::read_varlong() {
    uint64_t result = 0;
    for (int shift = 0; shift < 70; shift += 7) {
        uint8_t byte = read_u8();
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return static_cast<int64_t>(result);
    }
    throw ProtocolError("varlong longer than 10 bytes");
}

std::string ByteBuffer::read_string(size_t max_len) {
    int32_t len = read_varint();
    if (len < 0 || static_cast<size_t>(len) > max_len * 4) {
        throw ProtocolError(std::format("string length {} out of range", len));
    }
    need(static_cast<size_t>(len));
    std::string s(reinterpret_cast<const char*>(data_.data() + pos_), static_cast<size_t>(len));
    pos_ += static_cast<size_t>(len);
    return s;
}

std::vector<uint8_t> ByteBuffer::read_bytes(size_t n) {
    need(n);
    std::vector<uint8_t> out(data_.begin() + static_cast<long>(pos_),
                             data_.begin() + static_cast<long>(pos_ + n));
    pos_ += n;
    return out;
}

size_t ByteBuffer::varint_size(int32_t v) {
    auto u = static_cast<uint32_t>(v);
    size_t n = 1;
    while ((u & ~0x7Fu) != 0) {
        u >>= 7;
        ++n;
    }
    return n;
}

} // namespace mcpp
