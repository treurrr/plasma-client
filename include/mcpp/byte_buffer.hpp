#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp {

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class TimeoutError : public ProtocolError {
public:
    using ProtocolError::ProtocolError;
};

// Fixed-width ints are big-endian; lengths and packet ids are LEB128 varints.
class ByteBuffer {
public:
    ByteBuffer() = default;
    explicit ByteBuffer(std::vector<uint8_t> data) : data_(std::move(data)) {}

    void write_u8(uint8_t v);
    void write_bool(bool v) { write_u8(v ? 1 : 0); }
    void write_u16(uint16_t v);
    void write_i32(int32_t v);
    void write_i64(int64_t v);
    void write_f32(float v);
    void write_f64(double v);
    void write_varint(int32_t v);
    void write_varlong(int64_t v);
    void write_string(std::string_view s);
    void write_bytes(std::span<const uint8_t> b);

    uint8_t  read_u8();
    bool     read_bool() { return read_u8() != 0; }
    uint16_t read_u16();
    int32_t  read_i32();
    int64_t  read_i64();
    float    read_f32();
    double   read_f64();
    int32_t  read_varint();
    int64_t  read_varlong();
    std::string read_string(size_t max_len = 32767);
    std::vector<uint8_t> read_bytes(size_t n);

    size_t remaining() const { return data_.size() - pos_; }
    size_t position() const { return pos_; }
    void   set_position(size_t p) { pos_ = p; }
    const std::vector<uint8_t>& data() const { return data_; }
    std::vector<uint8_t> take() { pos_ = 0; return std::move(data_); }
    void clear() { data_.clear(); pos_ = 0; }

    static size_t varint_size(int32_t v);

private:
    void need(size_t n) const;

    std::vector<uint8_t> data_;
    size_t pos_ = 0;
};

} // namespace mcpp
